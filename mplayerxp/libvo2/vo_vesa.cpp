#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
 *  video_out_vesa.c
 *
 *	Copyright (C) Nickols_K <nickols_k@mail.ru> - Oct 2001
 *
 *  You can redistribute this file under terms and conditions
 *  of GNU General Public licence v2.
 *  This file is partly based on vbetest.c from lrmi distributive.
 */

/*
  TODO:
  - hw YUV support (need volunteers who have corresponding hardware)
  - triple buffering (if it will really speedup playback).
    note: triple buffering requires VBE 3.0 - need volunteers.
  - refresh rate support (need additional info from mplayer)
*/
#include <algorithm>
#include <iomanip>
#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <limits.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "mplayerxp.h"
#include "video_out.h"
#include "video_out_internal.h"

#include "osdep/fastmemcpy.h"
#include "sub.h"
#include "osdep/vbelib.h"
#include "osdep/bswap.h"
#include "aspect.h"
#ifdef CONFIG_VIDIX
#include "vidix_system.h"
#endif
#include "dri_vo.h"
#include "mpxp_help.h"
#include "vo_msg.h"

namespace	usr {
/* driver data */
struct win_frame
{
  uint8_t   *ptr;   /* pointer to window's frame memory */
  uint32_t   low;   /* lowest boundary of frame */
  uint32_t   high;  /* highest boundary of frame */
  char       idx;   /* indicates index of relocatable frame (A=0 or B=1)
			special case for DGA: idx=-1
			idx=-2 indicates invalid frame, exists only in init() */
};

class VESA_VO_Interface : public VO_Interface {
    public:
	VESA_VO_Interface(const std::string& args);
	virtual ~VESA_VO_Interface();

	virtual MPXP_Rc	configure(uint32_t width,
				uint32_t height,
				uint32_t d_width,
				uint32_t d_height,
				unsigned flags,
				const std::string& title,
				uint32_t format);
	virtual MPXP_Rc	select_frame(unsigned idx);
	virtual MPXP_Rc	flush_page(unsigned idx);
	virtual void	get_surface_caps(dri_surface_cap_t *caps) const;
	virtual void	get_surface(dri_surface_t *surf);
	virtual MPXP_Rc	query_format(vo_query_fourcc_t* format) const;
	virtual unsigned get_num_frames() const;

	virtual uint32_t check_events(const vo_resize_t*);
	virtual MPXP_Rc	ctrl(uint32_t request, any_t*data);
    private:
	std::string	parse_sub_device(const std::string& sd);
	int		has_dga() const { return win.idx == -1; }
	int		valid_win_frame(unsigned long offset) const { return offset >= win.low && offset < win.high; }
	any_t*		video_ptr(unsigned long offset) const { return win.ptr + offset - win.low; }
	unsigned	pixel_size() const { return (dstBpp+7)/8; }
	unsigned	screen_line_size(unsigned _pixel_size) const { return vmode_info.XResolution*_pixel_size; }
	unsigned	image_line_size(unsigned _pixel_size) const { return dstW*_pixel_size; }

	void		__vbeSwitchBank(unsigned long offset);
	void		__vbeSetPixel(int x, int y, int r, int g, int b);
	void		__vbeCopyBlockFast(unsigned long offset,uint8_t *image,unsigned long size);
	void		__vbeCopyBlock(unsigned long offset,uint8_t *image,unsigned long size);
	void		__vbeCopyData(uint8_t *image);
	void		paintBkGnd();
	void		clear_screen();
	void		clear_screen_fast();
	unsigned	fillMultiBuffer(unsigned long vsize, unsigned nbuffs);

	void		vesa_term();


	void		(VESA_VO_Interface::*cpy_blk_fnc)(unsigned long,uint8_t *,unsigned long);

	LocalPtr<Aspect>aspect;
	uint32_t	srcW,srcH,srcBpp,srcFourcc; /* source image description */
	uint32_t	dstBpp,dstW,dstH,dstFourcc; /* destinition image description */

	uint32_t	x_offset,y_offset; /* to center image on screen */
	unsigned	init_mode; /* mode before run of mplayer */
	any_t*		init_state; /* state before run of mplayer */
	struct win_frame win; /* real-mode window to video memory */
	uint8_t *	dga_buffer; /* for yuv2rgb and sw_scaling */
	unsigned	video_mode; /* selected video mode for playback */
	struct VesaModeInfoBlock vmode_info;

/* multibuffering */
	uint8_t*	video_base; /* should be never changed */
	uint32_t	multi_buff[MAX_DRI_BUFFERS]; /* contains offsets of buffers */
	uint8_t		multi_size; /* total number of buffers */
/* Linux Video Overlay */
#ifdef CONFIG_VIDIX
	Vidix_System*	vidix;
#endif
	uint32_t	subdev_flags;
};

static std::string __FASTCALL__ vbeErrToStr(int err)
{
    std::string rc;
    std::ostringstream oss;
    if((err & VBE_VESA_ERROR_MASK) == VBE_VESA_ERROR_MASK) {
	oss<<"VESA failed = 0x4f"<<std::hex<<setsetfill('0')<<std::setw(2)<<((err & VBE_VESA_ERRCODE_MASK)>>8);
	rc = oss.str();
    } else
	switch(err) {
	    case VBE_OK: rc = "No error"; break;
	    case VBE_VM86_FAIL: rc = "vm86() syscall failed"; break;
	    case VBE_OUT_OF_DOS_MEM: rc = "Out of DOS memory"; break;
	    case VBE_OUT_OF_MEM: rc = MSGTR_OutOfMemory; break;
	    case VBE_BROKEN_BIOS: rc = "Broken BIOS or DOS TSR"; break;
	    default: oss<<"Unknown or internal error: "<<err; rc=oss.str(); break;
    }
    return rc;
}

inline void PRINT_VBE_ERR(const char *name,int err) {
    mpxp_err<<"vo_vesa: "<<name<<" returns: "<<vbeErrToStr(err)<<std::endl;
    fflush(stdout);
}

VESA_VO_Interface::~VESA_VO_Interface()
{
    vesa_term();
    mpxp_dbg3<<"vo_vesa: uninit was called"<<std::endl;
#ifdef CONFIG_VIDIX
    if(vidix) delete vidix;
#endif
}

VESA_VO_Interface::VESA_VO_Interface(const std::string& arg)
		:VO_Interface(arg),
		aspect(new(zeromem) Aspect(mp_conf.monitor_pixel_aspect))
{
    std::string vidix_name;
    MPXP_Rc pre_init_err = MPXP_Ok;
    subdev_flags = 0xFFFFFFFEUL;
    cpy_blk_fnc=NULL;
    mpxp_dbg3<<"vo_vesa: preinit("<<arg<<") was called"<<std::endl;
    mpxp_dbg3<<"vo_vesa: subdevice "<<arg<<" is being initialized"<<std::endl;
    if(!arg.empty()) vidix_name = parse_sub_device(arg);
#ifdef CONFIG_VIDIX
    if(!vidix_name.empty()) {
	if(!(vidix=new(zeromem) Vidix_System(vidix_name))) {
	    mpxp_err<<"Cannot initialze vidix with '"<<vidix_name<<"' argument"<<std::endl;
	    throw std::runtime_error("Vidix error");
	}
    }
#endif
    mpxp_dbg3<<"vo_subdevice: initialization returns: "<<pre_init_err<<std::endl;
    if(pre_init_err==MPXP_Ok)
	if(vbeInit()!=VBE_OK) {
	    pre_init_err=MPXP_False;
	    PRINT_VBE_ERR("vbeInit",pre_init_err);
	    throw std::runtime_error("VESA preinit");
	}
}

static const int MOVIE_MODE=(MODE_ATTR_COLOR | MODE_ATTR_GRAPHICS);
static const int FRAME_MODE=(MODE_WIN_RELOCATABLE | MODE_WIN_WRITEABLE);

void VESA_VO_Interface::vesa_term()
{
    int err;
    if((err=vbeRestoreState(init_state)) != VBE_OK) PRINT_VBE_ERR("vbeRestoreState",err);
    if((err=vbeSetMode(init_mode,NULL)) != VBE_OK) PRINT_VBE_ERR("vbeSetMode",err);
    if(has_dga()) vbeUnmapVideoBuffer((unsigned long)win.ptr,win.high);
    if(dga_buffer && !has_dga()) delete dga_buffer;
    vbeDestroy();
}

void VESA_VO_Interface::__vbeSwitchBank(unsigned long offset)
{
    unsigned long gran;
    unsigned new_offset;
    int err;
    gran = vmode_info.WinGranularity*1024;
    new_offset = offset / gran;
    if(has_dga()) { err = -1; goto show_err; }
    if((err=vbeSetWindow(win.idx,new_offset)) != VBE_OK) {
	show_err:
	vesa_term();
	PRINT_VBE_ERR("vbeSetWindow",err);
	mpxp_fatal<<"vo_vesa: Fatal error occured! Can't continue"<<std::endl;
	throw std::runtime_error("VESA error");
    }
    win.low = new_offset * gran;
    win.high = win.low + vmode_info.WinSize*1024;
}

void VESA_VO_Interface::__vbeSetPixel(int x, int y, int r, int g, int b)
{
    int x_res = vmode_info.XResolution;
    int y_res = vmode_info.YResolution;
    int shift_r = vmode_info.RedFieldPosition;
    int shift_g = vmode_info.GreenFieldPosition;
    int shift_b = vmode_info.BlueFieldPosition;
    int _pixel_size = (dstBpp+7)/8;
    int bpl = vmode_info.BytesPerScanLine;
    int color;
    unsigned offset;

    if (x < 0 || x >= x_res || y < 0 || y >= y_res)	return;
    r >>= 8 - vmode_info.RedMaskSize;
    g >>= 8 - vmode_info.GreenMaskSize;
    b >>= 8 - vmode_info.BlueMaskSize;
    color = (r << shift_r) | (g << shift_g) | (b << shift_b);
    offset = y * bpl + (x * _pixel_size);
    if(!valid_win_frame(offset)) __vbeSwitchBank(offset);
    memcpy(video_ptr(offset), &color, _pixel_size);
}

/*
  Copies part of frame to video memory. Data should be in the same format
  as video memory.
*/
void VESA_VO_Interface::__vbeCopyBlockFast(unsigned long offset,uint8_t *image,unsigned long size)
{
    memcpy(&win.ptr[offset],image,size);
}

void VESA_VO_Interface::__vbeCopyBlock(unsigned long offset,uint8_t *image,unsigned long size)
{
    unsigned long delta,src_idx = 0;
    while(size) {
	if(!valid_win_frame(offset)) __vbeSwitchBank(offset);
	delta = std::min(size,win.high - offset);
	memcpy(video_ptr(offset),&image[src_idx],delta);
	src_idx += delta;
	offset += delta;
	size -= delta;
    }
}

/*
  Copies frame to video memory. Data should be in the same format as video
  memory.
*/
void VESA_VO_Interface::__vbeCopyData(uint8_t *image)
{
    unsigned long i,j,image_offset,offset;
    unsigned _pixel_size,_image_line_size,_screen_line_size,x_shift;
    _pixel_size = pixel_size();
    _screen_line_size = screen_line_size(_pixel_size);
    _image_line_size = image_line_size(_pixel_size);
    if(dstW == vmode_info.XResolution) {
	/* Special case for zooming */
	(this->*cpy_blk_fnc)(y_offset*_screen_line_size,image,_image_line_size*dstH);
    } else {
	x_shift = x_offset*_pixel_size;
	for(j=0,i=y_offset;j<dstH;i++,j++) {
	    offset = i*_screen_line_size+x_shift;
	    image_offset = j*_image_line_size;
	    (this->*cpy_blk_fnc)(offset,&image[image_offset],_image_line_size);
	}
    }
}

MPXP_Rc VESA_VO_Interface::select_frame(unsigned idx)
{
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->select_frame(idx);
#endif
    mpxp_dbg3<<"vo_vesa: select_frame was called"<<std::endl;
    if(!has_dga()) __vbeCopyData(dga_buffer);
    else {
	int err;
	if((err=vbeSetDisplayStart(multi_buff[idx],vo_conf.vsync)) != VBE_OK) {
	    vesa_term();
	    PRINT_VBE_ERR("vbeSetDisplayStart",err);
	    mpxp_fatal<<"vo_vesa: Fatal error occured! Can't continue"<<std::endl;
	    throw std::runtime_error("VESA error");
	}
	win.ptr = dga_buffer = video_base + multi_buff[(idx+1)%multi_size];
    }
    return MPXP_Ok;
}

static const int SUBDEV_NODGA=0x00000001UL;
static const int SUBDEV_FORCEDGA=0x00000002UL;
std::string VESA_VO_Interface::parse_sub_device(const std::string& sd)
{
    subdev_flags = 0;
    if(sd=="nodga") { subdev_flags |= SUBDEV_NODGA; subdev_flags &= ~(SUBDEV_FORCEDGA); }
    else
    if(sd=="dga")   { subdev_flags &= ~(SUBDEV_NODGA); subdev_flags |= SUBDEV_FORCEDGA; }
#ifdef CONFIG_VIDIX
    else
    if(sd.substr(0,5)=="vidix") return &sd[5]; /* priv.vidix_name will be valid within init() */
#endif
    else { mpxp_err<<"vo_vesa: Unknown subdevice: "<<sd<<std::endl; subdev_flags = 0xFFFFFFFFUL; }
    return "";
}

static int __FASTCALL__ check_depth(unsigned bpp)
{
    struct VbeInfoBlock vib;
    struct VesaModeInfoBlock vmib;
    int err;
    unsigned i,num_modes;
    unsigned short *mode_ptr;
    if((err=vbeGetControllerInfo(&vib)) != VBE_OK) return VOCAP_NA;
    num_modes = 0;
    mode_ptr = vib.VideoModePtr;
    while(*mode_ptr++ != 0xffff) num_modes++;
    mode_ptr = vib.VideoModePtr;
    for(i=0;i < num_modes;i++) {
	if((err=vbeGetModeInfo(mode_ptr[i],&vmib)) != VBE_OK) return VOCAP_NA;
	if(vmib.BitsPerPixel == bpp) return VOCAP_SUPPORTED;
    }
    return VOCAP_NA;
}


void VESA_VO_Interface::paintBkGnd()
{
    int x_res = vmode_info.XResolution;
    int y_res = vmode_info.YResolution;
    int x, y;

    for (y = 0; y < y_res; ++y) {
	for (x = 0; x < x_res; ++x) {
	    int r, g, b;
	    if ((x & 16) ^ (y & 16)) {
		r = x * 255 / x_res;
		g = y * 255 / y_res;
		b = 255 - x * 255 / x_res;
	    } else {
		r = 255 - x * 255 / x_res;
		g = y * 255 / y_res;
		b = 255 - y * 255 / y_res;
	    }
	    __vbeSetPixel(x, y, r, g, b);
	}
    }
}

void VESA_VO_Interface::clear_screen()
{
    int x_res = vmode_info.XResolution;
    int y_res = vmode_info.YResolution;
    int x, y;

    for (y = 0; y < y_res; ++y)
	for (x = 0; x < x_res; ++x)
	    __vbeSetPixel(x, y, 0, 0, 0);
}

void VESA_VO_Interface::clear_screen_fast( )
{
    int x_res = vmode_info.XResolution;
    int y_res = vmode_info.YResolution;
    int Bpp = (vmode_info.BitsPerPixel+7)/8;

    memset(dga_buffer,0,x_res*y_res*Bpp);
}

static const char * __FASTCALL__ model2str(unsigned char type)
{
    const char *retval;
    switch(type) {
	case memText: retval = "Text"; break;
	case memCGA:  retval="CGA"; break;
	case memHercules: retval="Hercules"; break;
	case memPL: retval="Planar"; break;
	case memPK: retval="Packed pixel"; break;
	case mem256: retval="256"; break;
	case memRGB: retval="Direct color RGB"; break;
	case memYUV: retval="Direct color YUV"; break;
	default: retval="Unknown"; break;
    }
    return retval;
}

unsigned VESA_VO_Interface::fillMultiBuffer(unsigned long vsize, unsigned nbuffs)
{
    unsigned long screen_size, offset;
    unsigned total,i;
    screen_size = vmode_info.XResolution*vmode_info.YResolution*((dstBpp+7)/8);
    if(screen_size%64) screen_size=((screen_size/64)*64)+64;
    total = vsize / screen_size;
    i = 0;
    offset = 0;
    total = std::min(total,nbuffs);
    while(i < total) { multi_buff[i++] = offset; offset += screen_size; }
    if(!i)
	mpxp_err<<"vo_vesa: Your have too small size of video memory for this mode:"<<std::endl;
	mpxp_err<<"vo_vesa: Requires: "<<std::hex<<std::setfill('0')<<std::setw(8)<<screen_size<<" exists: "<<std::hex<<std::setfill('0')<<std::setw(8)<<vsize<<std::endl;
    return i;
}


/* fullscreen:
 * bit 0 (0x01) means fullscreen (-fs)
 * bit 1 (0x02) means mode switching (-vm)
 * bit 2 (0x04) enables software scaling (-zoom)
 * bit 3 (0x08) enables flipping (-flip) (NK: and for what?)
 */
MPXP_Rc VESA_VO_Interface::configure(uint32_t width, uint32_t height, uint32_t d_width, uint32_t d_height,unsigned flags, const std::string& title, uint32_t format)
{
    struct VbeInfoBlock vib;
    struct VesaModeInfoBlock vmib;
    size_t i,num_modes;
    uint32_t w,h;
    unsigned short *mode_ptr,win_seg;
    unsigned bpp,best_x = UINT_MAX,best_y=UINT_MAX,best_mode_idx = UINT_MAX;
    int err,fs_mode,use_scaler=0;

    srcW = dstW = width;
    srcH = dstH = height;
    fs_mode = 0;
    if(subdev_flags == 0xFFFFFFFEUL) {
	mpxp_err<<"vo_vesa: detected internal fatal error: init is called before preinit"<<std::endl;
	return MPXP_False;
    }
    if(subdev_flags == 0xFFFFFFFFUL) return MPXP_False;
    if(flags & 0x8) {
	mpxp_warn<<"vo_vesa: switch -flip is not supported"<<std::endl;
    }
    if(flags & 0x04) use_scaler = 1;
    if(flags & 0x01) {
	if(use_scaler) use_scaler = 2;
	else          fs_mode = 1;
    }
    memcpy(vib.VESASignature,"VBE2",4);
    if((err=vbeGetControllerInfo(&vib)) != VBE_OK) {
	PRINT_VBE_ERR("vbeGetControllerInfo",err);
	mpxp_fatal<<"vo_vesa: possible reason: No VBE2 BIOS found"<<std::endl;
	return MPXP_False;
    }
    /* Print general info here */
    mpxp_v<<"vo_vesa: Found VESA VBE BIOS Version "<<std::hex<<((vib.VESAVersion >> 8) & 0xff)<<"."<<std::hex<<(vib.VESAVersion & 0xff)<<" Revision: "<<std::hex<<(vib.OemSoftwareRev & 0xffff)<<std::endl;
    mpxp_v<<"vo_vesa: Video memory: "<<(vib.TotalMemory*64)<<" Kb"<<std::endl;
    mpxp_v<<"vo_vesa: VESA Capabilities: "
	  <<(vib.Capabilities & VBE_NONVGA_CRTC ? "non-VGA CRTC,":"VGA CRTC,")<<" "
	  <<(vib.Capabilities & VBE_SNOWED_RAMDAC ? "snowed RAMDAC,":"normal RAMDAC,")<<" "
	  <<(vib.Capabilities & VBE_STEREOSCOPIC ? "stereoscopic,":"no stereoscopic,")<<" "
	  <<(vib.Capabilities & VBE_STEREO_EVC ? "Stereo EVC":"no stereo")<<" "
	  <<(vib.Capabilities & VBE_DAC_8BIT ? "8-bit DAC," : "6-bit DAC,")<<std::endl;
    mpxp_v<<"vo_vesa: !!! Below will be printed OEM info. !!!"<<std::endl;
    mpxp_v<<"vo_vesa: You should watch 5 OEM related lines below else you've broken vm86"<<std::endl;
    mpxp_v<<"vo_vesa: OEM info: "<<vib.OemStringPtr<<std::endl;
    mpxp_v<<"vo_vesa: OEM Revision: "<<std::hex<<vib.OemSoftwareRev<<std::endl;
    mpxp_v<<"vo_vesa: OEM vendor: "<<vib.OemVendorNamePtr<<std::endl;
    mpxp_v<<"vo_vesa: OEM Product Name: "<<vib.OemProductNamePtr<<std::endl;
    mpxp_v<<"vo_vesa: OEM Product Rev: "<<vib.OemProductRevPtr<<std::endl;
    mpxp_hint<<"vo_vesa: Hint: To get workable TV-Out you should have plugged tv-connector in"<<std::endl;
    mpxp_hint<<"vo_vesa: before booting PC since VESA BIOS initializes itself only during POST"<<std::endl;
    /* Find best mode here */
    num_modes = 0;
    mode_ptr = vib.VideoModePtr;
    while(*mode_ptr++ != 0xffff) num_modes++;
    switch(format) {
	case IMGFMT_BGR8:
	case IMGFMT_RGB8:  bpp = 8; break;
	case IMGFMT_BGR15:
	case IMGFMT_RGB15: bpp = 15; break;
	case IMGFMT_BGR16:
	case IMGFMT_RGB16: bpp = 16; break;
	case IMGFMT_BGR24:
	case IMGFMT_RGB24: bpp = 24; break;
	case IMGFMT_BGR32:
	case IMGFMT_RGB32: bpp = 32; break;
	default:	   bpp = 16; break;
    }
    srcBpp = bpp;
    srcFourcc = format;
    if(vo_conf.dbpp) bpp = vo_conf.dbpp;
    switch(bpp) {
	case 8:
	    dstFourcc = IMGFMT_BGR8;
	    break;
	case 15:
	    dstFourcc = IMGFMT_BGR15;
	    break;
	case 16:
	    dstFourcc = IMGFMT_BGR16;
	    break;
	case 24:
	    dstFourcc = IMGFMT_BGR24;
	    break;
	case 32:
	    dstFourcc = IMGFMT_BGR32;
	    break;
	default:
	    dstFourcc = IMGFMT_BGR16;
	    break;
    }
    if(mp_conf.verbose) {
	mpxp_v<<"vo_vesa: Requested mode: "<<width<<"x"<<height<<"@"<<bpp<<" ("<<vo_format_name(format)<<")"<<std::endl;
	mpxp_v<<"vo_vesa: Total modes found: "<<num_modes<<std::endl;
	mode_ptr = vib.VideoModePtr;
	mpxp_v<<"vo_vesa: Mode list:";
	for(i = 0;i < num_modes;i++) {
	    mpxp_v<<" "<<std::hex<<std::setw(4)<<mode_ptr[i];
	}
	mpxp_v<<std::endl;
	mpxp_v<<"vo_vesa: Modes in detail:"<<std::endl;
    }
    mode_ptr = vib.VideoModePtr;
    if(use_scaler) {
	dstW = d_width;
	dstH = d_height;
    }
    w = std::max(dstW,width);
    h = std::max(dstH,height);
    for(i=0;i < num_modes;i++) {
	if((err=vbeGetModeInfo(mode_ptr[i],&vmib)) != VBE_OK) {
	    PRINT_VBE_ERR("vbeGetModeInfo",err);
	    return MPXP_False;
	}
	if(vmib.XResolution >= w &&
	   vmib.YResolution >= h &&
	   (vmib.ModeAttributes & MOVIE_MODE) == MOVIE_MODE &&
	   vmib.BitsPerPixel == bpp) {
	    if((bpp > 8 && vmib.MemoryModel == memRGB) || bpp < 15)
	    if(vmib.XResolution <= best_x &&
		vmib.YResolution <= best_y) {
		    best_x = vmib.XResolution;
		    best_y = vmib.YResolution;
		    best_mode_idx = i;
	    }
	}
	if(mp_conf.verbose) {
	    mpxp_v<<"vo_vesa: Mode ("<<i<<"): mode="<<std::hex<<std::setw(4)<<mode_ptr[i]<<" "<<vmib.XResolution<<"x"<<vmib.YResolution<<"@"<<vmib.BitsPerPixel<<" attr="<<std::hex<<std::setw(4)<<vmib.ModeAttributes<<std::endl;
	    mpxp_v<<"vo_vesa:             #planes="<<vmib.NumberOfPlanes<<" model="<<vmib.MemoryModel<<"("<<model2str(vmib.MemoryModel)<<") #pages="<<vmib.NumberOfImagePages<<std::endl;
	    mpxp_v<<"vo_vesa:             winA="<<vmib.WinASegment<<"(attr="<<vmib.WinAAttributes<<") winB="<<std::hex<<vmib.WinBSegment<<"(attr="<<vmib.WinBAttributes<<") winSize="<<vmib.WinSize<<" winGran="<<vmib.WinGranularity<<std::endl;
	    mpxp_v<<"vo_vesa:             direct_color="<<vmib.DirectColorModeInfo<<" DGA_phys_addr="<<vmib.PhysBasePtr<<std::endl;
	    if(vmib.MemoryModel == 6 || vmib.MemoryModel == 7)
		mpxp_v<<"vo_vesa:             direct_color_info = "<<vmib.RedMaskSize<<":"<<vmib.GreenMaskSize<<":"<<vmib.BlueMaskSize<<":"<<vmib.RsvdMaskSize<<std::endl;
	    fflush(stdout);
	}
    }
    if(best_mode_idx != UINT_MAX) {
	video_mode = vib.VideoModePtr[best_mode_idx];
	fflush(stdout);
	if((err=vbeGetMode(&init_mode)) != VBE_OK) {
	    PRINT_VBE_ERR("vbeGetMode",err);
	    return MPXP_False;
	}
	mpxp_v<<"vo_vesa: Initial video mode: "<<std::hex<<init_mode<<std::endl;
	if((err=vbeGetModeInfo(video_mode,&vmode_info)) != VBE_OK) {
	    PRINT_VBE_ERR("vbeGetModeInfo",err);
	    return MPXP_False;
	}
	dstBpp = vmode_info.BitsPerPixel;
	mpxp_v<<"vo_vesa: Using VESA mode ("<<best_mode_idx<<") = "<<std::hex<<video_mode<<" ["<<vmode_info.XResolution<<"x"<<vmode_info.YResolution<<"@"<<dstBpp<<"]"<<std::endl;
	if(subdev_flags & SUBDEV_NODGA) vmode_info.PhysBasePtr = 0;
	if(use_scaler || fs_mode) {
	    /* software scale */
	    if(use_scaler > 1) {
		aspect->save(width,height,d_width,d_height,vmode_info.XResolution,vmode_info.YResolution);
		aspect->calc(dstW,dstH,flags&VOFLAG_FULLSCREEN?Aspect::ZOOM:Aspect::NOZOOM);
	    } else if(fs_mode) {
		dstW = vmode_info.XResolution;
		dstH = vmode_info.YResolution;
	    }
	    use_scaler = 1;
	}
	if((vmode_info.WinAAttributes & FRAME_MODE) == FRAME_MODE)
	    win.idx = 0; /* frame A */
	else if((vmode_info.WinBAttributes & FRAME_MODE) == FRAME_MODE)
	    win.idx = 1; /* frame B */
	else win.idx = -2;
	/* Try use DGA instead */
	if(vmode_info.PhysBasePtr && vib.TotalMemory && (vmode_info.ModeAttributes & MODE_ATTR_LINEAR)) {
	    any_t*lfb;
	    unsigned long vsize;
	    vsize = vib.TotalMemory*64*1024;
	    lfb = vbeMapVideoBuffer(vmode_info.PhysBasePtr,vsize);
	    if(lfb == NULL) mpxp_warn<<"vo_vesa: Can't use DGA. Force bank switching mode. :("<<std::endl;
	    else {
		video_base = win.ptr = reinterpret_cast<uint8_t*>(lfb);
		win.low = 0UL;
		win.high = vsize;
		win.idx = -1; /* HAS_DGA() is on */
		video_mode |= VESA_MODE_USE_LINEAR;
		mpxp_v<<"vo_vesa: Using DGA (physical resources: "<<std::hex<<vmode_info.PhysBasePtr<<"h, "<<vsize<<"h)"<<std::endl;
		mpxp_v<<" at "<<lfb<<")"<<std::endl;
		if(!(multi_size = fillMultiBuffer(vsize,vo_conf.xp_buffs))) return MPXP_False;
		if(multi_size < 2) mpxp_err<<"vo_vesa: Can't use double buffering: not enough video memory"<<std::endl;
		else mpxp_v<<"vo_vesa: using "<<multi_size<<" buffers for multi buffering"<<std::endl;
	    }
	}
	if(win.idx == -2) {
	    mpxp_err<<"vo_vesa: Can't find neither DGA nor relocatable window's frame."<<std::endl;
	    return MPXP_False;
	}
	if(!has_dga()) {
	    if(subdev_flags & SUBDEV_FORCEDGA) {
		mpxp_err<<"vo_vesa: you've forced DGA. Exiting"<<std::endl;
		return MPXP_False;
	    }
	    if(!(win_seg = win.idx == 0 ? vmode_info.WinASegment:vmode_info.WinBSegment)) {
		mpxp_err<<"vo_vesa: Can't find valid window address"<<std::endl;
		return MPXP_False;
	    }
	    win.ptr = (uint8_t*)PhysToVirtSO(win_seg,0);
	    win.low = 0L;
	    win.high= vmode_info.WinSize*1024;
	    mpxp_v<<"vo_vesa: Using bank switching mode (physical resources: "<<std::hex<<reinterpret_cast<any_t*>(win.ptr)<<", "<<std::hex<<win.high<<"h)"<<std::endl;
	}
	if(vmode_info.XResolution > dstW) x_offset = (vmode_info.XResolution - dstW) / 2;
	else x_offset = 0;
	if(vmode_info.YResolution > dstH)
	    y_offset = (vmode_info.YResolution - dstH) / 2;
	else y_offset = 0;
	    mpxp_v<<"vo_vesa: image: "<<dstW<<"x"<<dstH<<" screen = "<<vmode_info.XResolution<<"x"<<vmode_info.YResolution<<" x_offset = "<<x_offset<<" y_offset = "<<y_offset<<std::endl;
	if(has_dga()) {
	    dga_buffer = win.ptr; /* Trickly ;) */
	    cpy_blk_fnc = &VESA_VO_Interface::__vbeCopyBlockFast;
	} else {
	    cpy_blk_fnc = &VESA_VO_Interface::__vbeCopyBlock;
#ifdef CONFIG_VIDIX
	    if(!vidix)
#endif
	    {
		if(!(dga_buffer = new(alignmem,64) uint8_t[vmode_info.XResolution*vmode_info.YResolution*dstBpp])) {
		    mpxp_err<<"vo_vesa: Can't allocate temporary buffer"<<std::endl;
		    return MPXP_False;
		}
		mpxp_v<<"vo_vesa: dga emulator was allocated = "<<std::hex<<reinterpret_cast<any_t*>(dga_buffer)<<std::endl;
	    }
	}
	if((err=vbeSaveState(&init_state)) != VBE_OK) {
	    PRINT_VBE_ERR("vbeSaveState",err);
	    return MPXP_False;
	}
	if((err=vbeSetMode(video_mode,NULL)) != VBE_OK) {
	    PRINT_VBE_ERR("vbeSetMode",err);
	    return MPXP_False;
	}
	/* Now we are in video mode!!!*/
	/* Below 'return MPXP_False' is impossible */
	mpxp_v<<"vo_vesa: Graphics mode was activated"<<std::endl;
#ifdef CONFIG_VIDIX
	if(vidix) {
	    if(vidix->configure(width,height,x_offset,y_offset,dstW,
			dstH,format,dstBpp,
			vmode_info.XResolution,vmode_info.YResolution) != MPXP_Ok) {
		mpxp_err<<"vo_vesa: Can't initialize VIDIX driver"<<std::endl;
		vesa_term();
		return MPXP_False;
	    } else mpxp_v<<"vo_vesa: Using VIDIX"<<std::endl;
	    if(vidix->start()!=0) {
		vesa_term();
		return MPXP_False;
	    }
	}
#endif
    } else {
	mpxp_err<<"vo_vesa: Can't find mode for: "<<width<<"x"<<height<<"@"<<bpp<<std::endl;
	return MPXP_False;
    }
    mpxp_v<<"vo_vesa: VESA initialization complete"<<std::endl;
    if(has_dga()) {
	for(i=0;i<multi_size;i++) {
	    win.ptr = dga_buffer = video_base + multi_buff[i];
	    if(mp_conf.verbose>1) paintBkGnd();
	    else	  clear_screen_fast();
	}
    } else {
	int x;
	if(mp_conf.verbose>1) paintBkGnd();
	else clear_screen();
	x = (vmode_info.XResolution/vmode_info.XCharSize)/2-title.length()/2;
	if(x < 0) x = 0;
	vbeWriteString(x,0,7,title.c_str());
    }
    return MPXP_Ok;
}

MPXP_Rc VESA_VO_Interface::query_format(vo_query_fourcc_t* format) const
{
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->query_fourcc(format);
#endif
    mpxp_dbg3<<"vo_vesa: query_format was called: "<<std::hex<<format->fourcc<<" ("<<vo_format_name(format->fourcc)<<")"<<std::endl;
    switch(format->fourcc) {
	case IMGFMT_BGR8: format->flags=check_depth(8); break;
	case IMGFMT_BGR15: format->flags=check_depth(15); break;
	case IMGFMT_BGR16: format->flags=check_depth(16); break;
	case IMGFMT_BGR24: format->flags=check_depth(24); break;
	case IMGFMT_BGR32: format->flags=check_depth(32); break;
	default: break;
    }
    return MPXP_Ok;
}

void VESA_VO_Interface::get_surface_caps(dri_surface_cap_t *caps) const
{
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->get_surface_caps(caps);
#endif
    caps->caps = has_dga() ? DRI_CAP_VIDEO_MMAPED : DRI_CAP_TEMP_VIDEO;
    caps->fourcc = dstFourcc;
    caps->width=has_dga()?vmode_info.XResolution:dstW;
    caps->height=has_dga()?vmode_info.YResolution:dstH;
    caps->x=x_offset;
    caps->y=y_offset;
    caps->w=dstW;
    caps->h=dstH;
    caps->strides[0] = (has_dga()?vmode_info.XResolution:dstW)*((dstBpp+7)/8);
    caps->strides[1] = 0;
    caps->strides[2] = 0;
    caps->strides[3] = 0;
}

void VESA_VO_Interface::get_surface(dri_surface_t *surf)
{
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->get_surface(surf);
#endif
    surf->planes[0] = has_dga()?video_base + multi_buff[surf->idx]:dga_buffer;
    surf->planes[1] = 0;
    surf->planes[2] = 0;
    surf->planes[3] = 0;
}

unsigned VESA_VO_Interface::get_num_frames() const {
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->get_num_frames();
#endif
    return multi_size;
}

MPXP_Rc VESA_VO_Interface::flush_page(unsigned idx) {
#ifdef CONFIG_VIDIX
    if(vidix) return vidix->flush_page(idx);
#endif
    return MPXP_False;
}

uint32_t VESA_VO_Interface::check_events(const vo_resize_t* vr) {
    UNUSED(vr);
    return 0;
}

MPXP_Rc VESA_VO_Interface::ctrl(uint32_t request, any_t*data)
{
#ifdef CONFIG_VIDIX
    switch (request) {
	case VOCTRL_SET_EQUALIZER:
	    if(!vidix->set_video_eq(reinterpret_cast<vo_videq_t*>(data))) return MPXP_True;
	    return MPXP_False;
	case VOCTRL_GET_EQUALIZER:
	    if(vidix->get_video_eq(reinterpret_cast<vo_videq_t*>(data))) return MPXP_True;
	    return MPXP_False;
    }
#endif
    return MPXP_NA;
}

static VO_Interface* query_interface(const std::string& args) { return new(zeromem) VESA_VO_Interface(args); }
extern const vo_info_t vesa_vo_info =
{
	"VESA VBE 2.0 video output"
#ifdef CONFIG_VIDIX
	" (with vesa:vidix subdevice)"
#endif
	,
	"vesa",
	"Nickols_K <nickols_k@mail.ru>",
	"Requires ROOT privileges",
	query_interface
};
} //namespace
