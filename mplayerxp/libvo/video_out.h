/*
 *  video_out.h
 *
 *      Copyright (C) Aaron Holtzman - Aug 1999
 *	Strongly modified, most parts rewritten: A'rpi/ESP-team - 2000-2001
 *
 */

#ifndef __VIDEO_OUT_H
#define __VIDEO_OUT_H 1

#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;

#include <inttypes.h>
#include <stdarg.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "dri_vo.h"
#include "font_load.h"
#include "sub.h"
#include "libmpsub/subreader.h"
#include "img_format.h"
#include "xmpcore/mp_image.h"
#include "xmpcore/xmp_enums.h"

namespace mpxp {
    enum {
	VO_EVENT_EXPOSE		=1,
	VO_EVENT_RESIZE		=2,
	VO_EVENT_KEYPRESS	=4,
	VO_EVENT_FORCE_UPDATE	=0x80000000
    };

    enum {
	VOCTRL_SET_EQUALIZER=1,	/**< Set video equalizer */
	VOCTRL_GET_EQUALIZER	/**< Get video equalizer */
    };

    enum vo_flags_e {
	VOFLAG_NONE		=0x00,	/**< User wants to have fullscreen playback */
	VOFLAG_FULLSCREEN	=0x01,	/**< User wants to have fullscreen playback */
	VOFLAG_MODESWITCHING	=0x02,	/**< User enables to find the best video mode */
	VOFLAG_SWSCALE		=0x04,	/**< Obsolete. User enables slow Software scaler */
	VOFLAG_FLIPPING		=0x08	/**< User enables page flipping (doublebuffering / XP mode) */
    };

    /** Text description of VO-driver */
    class VO_Interface;
    typedef VO_Interface* (*query_interface_t)(const char* args);
    struct vo_info_t {
	const char* name;	/**< driver name ("Matrox Millennium G200/G400") */
	const char* short_name; /**< short name (for config strings) ("mga") */
	const char* author;	/**< author ("Aaron Holtzman <aholtzma@ess.engr.uvic.ca>") */
	const char* comment;/**< any additional comments */
	query_interface_t query_interface;
    };

    enum {
	VOCAP_NA=0x00,
	VOCAP_SUPPORTED=0x01,
	VOCAP_HWSCALER=0x02,
	VOCAP_FLIP=0x04
    };
    inline vo_flags_e operator~(vo_flags_e a) { return static_cast<vo_flags_e>(~static_cast<unsigned>(a)); }
    inline vo_flags_e operator|(vo_flags_e a, vo_flags_e b) { return static_cast<vo_flags_e>(static_cast<unsigned>(a)|static_cast<unsigned>(b)); }
    inline vo_flags_e operator&(vo_flags_e a, vo_flags_e b) { return static_cast<vo_flags_e>(static_cast<unsigned>(a)&static_cast<unsigned>(b)); }
    inline vo_flags_e operator^(vo_flags_e a, vo_flags_e b) { return static_cast<vo_flags_e>(static_cast<unsigned>(a)^static_cast<unsigned>(b)); }
    inline vo_flags_e operator|=(vo_flags_e a, vo_flags_e b) { return (a=static_cast<vo_flags_e>(static_cast<unsigned>(a)|static_cast<unsigned>(b))); }
    inline vo_flags_e operator&=(vo_flags_e a, vo_flags_e b) { return (a=static_cast<vo_flags_e>(static_cast<unsigned>(a)&static_cast<unsigned>(b))); }
    inline vo_flags_e operator^=(vo_flags_e a, vo_flags_e b) { return (a=static_cast<vo_flags_e>(static_cast<unsigned>(a)^static_cast<unsigned>(b))); }

    /** Request for supported FOURCC by VO-driver */
    typedef struct vo_query_fourcc_s {
	uint32_t	fourcc;	/**< Fourcc of decoded image */
	unsigned	w,h;	/**< Width and height of decoded image */
	unsigned	flags;  /**< Flags for this fourcc VOCAP_*  */
    }vo_query_fourcc_t;

    /** Named video equalizer */
    typedef struct vo_videq_s {
#define VO_EC_BRIGHTNESS "Brightness"
#define VO_EC_CONTRAST	 "Contrast"
#define VO_EC_GAMMA	 "Gamma"
#define VO_EC_HUE	 "Hue"
#define VO_EC_SATURATION "Saturation"
#define VO_EC_RED_INTENSITY "RedIntensity"
#define VO_EC_GREEN_INTENSITY "GreenIntensity"
#define VO_EC_BLUE_INTENSITY "BlueIntensity"
	const char *name;	/**< name of equalizer control */
	int	    value;	/**< value of equalizer control in range -1000 +1000 */
    }vo_videq_t;

    typedef struct vo_gamma_s{
	int		brightness;
	int		saturation;
	int		contrast;
	int		hue;
	int		red_intensity;
	int		green_intensity;
	int		blue_intensity;
    }vo_gamma_t;

    typedef struct vo_rect_s {
	unsigned x,y,w,h;
    }vo_rect_t;

    struct vo_rect2 {
	int left, right, top, bottom, width, height;
    };

    struct VO_Config {
	VO_Config();
	~VO_Config() {}

	char *		subdevice; // currently unused
	char*		mDisplayName;
	int		xinerama_screen;

	int		vsync;

	unsigned	xp_buffs; /**< contains number of buffers for decoding ahead */
	unsigned	use_bm; /**< indicates user's agreement for using busmastering */

	vo_gamma_t	gamma;

	int		image_width; //opt_screen_size_x
	int		image_height; //opt_screen_size_y
	float		image_zoom; //screen_size_xy

	float		movie_aspect;
	int		fsmode;
	int		vidmode;
	int		fullscreen;
	int		softzoom;
	int		flip;
	unsigned	dbpp;
    };
    extern VO_Config vo_conf;

    class video_private : public Opaque {
	public:
	    video_private() {}
	    virtual ~video_private() {}
    };

    class Video_Output {
	public:
	    Video_Output();
	    virtual ~Video_Output();

	    int		ZOOM() const	{ return flags&VOFLAG_SWSCALE; }
	    void	ZOOM_SET()	{ flags|=VOFLAG_SWSCALE; }
	    void	ZOOM_UNSET()	{ flags&=~VOFLAG_SWSCALE; }
	    int		FS() const	{ return flags&VOFLAG_FULLSCREEN; }
	    void	FS_SET()	{ flags|=VOFLAG_FULLSCREEN; }
	    void	FS_UNSET()	{ flags&=~VOFLAG_FULLSCREEN; }
	    int		VM() const	{ return flags&VOFLAG_MODESWITCHING; }
	    void	VM_SET()	{ flags|=VOFLAG_MODESWITCHING; }
	    void	VM_UNSET()	{ flags&=~VOFLAG_MODESWITCHING; }
	    int		FLIP() const	{ return flags&VOFLAG_FLIPPING; }
	    void	FLIP_SET()	{ flags|=VOFLAG_FLIPPING; }
	    void	FLIP_UNSET()	{ flags&=~VOFLAG_FLIPPING; }
	    void	FLIP_REVERT()	{ flags^=VOFLAG_FLIPPING; }

	    MPXP_Rc	init(const char *subdevice_name) const;
	    void	print_help() const;
	    MPXP_Rc	_register(const char *driver_name) const;
	    const vo_info_t* get_info() const;
	    MPXP_Rc	configure(uint32_t width, uint32_t height, uint32_t d_width,
				  uint32_t d_height, vo_flags_e fullscreen,const char *title,
				  uint32_t format);
	    uint32_t	query_format(uint32_t* fourcc,unsigned src_w,unsigned src_h) const;

	    MPXP_Rc	reset() const;
	    MPXP_Rc	fullscreen() const;
	    MPXP_Rc	screenshot(unsigned idx) const;
	    MPXP_Rc	pause() const;
	    MPXP_Rc	resume() const;

	    MPXP_Rc	get_surface(mp_image_t* mpi) const;
	    MPXP_Rc	get_surface_caps(dri_surface_cap_t*) const;

	    int		check_events() const;
	    unsigned	get_num_frames() const;
	    MPXP_Rc	draw_slice(const mp_image_t *mpi) const;
	    void	select_frame(unsigned idx) const;
	    void	flush_page(unsigned decoder_idx) const;
	    void	draw_osd(unsigned idx) const;
	    void	draw_spudec_direct(unsigned idx) const;
	    MPXP_Rc	ctrl(uint32_t request, any_t*data) const;
	    int		is_final() const;

	    int		adjust_size(unsigned cw,unsigned ch,unsigned *nw,unsigned *nh) const;
	    void	dri_remove_osd(unsigned idx,int x0,int _y0, int w,int h) const;
	    void	dri_draw_osd(unsigned idx,int x0,int _y0, int w,int h,const unsigned char* src,const unsigned char *srca, int stride) const;

	    char		antiviral_hole[RND_CHAR4];
	    vo_flags_e		flags;
	    /* subtitle support */
	    char*		osd_text;
	    any_t*		spudec;
	    any_t*		vobsub;
	    font_desc_t*	font;
	    int			osd_progbar_type;
	    int			osd_progbar_value;   // 0..255
	    const subtitle*	sub;
	private:
	    void		dri_config(uint32_t fourcc) const;
	    void		ps_tune(unsigned width,unsigned height) const;
	    void		dri_tune(unsigned width,unsigned height) const;
	    void		dri_reconfig(int is_resize) const;

	    void		clear_rect(unsigned _y0,unsigned h,uint8_t *dest,unsigned stride,unsigned dstride,uint8_t filler) const;
	    void		clear_rect2(unsigned _y0,unsigned h,uint8_t *dest,unsigned stride,unsigned dstride,uint8_t filler) const;
	    void		clear_rect4(unsigned _y0,unsigned h,uint8_t *dest,unsigned stride,unsigned dstride,uint8_t filler) const;
	    void		clear_rect_rgb(unsigned _y0,unsigned h,uint8_t *dest,unsigned stride,unsigned dstride) const;
	    void		clear_rect_yuy2(unsigned _y0,unsigned h,uint8_t *dest,unsigned stride,unsigned dstride) const;

	    int			inited;
	    video_private*	vo_priv;/* private data of vo structure */
    };

    /** Notification event when windowed output has been resized (as data of VOCTRL_CHECK_EVENT) */
    typedef int (*__FASTCALL__ vo_adjust_size_t)(const Video_Output*,unsigned cw,unsigned ch,unsigned *nw,unsigned *nh);
    struct vo_resize_t {
	uint32_t		event_type; /**< X11 event type */

	/** callback to adjust size of window keeping aspect ratio
	    * @param cw	current window width
	    * @param ch	current window height
	    * @param nw	storage for new width to be stored current window width
	    * @param nh	storage for new height to be stored current window width
	    * @return	0 if fail  !0 if success
	**/
	const Video_Output*	vo;
	vo_adjust_size_t	adjust_size;
    };
    /** Contains geometry of fourcc */
    typedef struct s_vo_format_desc {
	unsigned bpp;
	/* in some strange fourccs (NV12) horz period != vert period of UV */
	unsigned x_mul[4],x_div[4];
	unsigned y_mul[4],y_div[4];
    }vo_format_desc;
    extern int	__FASTCALL__	vo_describe_fourcc(uint32_t fourcc,vo_format_desc *vd);
} // namespace mpxp
#endif
