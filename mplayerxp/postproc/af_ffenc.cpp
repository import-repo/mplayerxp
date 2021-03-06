#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "mpxp_conf_lavc.h"
#include "libmpcodecs/codecs_ld.h"
#include "osdep/fastmemcpy.h"
#include "af.h"
#include "af_internal.h"
#include "pp_msg.h"

#define MIN_LIBAVCODEC_VERSION_INT	((51<<16)+(0<<8)+0)

// Data for specific instances of this filter
struct af_ffenc_t
{
    char cname[256];
    unsigned brate;
    AVCodec *lavc_codec;
    AVCodecContext *lavc_context;
    int acodec_inited;
    unsigned frame_size;
    uint8_t *tail;
    unsigned tail_size;
};

static void print_encoders(void)
{
#if 0
    AVCodec *p;
    p = first_avcodec;
    while (p) {
	if (p->encode != NULL && p->type == CODEC_TYPE_AUDIO)
	    mpxp_info<<p->name<<" ";
	p = p->next;
    }
    mpxp_info<<<<std::endl;
#endif
    mpxp_info<<"Not ready yet!!!"<<std::endl;
}

static uint32_t find_atag(const char *codec)
{
	if(codec == NULL)
		return 0;

	if(! strcasecmp(codec, "adpcm_ima_wav"))
		return 0x11;

	if(! strcasecmp(codec, "g726"))
		return 0x45;

	if(! strcasecmp(codec, "mp2"))
		return 0x50;

	if(! strcasecmp(codec, "mp3"))
		return 0x55;

	if(! strcasecmp(codec, "ac3"))
		return 0x2000;

	if(! strcasecmp(codec, "dts"))
		return 0x2001;

	if(! strcasecmp(codec, "sonic"))
		return 0x2048;

	if(! strncasecmp(codec, "bonk", 4))
		return 0x2048;

	if(! strcasecmp(codec, "aac"))
		return 0x706D;

	if(! strcasecmp(codec, "vorbis")) // FIXME ???
		return 0xFFFE;

	return 0;
}

static MPXP_Rc __FASTCALL__ config_af(af_instance_t* af,const af_conf_t* arg)
{
    af_ffenc_t *s=reinterpret_cast<af_ffenc_t*>(af->setup);
    if(!s->acodec_inited) {
	avcodec_register_all();
	s->acodec_inited=1;
    }
    if(strcmp(s->cname,"help")==0) {
	print_encoders();
	return MPXP_Error;
    }
    if(!(s->lavc_codec=avcodec_find_encoder_by_name(s->cname))) {
	mpxp_err<<"Can't find encoder "<<s->cname<<" in libavcodec"<<std::endl;
	return MPXP_Error;
    }
    s->lavc_context=avcodec_alloc_context3(s->lavc_codec);
    /* put sample parameters */
    s->lavc_context->bit_rate = s->brate;
    s->lavc_context->sample_rate = arg->rate;
    s->lavc_context->channels = arg->nch;
    s->lavc_context->sample_fmt = AV_SAMPLE_FMT_S16;
    /* af_open it */
    if (avcodec_open(s->lavc_context, s->lavc_codec) < 0) {
	mpxp_err<<"could not af_open codec "<<s->cname<<" with libavcodec"<<std::endl;
	return MPXP_Error;
    }
    s->frame_size = s->lavc_context->frame_size*arg->nch*2/*bps*/;
    s->tail=new uint8_t [s->frame_size];
    /* correct in format */
    af->conf.rate   = arg->rate;
    af->conf.nch    = arg->nch;
    af->conf.format = afmt2mpaf(find_atag(s->cname)<<16);
    mpxp_v<<"[af_ffenc] Was reinitialized, rate="<<af->conf.rate<<"Hz, nch = "<<af->conf.nch<<", format = 0x"<<std::hex<<af->conf.format<<std::endl;
    return MPXP_Ok;
}
// Initialization and runtime control_af
static MPXP_Rc __FASTCALL__ control_af(af_instance_t* af, int cmd, any_t* arg)
{
  af_ffenc_t *s=reinterpret_cast<af_ffenc_t*>(af->setup);
  switch(cmd){
  case AF_CONTROL_SHOWCONF:
    mpxp_info<<"[af_ffenc] in use ["<<s->cname<<" "<<s->brate<<"]"<<std::endl;
    return MPXP_Ok;
  case AF_CONTROL_COMMAND_LINE:{
    char *comma;
    strcpy(s->cname,"mp3");
    s->brate=128000;
    if(arg)
    {
	sscanf((char*)arg,"%s", s->cname);
	comma=strchr(s->cname,':');
	if(comma) {
	    *comma='\0';
	    s->brate=atoi(++comma);
	}
    }
    return MPXP_Ok;
  }
  }
  return MPXP_Unknown;
}

// Deallocate memory
static void __FASTCALL__ uninit(af_instance_t* af)
{
    af_ffenc_t *s=reinterpret_cast<af_ffenc_t*>(af->setup);
    avcodec_close(s->lavc_context);
    if(s->lavc_context) delete s->lavc_context;
    delete s->tail;
    if(af->setup) delete af->setup;
}

// Filter data through filter
static mp_aframe_t __FASTCALL__ play(af_instance_t* af,const mp_aframe_t& in)
{
    unsigned tlen,ilen,olen,delta;
    af_ffenc_t *s=reinterpret_cast<af_ffenc_t*>(af->setup);
    uint8_t *inp,*outp;
    mp_aframe_t out = in.genome();
    out.alloc();

    ilen=tlen=in.len;
    if(out.audio) {
	out.len=0;
	inp=reinterpret_cast<uint8_t*>(in.audio);
	outp=reinterpret_cast<uint8_t*>(out.audio);
	if(s->tail_size && s->tail_size+ilen>=s->frame_size) {
	    delta=s->frame_size-s->tail_size;
	    memcpy(&s->tail[s->tail_size],inp,delta);
	    ilen-=delta;
	    olen = avcodec_encode_audio(s->lavc_context, outp, tlen, (const short *)s->tail);
	    mpxp_dbg3<<"encoding tail "<<s->tail_size<<" bytes + "<<delta<<" stream => "<<olen<<" compressed"<<std::endl;
	    inp+=delta;
	    out.len += olen;
	    outp+=olen;
	    tlen-=olen;
	    s->tail_size=0;
	}
	while(ilen>=s->frame_size) {
	    olen = avcodec_encode_audio(s->lavc_context, outp, tlen, (const short *)inp);
	    mpxp_dbg2<<"encoding [out "<<tlen<<" in "<<ilen<<"]=>"<<olen<<" compressed"<<std::endl;
	    out.len += olen;
	    inp+=s->frame_size;
	    ilen-=s->frame_size;
	    tlen-=olen;
	    outp+=olen;
	}
	delta=ilen;
	if(delta) {
	    mpxp_dbg2<<"encoding append tail "<<delta<<" bytes to "<<s->tail_size<<" existed"<<std::endl;
	    memcpy(&s->tail[s->tail_size],inp,delta);
	    s->tail_size+=delta;
	}
    }
    return out;
}

// Allocate memory and set function pointers
static MPXP_Rc __FASTCALL__ af_open(af_instance_t* af){
  af->config_af=config_af;
  af->control_af=control_af;
  af->uninit=uninit;
  af->play=play;
  af->mul.d=1;
  af->mul.n=1;
  af->setup=new(zeromem) af_ffenc_t;

    check_pin("afilter",af->pin,AF_PIN);
  return MPXP_Ok;
}

// Description of this filter
extern const af_info_t af_info_ffenc = {
    "Encode audio with using of libavcodec",
    "ffenc",
    "Nickols_K",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
