#include "mp_config.h"
#include "mplib.h"
#define MSGT_CLASS MSGT_OSDEP
#include "mp_msg.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <malloc.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

typedef struct mp_slot_s {
    any_t*	page_ptr;
    size_t	size;
    size_t	ncalls;
    any_t*	calls[10];
}mp_slot_t;

typedef struct mp_slot_container_s {
    mp_slot_t*	slots;
    size_t	nslots;
    size_t	size;
}mp_slot_container_t;

typedef struct priv_s {
    const char*			argv0;
    unsigned			rnd_limit;
    unsigned			every_nth_call;
    enum mp_malloc_e		flags;
    /* for randomizer */
    unsigned long long int	total_calls;
    /* backtrace and protector slots */
    mp_slot_container_t		mallocs;/* not freed mallocs */
    mp_slot_container_t		reallocs; /* suspect reallocs */
    mp_slot_container_t		frees;    /* suspect free */
}priv_t;
static priv_t* priv;

static any_t* prot_page_align(any_t *ptr) { return (any_t*)(((unsigned long)ptr)&(~(__VM_PAGE_SIZE__-1))); }
static size_t app_fullsize(size_t size) {
    unsigned npages = size/__VM_PAGE_SIZE__;
    unsigned fullsize;
    if(size%__VM_PAGE_SIZE__) npages++;
    npages++;
    fullsize=npages*__VM_PAGE_SIZE__;
    return fullsize;
}
static any_t* prot_last_page(any_t* rp,size_t fullsize) { return rp+(fullsize-__VM_PAGE_SIZE__); }
static void __prot_print_slots(mp_slot_container_t* c) {
    size_t i;
    for(i=0;i<c->nslots;i++) {
	MSG_INFO("slot[%u] address: %p size: %u\n"
		,i
		,c->slots[i].page_ptr
		,c->slots[i].size);
    }
}

static size_t	prot_find_slot_idx(mp_slot_container_t* c,any_t* ptr) {
    size_t i;
    for(i=0;i<c->nslots;i++) {
	if(c->slots[i].page_ptr==ptr) return i;
    }
    return UINT_MAX;
}

static mp_slot_t*	prot_find_slot(mp_slot_container_t* c,any_t* ptr) {
    size_t idx=prot_find_slot_idx(c,ptr);
    if(idx!=UINT_MAX) return &c->slots[idx];
    return NULL;
}

static mp_slot_t*	prot_append_slot(mp_slot_container_t* c,any_t*ptr,size_t size) {
    mp_slot_t* s;
    s=c->slots;
    if(!c->slots) {
	c->size=16;
	s=malloc(sizeof(mp_slot_t)*16);
    }
    else {
	if(c->nslots+1>c->size) {
	    c->size+=16;
	    s=realloc(c->slots,sizeof(mp_slot_t)*c->size);
	}
    }
    c->slots=s;
    memset(&c->slots[c->nslots],0,sizeof(mp_slot_t));
    c->slots[c->nslots].page_ptr=ptr;
    c->slots[c->nslots].size=size;
    c->nslots++;
    return &c->slots[c->nslots-1];
}

static void	prot_free_slot(mp_slot_container_t* c,any_t* ptr) {
    size_t idx=prot_find_slot_idx(c,ptr);
    if(idx!=UINT_MAX) {
	memmove(&c->slots[idx],&c->slots[idx+1],sizeof(mp_slot_t)*(c->nslots-(idx+1)));
	c->nslots--;
	if(c->nslots<c->size-16) {
	    c->size-=16;
	    c->slots=realloc(c->slots,sizeof(mp_slot_t)*c->size);
	}
    } else printf("[prot_free_slot] Internal error! Can't find slot for address: %p\n",ptr);
}

/* ----------- append ------------ */

static any_t* __prot_malloc_append(size_t size) {
    any_t* rp;
    size_t fullsize=app_fullsize(size);
    rp=memalign(__VM_PAGE_SIZE__,fullsize);
    if(rp) {
	prot_append_slot(&priv->mallocs,rp,size);
	// protect last page here
	mprotect(prot_last_page(rp,fullsize),__VM_PAGE_SIZE__,MP_DENY_ALL);
	rp+=fullsize-__VM_PAGE_SIZE__-size;
    }
    return rp;
}

static any_t* __prot_memalign_append(size_t boundary,size_t size) { UNUSED(boundary);  return __prot_malloc_append(size); }

/* REPORT */
typedef struct bt_cache_entry_s {
    any_t*	addr;
    char*	str;
}bt_cache_entry_t;

typedef struct bt_cache_s {
    bt_cache_entry_t*	entry;
    unsigned		num_entries;
}bt_cache_t;

static bt_cache_t*	init_bt_cache(void) { return calloc(1,sizeof(bt_cache_t)); }

static void		uninit_bt_cache(bt_cache_t* cache) {
    unsigned i;
    for(i=0;i<cache->num_entries;i++) free(cache->entry[i].str);
    free(cache->entry);
    free(cache);
}

static char* bt_find_cache(bt_cache_t* cache,any_t* ptr) {
    unsigned i;
    for(i=0;i<cache->num_entries;i++) if(cache->entry[i].addr==ptr) return cache->entry[i].str;
    return NULL;
}

static bt_cache_entry_t* bt_append_cache(bt_cache_t* cache,any_t* ptr,const char *str) {
    if(!cache->entry)	cache->entry=malloc(sizeof(bt_cache_entry_t));
    else		cache->entry=realloc(cache->entry,sizeof(bt_cache_entry_t)*(cache->num_entries+1));
    cache->entry[cache->num_entries].addr=ptr;
    cache->entry[cache->num_entries].str=strdup(str);
    cache->num_entries++;
    return &cache->entry[cache->num_entries-1];
}

static char * addr2line(bt_cache_t* cache,any_t*ptr) {
    char *rs;
    if(priv->argv0) {
	bt_cache_entry_t* centry;
	unsigned i;
	char cmd[4096],result[4096];
	char ch;
	if((rs=bt_find_cache(cache,ptr))!=NULL) return rs;
	sprintf(cmd,"addr2line -s -e %s %p\n",priv->argv0,ptr);
	FILE* in=popen(cmd,"r");
	if(!in) return NULL;
	i=0;
	while(1) {
	    ch=fgetc(in);
	    if(feof(in)) break;
	    if(ch=='\n') break;
	    result[i++]=ch;
	    if(i>=sizeof(result)-1) break;
	}
	result[i]='\0';
	pclose(in);
	centry=bt_append_cache(cache,ptr,result);
	return centry->str;
    }
    return NULL;
}

static __always_inline void __print_backtrace(unsigned num) {
    bt_cache_t* cache=init_bt_cache();
    any_t*	calls[num];
    unsigned	i,ncalls;
    ncalls=backtrace(calls,num);
    MSG_INFO("*** Backtrace for suspect call ***\n");
    for(i=0;i<ncalls;i++) {
	MSG_INFO("    %p -> %s\n",calls[i],addr2line(cache,calls[i]));
    }
    uninit_bt_cache(cache);
}

void print_backtrace(const char *why,any_t** stack,unsigned num) {
    bt_cache_t* cache=init_bt_cache();
    unsigned	i;
    MSG_INFO(why?why:"*** Backtrace for suspect call ***\n");
    for(i=0;i<num;i++) {
	MSG_INFO("    %p -> %s\n",stack[i],addr2line(cache,stack[i]));
    }
    uninit_bt_cache(cache);
}

static void __prot_free_append(any_t*ptr) {
    any_t *page_ptr=prot_page_align(ptr);
    mp_slot_t* slot=prot_find_slot(&priv->mallocs,page_ptr);
    if(!slot) {
	printf("[__prot_free_append] suspect call found! Can't find slot for address: %p [aligned: %p]\n",ptr,page_ptr);
	__prot_print_slots(&priv->mallocs);
	__print_backtrace(10);
	kill(getpid(), SIGILL);
    }
    size_t fullsize=app_fullsize(slot->size);
    mprotect(prot_last_page(page_ptr,fullsize),__VM_PAGE_SIZE__,MP_PROT_READ|MP_PROT_WRITE);
    free(page_ptr);
    prot_free_slot(&priv->mallocs,page_ptr);
}

#define min(a,b) ((a)<(b)?(a):(b))
static any_t* __prot_realloc_append(any_t*ptr,size_t size) {
    any_t* rp;
    if((rp=__prot_malloc_append(size))!=NULL && ptr) {
	mp_slot_t* slot=prot_find_slot(&priv->mallocs,prot_page_align(ptr));
	if(!slot) {
	    printf("[__prot_realloc_append] suspect call found! Can't find slot for address: %p [aligned: %p]\n",ptr,prot_page_align(ptr));
	    __prot_print_slots(&priv->mallocs);
	    __print_backtrace(10);
	    kill(getpid(), SIGILL);
	}
	memcpy(rp,ptr,min(slot->size,size));
	__prot_free_append(ptr);
    }
    return rp;
}

/* ----------- prepend ------------ */
static any_t* pre_page_align(any_t *ptr) { return (any_t*)((unsigned long)ptr-__VM_PAGE_SIZE__); }
static size_t pre_fullsize(size_t size) { return size+__VM_PAGE_SIZE__; }

static any_t* __prot_malloc_prepend(size_t size) {
    any_t* rp;
    size_t fullsize=pre_fullsize(size);
    rp=memalign(__VM_PAGE_SIZE__,fullsize);
    if(rp) {
	prot_append_slot(&priv->mallocs,rp,size);
	// protect last page here
	mprotect(rp,__VM_PAGE_SIZE__,MP_DENY_ALL);
	rp+=__VM_PAGE_SIZE__;
    }
    return rp;
}

static any_t* __prot_memalign_prepend(size_t boundary,size_t size) { UNUSED(boundary);  return __prot_malloc_prepend(size); }

static void __prot_free_prepend(any_t*ptr) {
    any_t *page_ptr=pre_page_align(ptr);
    mp_slot_t* slot=prot_find_slot(&priv->mallocs,page_ptr);
    if(!slot) {
	printf("[__prot_free_prepend] suspect call found! Can't find slot for address: %p [aligned: %p]\n",ptr,page_ptr);
	__prot_print_slots(&priv->mallocs);
	__print_backtrace(10);
	kill(getpid(), SIGILL);
    }
    mprotect(page_ptr,__VM_PAGE_SIZE__,MP_PROT_READ|MP_PROT_WRITE);
    free(page_ptr);
    prot_free_slot(&priv->mallocs,page_ptr);
}

static any_t* __prot_realloc_prepend(any_t*ptr,size_t size) {
    any_t* rp;
    if((rp=__prot_malloc_prepend(size))!=NULL && ptr) {
	mp_slot_t* slot=prot_find_slot(&priv->mallocs,pre_page_align(ptr));
	if(!slot) {
	    printf("[__prot_realloc_prepend] suspect call found! Can't find slot for address: %p [aligned: %p]\n",ptr,pre_page_align(ptr));
	    __prot_print_slots(&priv->mallocs);
	    __print_backtrace(10);
	    kill(getpid(), SIGILL);
	}
	memcpy(rp,ptr,min(slot->size,size));
	__prot_free_prepend(ptr);
    }
    return rp;
}

static any_t* prot_malloc(size_t size) {
    any_t* rp;
    if(priv->flags&MPA_FLG_BOUNDS_CHECK) rp=__prot_malloc_append(size);
    else				 rp=__prot_malloc_prepend(size);
    return rp;
}

static any_t* prot_memalign(size_t boundary,size_t size) {
    any_t* rp;
    if(priv->flags&MPA_FLG_BOUNDS_CHECK) rp=__prot_memalign_append(boundary,size);
    else				 rp=__prot_memalign_prepend(boundary,size);
    return rp;
}

static any_t* prot_realloc(any_t*ptr,size_t size) {
    any_t* rp;
    if(priv->flags&MPA_FLG_BOUNDS_CHECK) rp=__prot_realloc_append(ptr,size);
    else				 rp=__prot_realloc_prepend(ptr,size);
    return rp;
}

static void prot_free(any_t*ptr) {
    if(priv->flags&MPA_FLG_BOUNDS_CHECK) __prot_free_append(ptr);
    else				 __prot_free_prepend(ptr);
}

static __always_inline any_t* bt_malloc(size_t size) {
    any_t*rp;
    mp_slot_t* slot;
    rp=malloc(size);
    if(rp) {
	slot=prot_append_slot(&priv->mallocs,rp,size);
	slot->ncalls=backtrace(slot->calls,10);
    }
    return rp;
}

static __always_inline any_t* bt_memalign(size_t boundary,size_t size) {
    any_t*rp;
    rp=memalign(boundary,size);
    if(rp) {
	mp_slot_t* slot;
	slot=prot_append_slot(&priv->mallocs,rp,size);
	slot->ncalls=backtrace(slot->calls,10);
    }
    return rp;
}

static __always_inline any_t* bt_realloc(any_t*ptr,size_t size) {
    any_t* rp;
    mp_slot_t* slot;
    if(!ptr) return bt_malloc(size);
    rp=realloc(ptr,size);
    if(rp) {
	slot=prot_find_slot(&priv->mallocs,ptr);
	if(!slot) {
	    MSG_WARN("[bt_realloc] suspect call found! Can't find slot for address: %p\n",ptr);
	    mp_slot_t* _slot;
	    _slot=prot_append_slot(&priv->reallocs,ptr,size);
	    _slot->ncalls=backtrace(_slot->calls,10);
	} else {
	    slot->page_ptr=rp; // update address after realloc
	    slot->size=size;
	}
    }
    return rp;
}

static __always_inline void bt_free(any_t*ptr) {
    mp_slot_t* slot=prot_find_slot(&priv->mallocs,ptr);
    if(!slot) {
	MSG_WARN("[bt_free] suspect call found! Can't find slot for address: %p\n",ptr);
	mp_slot_t* _slot;
	_slot=prot_append_slot(&priv->frees,ptr,0);
	_slot->ncalls=backtrace(_slot->calls,10);
	return;
    }
    prot_free_slot(&priv->mallocs,ptr);
    free(ptr);
}

/*======== STATISTICS =======*/

static void bt_print_slots(bt_cache_t* cache,mp_slot_container_t* c) {
    size_t i,j;
    for(i=0;i<c->nslots;i++) {
	char *s;
	int printable=1;
	MSG_INFO("address: %p size: %u dump: ",c->slots[i].page_ptr,c->slots[i].size);
	s=c->slots[i].page_ptr;
	for(j=0;j<min(c->slots[i].size,20);j++) {
	    if(!isprint(s[j])) {
		printable=0;
		break;
	    }
	}
	if(printable) MSG_INFO("%20s",s);
	else for(j=0;j<min(c->slots[i].size,10);j++) {
	    MSG_INFO("%02X ",(unsigned char)s[j]);
	}
	MSG_INFO("\n");
	for(j=0;j<c->slots[i].ncalls;j++) {
	    MSG_INFO("%s%p -> %s\n",j==0?"bt=>":"    ",c->slots[i].calls[j],addr2line(cache,c->slots[i].calls[j]));
	}
    }
}
/* ================== HEAD FUNCTIONS  ======================= */
void	mp_init_malloc(const char *argv0,unsigned rnd_limit,unsigned every_nth_call,enum mp_malloc_e flags)
{
    srand(time(0));
    if(!priv) priv=malloc(sizeof(priv_t));
    memset(priv,0,sizeof(priv_t));
    priv->argv0=argv0;
    priv->rnd_limit=rnd_limit;
    priv->every_nth_call=every_nth_call;
    priv->flags=flags;
}

void	mp_uninit_malloc(int verbose)
{
    int done=0;
    bt_cache_t* cache=init_bt_cache();
    if(priv->flags&MPA_FLG_BACKTRACE) {
	if(priv->mallocs.nslots) {
	    unsigned long total;
	    unsigned i;
	    total=0;
	    for(i=0;i<priv->mallocs.nslots;i++) total+=priv->mallocs.slots[i].size;
	    MSG_WARN("Warning! %lli slots were not freed. Totally %llu bytes was leaked\n",priv->mallocs.nslots,total);
	}
	if(verbose) {
	    if(priv->mallocs.nslots) {
		MSG_INFO("****** List of malloced but not freed pointers *******\n");
		bt_print_slots(cache,&priv->mallocs);
		done=1;
	    }
	    if(priv->reallocs.nslots) {
		MSG_INFO("\n****** List of suspect realloc() calls *******\n");
		bt_print_slots(cache,&priv->reallocs);
		done=1;
	    }
	    if(priv->frees.nslots) {
		MSG_INFO("\n****** List of suspect free() calls *******\n");
		bt_print_slots(cache,&priv->frees);
		done=1;
	    }
	} else {
	    if(priv->reallocs.nslots || priv->frees.nslots)
		MSG_WARN("*** Were found suspect calls of mp_realloc or mp_free  ***\n"
			 "*** Most probably your copy of program contains VIRUSES!!!\n");
	}
    }
    if(done) MSG_HINT("\nFor source lines you may also print in (gdb): list *0xADDRESS\n");
    uninit_bt_cache(cache);
    free(priv);
    priv=NULL;
}

any_t* mp_malloc(size_t __size)
{
    any_t* rb,*rnd_buff=NULL;
    if(!priv) mp_init_malloc(NULL,1000,10,MPA_FLG_RANDOMIZER);
    if(priv->every_nth_call && priv->rnd_limit && !priv->flags) {
	if(priv->total_calls%priv->every_nth_call==0) {
	    rnd_buff=malloc(rand()%priv->rnd_limit);
	}
    }
    if(priv->flags&(MPA_FLG_BOUNDS_CHECK|MPA_FLG_BEFORE_CHECK)) rb=prot_malloc(__size);
    else if(priv->flags&MPA_FLG_BACKTRACE)			rb=bt_malloc(__size);
    else							rb=malloc(__size);
    if(rnd_buff) free(rnd_buff);
    priv->total_calls++;
    return rb;
}

/* randomizing of memalign is useless feature */
any_t*	__FASTCALL__ mp_memalign (size_t boundary, size_t __size)
{
    any_t* rb;
    if(!priv) mp_init_malloc(NULL,1000,10,MPA_FLG_RANDOMIZER);
    if(priv->flags&(MPA_FLG_BOUNDS_CHECK|MPA_FLG_BEFORE_CHECK)) rb=prot_memalign(boundary,__size);
    else if(priv->flags&MPA_FLG_BACKTRACE)			rb=bt_memalign(boundary,__size);
    else							rb=memalign(boundary,__size);
    return rb;
}

any_t*	mp_realloc(any_t*__ptr, size_t __size) {
    any_t* rp;
    if(priv->flags&(MPA_FLG_BOUNDS_CHECK|MPA_FLG_BEFORE_CHECK)) rp=prot_realloc(__ptr,__size);
    else if(priv->flags&MPA_FLG_BACKTRACE)			rp=bt_realloc(__ptr,__size);
    else							rp=realloc(__ptr,__size);
    return rp;
}

void	mp_free(any_t*__ptr)
{
    if(!priv) mp_init_malloc(NULL,1000,10,MPA_FLG_RANDOMIZER);
    if(__ptr) {
	if(priv->flags&(MPA_FLG_BOUNDS_CHECK|MPA_FLG_BEFORE_CHECK))
	    prot_free(__ptr);
	else if(priv->flags&MPA_FLG_BACKTRACE)
	    bt_free(__ptr);
	else
	    free(__ptr);
    }
}

/* ================ APPENDIX ==================== */

any_t*	mp_mallocz (size_t __size) {
    any_t* rp;
    rp=mp_malloc(__size);
    if(rp) memset(rp,0,__size);
    return rp;
}

char *	mp_strdup(const char *src) {
    char *rs=NULL;
    if(src) {
	unsigned len=strlen(src);
	rs=mp_malloc(len+1);
	if(rs) strcpy(rs,src);
    }
    return rs;
}

int __FASTCALL__ mp_mprotect(const any_t* addr,size_t len,enum mp_prot_e flags)
{
    return mprotect((any_t*)addr,len,flags);
}
