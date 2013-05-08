#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */
#include "ad_internal.h"
#include "mplayerxp.h"
#include "osdep/cpudetect.h"
#include "osdep/mm_accel.h"
#include "osdep/fastmemcpy.h"
#include "osdep/bswap.h"
#include "codecs_ld.h"
#include "libao3/afmt.h"
#include "libao3/audio_out.h"
#include "postproc/af.h"

static const ad_info_t info = {
    "MPEG layer-123",
    "mp3lib",
    "Nickols_K",
    "http://www.mpg123.de"
};

static const mpxp_option_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBAD_EXTERN(mp3)

/** Opaque structure for the libmpg123 decoder handle. */
struct mpg123_handle_struct;
typedef struct mpg123_handle_struct mpg123_handle;

/** Enumeration of the mode types of Variable Bitrate */
enum mpg123_vbr {
    MPG123_CBR=0,	/**< Constant Bitrate Mode (default) */
    MPG123_VBR,		/**< Variable Bitrate Mode */
    MPG123_ABR		/**< Average Bitrate Mode */
};

/** Enumeration of the MPEG Versions */
enum mpg123_version {
    MPG123_1_0=0,	/**< MPEG Version 1.0 */
    MPG123_2_0,		/**< MPEG Version 2.0 */
    MPG123_2_5		/**< MPEG Version 2.5 */
};


/** Enumeration of the MPEG Audio mode.
 *  Only the mono mode has 1 channel, the others have 2 channels. */
enum mpg123_mode {
    MPG123_M_STEREO=0,	/**< Standard Stereo. */
    MPG123_M_JOINT,	/**< Joint Stereo. */
    MPG123_M_DUAL,	/**< Dual Channel. */
    MPG123_M_MONO	/**< Single Channel. */
};

/** Enumeration of the MPEG Audio flag bits */
enum mpg123_flags {
    MPG123_CRC=0x1,	/**< The bitstream is error protected using 16-bit CRC. */
    MPG123_COPYRIGHT=0x2,/**< The bitstream is copyrighted. */
    MPG123_PRIVATE=0x4,	/**< The private bit has been set. */
    MPG123_ORIGINAL=0x8	/**< The bitstream is an original, not a copy. */
};

/** Data structure for storing information about a frame of MPEG Audio */
struct mpg123_frameinfo {
    enum mpg123_version version;/**< The MPEG version (1.0/2.0/2.5). */
    int layer;			/**< The MPEG Audio Layer (MP1/MP2/MP3). */
    long rate; 			/**< The sampling rate in Hz. */
    enum mpg123_mode mode;	/**< The audio mode (Mono, Stereo, Joint-stero, Dual Channel). */
    int mode_ext;		/**< The mode extension bit flag. */
    int framesize;		/**< The size of the frame (in bytes). */
    enum mpg123_flags flags;	/**< MPEG Audio flag bits. */
    int emphasis;		/**< The emphasis type. */
    int bitrate;		/**< Bitrate of the frame (kbps). */
    int abr_rate;		/**< The target average bitrate. */
    enum mpg123_vbr vbr;	/**< The VBR mode. */
};


/** Enumeration of the parameters types that it is possible to set/get. */
enum mpg123_parms {
    MPG123_VERBOSE,	/**< set verbosity value for enabling messages to stderr, >= 0 makes sense (integer) */
    MPG123_FLAGS,	/**< set all flags, p.ex val = MPG123_GAPLESS|MPG123_MONO_MIX (integer) */
    MPG123_ADD_FLAGS,	/**< add some flags (integer) */
    MPG123_FORCE_RATE,	/**< when value > 0, force output rate to that value (integer) */
    MPG123_DOWN_SAMPLE,	/**< 0=native rate, 1=half rate, 2=quarter rate (integer) */
    MPG123_RVA,		/**< one of the RVA choices above (integer) */
    MPG123_DOWNSPEED,	/**< play a frame N times (integer) */
    MPG123_UPSPEED,	/**< play every Nth frame (integer) */
    MPG123_START_FRAME,	/**< start with this frame (skip frames before that, integer) */
    MPG123_DECODE_FRAMES,/**< decode only this number of frames (integer) */
    MPG123_ICY_INTERVAL,/**< stream contains ICY metadata with this interval (integer) */
    MPG123_OUTSCALE,	/**< the scale for output samples (amplitude - integer or float according to mpg123 output format, normally integer) */
    MPG123_TIMEOUT,	/**< timeout for reading from a stream (not supported on win32, integer) */
    MPG123_REMOVE_FLAGS,/**< remove some flags (inverse of MPG123_ADD_FLAGS, integer) */
    MPG123_RESYNC_LIMIT,/**< Try resync on frame parsing for that many bytes or until end of stream (<0 ... integer). */
    MPG123_INDEX_SIZE,	/**< Set the frame index size (if supported). Values <0 mean that the index is allowed to grow dynamically in these steps (in positive direction, of course) -- Use this when you really want a full index with every individual frame. */
    MPG123_PREFRAMES	/**< Decode/ignore that many frames in advance for layer 3. This is needed to fill bit reservoir after seeking, for example (but also at least one frame in advance is needed to have all "normal" data for layer 3). Give a positive integer value, please.*/
};

/** Flag bits for MPG123_FLAGS, use the usual binary or to combine. */
enum mpg123_param_flags {
    MPG123_FORCE_MONO   =0x7,  /**<     0111 Force some mono mode: This is a test bitmask for seeing if any mono forcing is active. */
    MPG123_MONO_LEFT    =0x1,  /**<     0001 Force playback of left channel only.  */
    MPG123_MONO_RIGHT   =0x2,  /**<     0010 Force playback of right channel only. */
    MPG123_MONO_MIX     =0x4,  /**<     0100 Force playback of mixed mono.         */
    MPG123_FORCE_STEREO =0x8,  /**<     1000 Force stereo output.                  */
    MPG123_FORCE_8BIT   =0x10, /**< 00010000 Force 8bit formats.                   */
    MPG123_QUIET        =0x20, /**< 00100000 Suppress any printouts (overrules verbose).                    */
    MPG123_GAPLESS      =0x40, /**< 01000000 Enable gapless decoding (default on if libmpg123 has support). */
    MPG123_NO_RESYNC    =0x80, /**< 10000000 Disable resync stream after error.                             */
    MPG123_SEEKBUFFER   =0x100,/**< 000100000000 Enable small buffer on non-seekable streams to allow some peek-ahead (for better MPEG sync). */
    MPG123_FUZZY        =0x200,/**< 001000000000 Enable fuzzy seeks (guessing byte offsets or using approximate seek points from Xing TOC) */
    MPG123_FORCE_FLOAT  =0x400,/**< 010000000000 Force floating point output (32 or 64 bits depends on mpg123 internal precision). */
    MPG123_PLAIN_ID3TEXT=0x800 /**< 100000000000 Do not translate ID3 text data to UTF-8. ID3 strings will contain the raw text data, with the first byte containing the ID3 encoding code. */
};

/** choices for MPG123_RVA */
enum mpg123_param_rva {
    MPG123_RVA_OFF   =0, /**< RVA disabled (default).   */
    MPG123_RVA_MIX   =1, /**< Use mix/track/radio gain. */
    MPG123_RVA_ALBUM =2, /**< Use album/audiophile gain */
    MPG123_RVA_MAX   =MPG123_RVA_ALBUM /**< The maximum RVA code, may increase in future. */
};

/** Enumeration of the message and error codes and returned by libmpg123 functions. */
enum mpg123_errors {
    MPG123_DONE=-12,		/**< Message: Track ended. Stop decoding. */
    MPG123_NEW_FORMAT=-11,	/**< Message: Output format will be different on next call. Note that some libmpg123 versions between 1.4.3 and 1.8.0 insist on you calling mpg123_getformat() after getting this message code. Newer verisons behave like advertised: You have the chance to call mpg123_getformat(), but you can also just continue decoding and get your data. */
    MPG123_NEED_MORE=-10,	/**< Message: For feed reader: "Feed me more!" (call mpg123_feed() or mpg123_decode() with some new input data). */
    MPG123_ERR=-1,		/**< Generic Error */
    MPG123_OK=0,		/**< Success */
    MPG123_BAD_OUTFORMAT,	/**< Unable to set up output format! */
    MPG123_BAD_CHANNEL,		/**< Invalid channel number specified. */
    MPG123_BAD_RATE,		/**< Invalid sample rate specified.  */
    MPG123_ERR_16TO8TABLE,	/**< Unable to allocate memory for 16 to 8 converter table! */
    MPG123_BAD_PARAM,		/**< Bad parameter id! */
    MPG123_BAD_BUFFER,		/**< Bad buffer given -- invalid pointer or too small size. */
    MPG123_OUT_OF_MEM,		/**< Out of memory -- some mp_malloc() failed. */
    MPG123_NOT_INITIALIZED,	/**< You didn't initialize the library! */
    MPG123_BAD_DECODER,		/**< Invalid decoder choice. */
    MPG123_BAD_HANDLE,		/**< Invalid mpg123 handle. */
    MPG123_NO_BUFFERS,		/**< Unable to initialize frame buffers (out of memory?). */
    MPG123_BAD_RVA,		/**< Invalid RVA mode. */
    MPG123_NO_GAPLESS,		/**< This build doesn't support gapless decoding. */
    MPG123_NO_SPACE,		/**< Not enough buffer space. */
    MPG123_BAD_TYPES,		/**< Incompatible numeric data types. */
    MPG123_BAD_BAND,		/**< Bad equalizer band. */
    MPG123_ERR_NULL,		/**< Null pointer given where valid storage address needed. */
    MPG123_ERR_READER,		/**< Error reading the stream. */
    MPG123_NO_SEEK_FROM_END,	/**< Cannot seek from end (end is not known). */
    MPG123_BAD_WHENCE,		/**< Invalid 'whence' for seek function.*/
    MPG123_NO_TIMEOUT,		/**< Build does not support stream timeouts. */
    MPG123_BAD_FILE,		/**< File access error. */
    MPG123_NO_SEEK,		/**< Seek not supported by stream. */
    MPG123_NO_READER,		/**< No stream opened. */
    MPG123_BAD_PARS,		/**< Bad parameter handle. */
    MPG123_BAD_INDEX_PAR,	/**< Bad parameters to mpg123_index() */
    MPG123_OUT_OF_SYNC,		/**< Lost track in bytestream and did not try to resync. */
    MPG123_RESYNC_FAIL,		/**< Resync failed to find valid MPEG data. */
    MPG123_NO_8BIT,		/**< No 8bit encoding possible. */
    MPG123_BAD_ALIGN,		/**< Stack aligmnent error */
    MPG123_NULL_BUFFER,		/**< NULL input buffer with non-zero size... */
    MPG123_NO_RELSEEK,		/**< Relative seek not possible (screwed up file offset) */
    MPG123_NULL_POINTER,	/**< You gave a null pointer somewhere where you shouldn't have. */
    MPG123_BAD_KEY,		/**< Bad key value given. */
    MPG123_NO_INDEX,		/**< No frame index in this build. */
    MPG123_INDEX_FAIL,		/**< Something with frame index went wrong. */
    MPG123_BAD_DECODER_SETUP,	/**< Something prevents a proper decoder setup */
    MPG123_MISSING_FEATURE,	/**< This feature has not been built into libmpg123. */
    MPG123_BAD_VALUE,		/**< A bad value has been given, somewhere. */
    MPG123_LSEEK_FAILED		/**< Low-level seek failed. */
};


struct mp3_private_t : public Opaque {
    mp3_private_t();
    virtual ~mp3_private_t();

    mpg123_handle *mh;
    off_t pos;
    float pts;
    sh_audio_t* sh;
    any_t* dll_handle;
    void (*mpg123_init_ptr)(void);
    void (*mpg123_exit_ptr)(void);
    mpg123_handle * (*mpg123_new_ptr)(const char* decoder, int *error);
    void (*mpg123_delete_ptr)(mpg123_handle *mh);

    const char* (*mpg123_plain_strerror_ptr)(int errcode);
    int (*mpg123_open_feed_ptr)(mpg123_handle *mh);
    int (*mpg123_close_ptr)(mpg123_handle *mh);
    int (*mpg123_read_ptr)(mpg123_handle *mh, unsigned char *outmemory, size_t outmemsize, size_t *done);
    int (*mpg123_feed_ptr)(mpg123_handle *mh, const unsigned char *in, size_t size);
    int (*mpg123_decode_ptr)(mpg123_handle *mh, const unsigned char *inmemory, size_t inmemsize,
				unsigned char *outmemory, size_t outmemsize, size_t *done);
    int (*mpg123_getformat_ptr)(mpg123_handle *mh, long *rate, int *channels, int *encoding);
    int (*mpg123_param_ptr)(mpg123_handle *mh, enum mpg123_parms type, long value, double fvalue);
    int (*mpg123_info_ptr)(mpg123_handle *mh, struct mpg123_frameinfo *mi);
    const char* (*mpg123_current_decoder_ptr)(mpg123_handle *mh);
    int (*mpg123_decode_frame_ptr)(mpg123_handle *mh, off_t *num, unsigned char **audio, size_t *bytes);
    off_t (*mpg123_tell_stream_ptr)(mpg123_handle *mh);
};
mp3_private_t::mp3_private_t() {}
mp3_private_t::~mp3_private_t() {
    if(mh) {
	(*mpg123_close_ptr)(mh);
	(*mpg123_delete_ptr)(mh);
	(*mpg123_exit_ptr)();
    }
    if(dll_handle) ::dlclose(dll_handle);
}

static MPXP_Rc load_dll(mp3_private_t& priv,const char *libname) {
    if(!(priv.dll_handle=ld_codec(libname,mpcodecs_ad_mp3.info->url))) return MPXP_False;
    priv.mpg123_init_ptr = (void (*)())ld_aliased_sym(priv.dll_handle,"mpg123_init","mpg123_init_64");
    priv.mpg123_exit_ptr = (void (*)())ld_aliased_sym(priv.dll_handle,"mpg123_exit","mpg123_exit_64");
    priv.mpg123_new_ptr = (mpg123_handle* (*)(const char*,int*))ld_aliased_sym(priv.dll_handle,"mpg123_new","mpg123_new_64");
    priv.mpg123_delete_ptr = (void (*)(mpg123_handle*))ld_aliased_sym(priv.dll_handle,"mpg123_delete","mpg123_delete_64");
    priv.mpg123_plain_strerror_ptr = (const char* (*)(int))ld_aliased_sym(priv.dll_handle,"mpg123_plain_strerror","mpg123_plain_strerror_64");
    priv.mpg123_open_feed_ptr = (int (*)(mpg123_handle*))ld_aliased_sym(priv.dll_handle,"mpg123_open_feed","mpg123_open_feed_64");
    priv.mpg123_close_ptr = (int (*)(mpg123_handle*))ld_aliased_sym(priv.dll_handle,"mpg123_close","mpg123_close_64");
    priv.mpg123_getformat_ptr = (int (*)(mpg123_handle*,long*,int*,int*))ld_aliased_sym(priv.dll_handle,"mpg123_getformat","mpg123_getformat_64");
    priv.mpg123_param_ptr = (int (*)(mpg123_handle*,mpg123_parms,long,double))ld_aliased_sym(priv.dll_handle,"mpg123_param","mpg123_param_64");
    priv.mpg123_info_ptr = (int (*)(mpg123_handle*,mpg123_frameinfo*))ld_aliased_sym(priv.dll_handle,"mpg123_info","mpg123_info_64");
    priv.mpg123_current_decoder_ptr = (const char* (*)(mpg123_handle*))ld_aliased_sym(priv.dll_handle,"mpg123_current_decoder","mpg123_current_decoder_64");
    priv.mpg123_decode_ptr = (int (*)(mpg123_handle*,const unsigned char*,size_t,unsigned char*,size_t,size_t*))ld_aliased_sym(priv.dll_handle,"mpg123_decode","mpg123_decode_64");
    priv.mpg123_read_ptr = (int (*)(mpg123_handle*,unsigned char*,size_t,size_t*))ld_aliased_sym(priv.dll_handle,"mpg123_read","mpg123_read_64");
    priv.mpg123_feed_ptr = (int (*)(mpg123_handle*,const unsigned char*,size_t))ld_aliased_sym(priv.dll_handle,"mpg123_feed","mpg123_feed_64");
    priv.mpg123_tell_stream_ptr = (off_t (*)(mpg123_handle*))ld_aliased_sym(priv.dll_handle,"mpg123_tell_stream","mpg123_tell_stream_64");
    priv.mpg123_decode_frame_ptr = (int (*)(mpg123_handle*,off_t*,unsigned char**,size_t*))ld_aliased_sym(priv.dll_handle,"mpg123_decode_frame","mpg123_decode_frame_64");
    return (priv.mpg123_decode_ptr && priv.mpg123_init_ptr && priv.mpg123_exit_ptr &&
	priv.mpg123_new_ptr && priv.mpg123_delete_ptr && priv.mpg123_plain_strerror_ptr &&
	priv.mpg123_open_feed_ptr && priv.mpg123_close_ptr && priv.mpg123_getformat_ptr &&
	priv.mpg123_param_ptr && priv.mpg123_info_ptr && priv.mpg123_current_decoder_ptr &&
	priv.mpg123_read_ptr && priv.mpg123_feed_ptr && priv.mpg123_decode_frame_ptr && priv.mpg123_tell_stream_ptr)?
	MPXP_Ok:MPXP_False;
}

static const audio_probe_t probes[] = {
    { "mp3lib", "libmpg123"SLIBSUFFIX, 0x50, ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, 0x55, ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, 0x55005354, ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, 0x5000736D, ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, 0x5500736D, ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, FOURCC_TAG('.','M','P','3'), ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, FOURCC_TAG('L','A','M','E'), ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { "mp3lib", "libmpg123"SLIBSUFFIX, FOURCC_TAG('M','P','3',' '), ACodecStatus_Working, {AFMT_FLOAT32, AFMT_S16_LE} },
    { NULL, NULL, 0x0, ACodecStatus_NotWorking, {AFMT_S8}}
};

static const audio_probe_t* __FASTCALL__ probe(uint32_t wtag) {
    unsigned i;
    for(i=0;probes[i].driver;i++)
	if(wtag==probes[i].wtag)
	    return &probes[i];
    return NULL;
}

Opaque* preinit(const audio_probe_t& probe,sh_audio_t *sh,audio_filter_info_t& afi)
{
    UNUSED(afi);
    sh->audio_out_minsize=9216;
    mp3_private_t* priv = new(zeromem) mp3_private_t;
    if(load_dll(*priv,probe.codec_dll)!=MPXP_Ok) { delete priv; return NULL; }
    priv->sh = sh;
    return priv;
}

MPXP_Rc init(Opaque& ctx)
{
    mp3_private_t& priv=static_cast<mp3_private_t&>(ctx);
    // MPEG Audio:
    float pts;
    long param,rate;
    size_t indata_size,done,len;
    int err=0,nch,enc;
    unsigned char *indata;
    struct mpg123_frameinfo fi;
    sh_audio_t* sh = priv.sh;
    sh->afmt=AFMT_FLOAT32;
    (*priv.mpg123_init_ptr)();
    priv.mh = (*priv.mpg123_new_ptr)(NULL,&err);
    if(err) {
	err_exit:
	MSG_ERR("mpg123_init: %s\n",(*priv.mpg123_plain_strerror_ptr)(err));
	if(priv.mh) (*priv.mpg123_delete_ptr)(priv.mh);
	(*priv.mpg123_exit_ptr)();
	return MPXP_False;
    }
    if((err=(*priv.mpg123_open_feed_ptr)(priv.mh))!=0) goto err_exit;
    param = MPG123_FORCE_STEREO|MPG123_FORCE_FLOAT;
    if(!mp_conf.verbose) param|=MPG123_QUIET;
    (*priv.mpg123_param_ptr)(priv.mh,MPG123_FLAGS,param,0);
    // Decode first frame (to get header filled)
    err=MPG123_NEED_MORE;
    len=0;
    while(err==MPG123_NEED_MORE) {
	indata_size=ds_get_packet_r(*sh->ds,&indata,pts);
	(*priv.mpg123_feed_ptr)(priv.mh,indata,indata_size);
	err=(*priv.mpg123_read_ptr)(priv.mh,reinterpret_cast<unsigned char*>(sh->a_buffer),sh->a_buffer_size,&done);
	len+=done;
    }
    sh->a_buffer_len=len;
    if(err!=MPG123_NEW_FORMAT) {
	MSG_ERR("mpg123_init: within [%d] can't retrieve stream property: %s\n",indata_size,(*priv.mpg123_plain_strerror_ptr)(err));
	(*priv.mpg123_close_ptr)(priv.mh);
	(*priv.mpg123_delete_ptr)(priv.mh);
	(*priv.mpg123_exit_ptr)();
	return MPXP_False;
    }
    (*priv.mpg123_getformat_ptr)(priv.mh, &rate, &nch, &enc);
    sh->rate = rate;
    sh->nch = nch;
    (*priv.mpg123_info_ptr)(priv.mh,&fi);
    sh->i_bps=((fi.abr_rate?fi.abr_rate:fi.bitrate)/8)*1000;
    // Prints first frame header in ascii.
    static const char *modes[4] = { "Stereo", "Joint-Stereo", "Dual-Channel", "Mono" };
    static const char *layers[4] = { "???" , "I", "II", "III" };
    static const char *vers[4] = { "1.0", "2.0", "2.5", "???" };
    static const char *xbr[4]  = { "CBR", "VBR", "ABR", "???" };

    MSG_INFO("\rmpg123_init: MPEG-%s [Layer:%s (%s)], Hz=%d %d-kbit %s, BPF=%d Out=32-bit\n"
	,vers[fi.version&0x3]
	,layers[fi.layer&0x3]
	,xbr[fi.vbr&0x3]
	,fi.rate
	,(sh->i_bps*8)/1000
	,modes[fi.mode&0x3]
	,fi.framesize);
    MSG_INFO("mpg123_init: Copyrght=%s Orig=%s CRC=%s Priv=%s Emphas=%d Optimiz=%s\n"
	,fi.flags&MPG123_COPYRIGHT?"Yes":"No"
	,fi.flags&MPG123_ORIGINAL?"Yes":"No"
	,fi.flags&MPG123_CRC?"Yes":"No"
	,fi.flags&MPG123_PRIVATE?"Yes":"No"
	,fi.emphasis,(*priv.mpg123_current_decoder_ptr)(priv.mh));
    return MPXP_Ok;
}

void uninit(Opaque& ctx) { UNUSED(ctx); }

MPXP_Rc control_ad(Opaque& ctx,int cmd,any_t* arg, ...)
{
    UNUSED(ctx);
    UNUSED(cmd);
    UNUSED(arg);
    return MPXP_Unknown;
}

unsigned decode(Opaque& ctx,unsigned char *buf,unsigned minlen,unsigned maxlen,float& pts)
{
    mp3_private_t& priv=static_cast<mp3_private_t&>(ctx);
    sh_audio_t* sh = priv.sh;
    unsigned char *indata=NULL,*outdata=NULL;
    int err=MPG123_OK,indata_size=0;
    off_t offset,cpos;
    size_t done=0;
    minlen=1;
    /* decode one frame per call to be compatible with old logic:
	***************************
	int retval=-1;
	while(retval<0) {
	    retval = MP3_DecodeFrame(buf,-1,pts);
	    if(retval<0) control_ad(sh_audio,ADCTRL_RESYNC_STREAM,NULL);
	}
	return retval;
	***************************
	this logic violate some rules buf buffer overflow never happens because maxlen in 10 times less
	than total buffer size!
	TODO: Try to switch on mpg123_framebyframe_decode() after stabilizing its interface by libmpg123 developers.
     */
    MSG_DBG2("mp3_decode start: pts=%f\n",pts);
    do {
	cpos = (*priv.mpg123_tell_stream_ptr)(priv.mh);
	pts = priv.pts+((float)(cpos-priv.pos)/sh->i_bps);
	err=(*priv.mpg123_decode_frame_ptr)(priv.mh,&offset,&outdata,&done);
	if(!((err==MPG123_OK)||(err==MPG123_NEED_MORE))) {
	    MSG_ERR("mpg123_read = %s done = %u minlen = %u\n",(*priv.mpg123_plain_strerror_ptr)(err),done,minlen);
	}
	if(err==MPG123_OK) {
	    MSG_DBG2("ad_mp3.decode: copy %u bytes from %p\n",done,outdata);
	    memcpy(buf,outdata,done);
	}
	else if(err==MPG123_NEED_MORE) {
	    float apts=0.;
	    indata_size=ds_get_packet_r(*sh->ds,&indata,apts);
	    if(indata_size<0) return 0;
	    priv.pos = (*priv.mpg123_tell_stream_ptr)(priv.mh);
	    priv.pts = apts;
	    (*priv.mpg123_feed_ptr)(priv.mh,indata,indata_size);
	}
    }while(err==MPG123_NEED_MORE);
    MSG_DBG2("mp3_decode: %i->%i [%i...%i] pts=%f\n",indata_size,done,minlen,maxlen,pts);
    return done;
}
