#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "mplayerxp.h"

#ifdef HAVE_X11
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "video_out.h"
#include "x11_system.h"
#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include "wskeys.h"
#include "osdep/keycodes.h"
#include "input2/input.h"
#include "input2/mouse.h"
#include "vo_msg.h"

#ifdef HAVE_SHM
/* since it doesn't seem to be defined on some platforms */
extern int XShmGetEventBase( Display* );
#endif

static int x11_errorhandler(::Display *display,::XErrorEvent *event)
{
    char msg[256];

    ::XGetErrorText(display, event->error_code, (char *)&msg, sizeof(msg));
    MSG_ERR("X11 error: %s\n", msg);
    MSG_V("Type: %x, display: %x, resourceid: %x, serial: %lx\n",
	    event->type, event->display, event->resourceid, event->serial);
    MSG_V("Error code: %x, request code: %x, minor code: %x\n",
	    event->error_code, event->request_code, event->minor_code);
    exit_player("X11 error");
    return 0;
}

X11_System::X11_System(const char* DisplayName) {
    unsigned bpp;
    unsigned int mask;
// char    * DisplayName = ":0.0";
    ::XImage  * mXImage = NULL;
    ::XWindowAttributes attribs;
    const char* dispName;

    MotifHints = None;

#ifdef HAVE_SHM
    CompletionType=-1;
#endif
    ::XSetErrorHandler(x11_errorhandler);

    if (!DisplayName)
	if (!(DisplayName=getenv("DISPLAY")))
	    DisplayName=":0.0";
    dispName = ::XDisplayName(DisplayName);

    MSG_V("X11 opening display: %s\n", dispName);

    if(!(mDisplay=::XOpenDisplay(dispName))) {
	MSG_ERR( "X11_System: couldn't open the X11 display: '%s'!\n",dispName );
	exit_player("X11 error");
    }
    mScreen=DefaultScreen( mDisplay );
    mRootWin=RootWindow( mDisplay,mScreen );// Root window ID.

#ifdef HAVE_XINERAMA
    if(::XineramaIsActive(mDisplay)) {
	XineramaScreenInfo *screens;
	int num_screens;

	screens = ::XineramaQueryScreens(mDisplay, &num_screens);
	if(mp_conf.xinerama_screen >= num_screens) mp_conf.xinerama_screen = 0;
	if (! vo_conf.screenwidth)
	    vo_conf.screenwidth=screens[mp_conf.xinerama_screen].width;
	if (! vo_conf.screenheight)
	    vo_conf.screenheight=screens[mp_conf.xinerama_screen].height;
	xinerama_x = screens[mp_conf.xinerama_screen].x_org;
	xinerama_y = screens[mp_conf.xinerama_screen].y_org;

	::XFree(screens);
    } else
#endif
#ifdef HAVE_XF86VM
    {
	int clock;
	::XF86VidModeGetModeLine( mDisplay,mScreen,&clock ,&modeline );
	if ( !vo_conf.screenwidth )  vo_conf.screenwidth=modeline.hdisplay;
	if ( !vo_conf.screenheight ) vo_conf.screenheight=modeline.vdisplay;
    }
#endif

    if (! vo_conf.screenwidth)
	vo_conf.screenwidth=DisplayWidth( mDisplay,mScreen );
    if (! vo_conf.screenheight)
	vo_conf.screenheight=DisplayHeight( mDisplay,mScreen );

    // get color depth (from root window, or the best visual):
    ::XGetWindowAttributes(mDisplay, mRootWin, &attribs);
    _depth=attribs.depth;

#ifdef	SCAN_VISUALS
    if (_depth != 15 && _depth != 16 && _depth != 24 && _depth != 32) {
	::Visual *visual;

	_depth = vo_find_depth_from_visuals(mDisplay, mScreen, &visual);
	if ((int)_depth != -1)
	    mXImage=::XCreateImage(mDisplay, visual, _depth, ZPixmap,
				0, NULL, 1, 1, 8, 1);
    } else
#endif
	mXImage=::XGetImage( mDisplay,mRootWin,0,0,1,1,AllPlanes,ZPixmap );

    // get bits/pixel from XImage structure:
    if (mXImage == NULL) mask = 0;
    else {
   /*
    * for the depth==24 case, the XImage structures might use
    * 24 or 32 bits of data per pixel.  The global variable
    * vo_depthonscreen stores the amount of data per pixel in the
    * XImage structure!
    *
    * Maybe we should rename vo_depthonscreen to (or add) vo_bpp?
    */
	bpp=mXImage->bits_per_pixel;
	if((_depth+7)/8 != (bpp+7)/8) _depth=bpp; // by A'rpi
	mask=mXImage->red_mask|mXImage->green_mask|mXImage->blue_mask;
	MSG_V("X11_System: color mask:  %X  (R:%lX G:%lX B:%lX)\n",
	    mask,mXImage->red_mask,mXImage->green_mask,mXImage->blue_mask);
	XDestroyImage( mXImage );
    }
    if(((_depth+7)/8)==2){
	if(mask==0x7FFF) _depth=15;
	else if(mask==0xFFFF) _depth=16;
    }
/* slightly improved local display detection AST */
    if ( strncmp(dispName, "unix:", 5) == 0) dispName += 4;
    else if ( strncmp(dispName, "localhost:", 10) == 0) dispName += 9;
    if (*dispName==':')	mLocalDisplay=1;
    else		mLocalDisplay=0;
    MSG_OK("X11_System: running  %dx%d with depth %d bits/pixel (\"%s\" => %s display)\n",
	vo_conf.screenwidth,vo_conf.screenheight,
	_depth,
	dispName,mLocalDisplay?"local":"remote");
}

X11_System::~X11_System() {
    ::XSetErrorHandler(NULL);
    /* and -wid is set */
    ::XDestroyWindow(mDisplay, window);
    ::XCloseDisplay(mDisplay);
}

void X11_System::get_win_coord(vo_rect_t* r) const
{
    XWindowAttributes xwa;
    ::XGetWindowAttributes(mDisplay, window, &xwa);
    r->x = xwa.x;
    r->y = xwa.y;
    r->w = xwa.width;
    r->h = xwa.height;
}

void X11_System::match_visual(XVisualInfo* vinfo) const
{
    ::XMatchVisualInfo( mDisplay,mScreen,_depth,TrueColor,vinfo);
}

XVisualInfo* X11_System::get_visual() const
{
    XWindowAttributes wattr;
    XVisualInfo vi_template;
    int dummy;

    ::XGetWindowAttributes(mDisplay, window, &wattr);
    vi_template.visualid = ::XVisualIDFromVisual(wattr.visual);
    return ::XGetVisualInfo(mDisplay, VisualIDMask, &vi_template, &dummy);
}

void X11_System::create_window(const XSizeHints& hint,Visual* visual,int is_vm,unsigned dpth,const char*title)
{
    Colormap theCmap;
    XSetWindowAttributes xswa;
    XGCValues xgcv;
    XEvent xev;
    unsigned xswamask;

    theCmap  =::XCreateColormap( mDisplay,RootWindow( mDisplay,mScreen ),
				visual,AllocNone);

    xswa.background_pixel=0;
    xswa.border_pixel=0;
    xswa.colormap=theCmap;
    xswamask=CWBackPixel | CWBorderPixel | CWColormap;

#ifdef HAVE_XF86VM
    if ( is_vm ) {
	xswa.override_redirect=True;
	xswamask|=CWOverrideRedirect;
    }
#endif

    window=::XCreateWindow( mDisplay,RootWindow( mDisplay,mScreen ),
				hint.x,hint.y,
				hint.width,hint.height,
				xswa.border_pixel,dpth,CopyFromParent,visual,xswamask,&xswa );
    XSizeHints hn = hint;
    ::XSetStandardProperties( mDisplay,window,title,title,None,NULL,0,&hn );
    ::XMapWindow( mDisplay,window );
#ifdef HAVE_XINERAMA
    xinerama_move(&hn);
#endif
    select_input( StructureNotifyMask );
    do { ::XNextEvent( mDisplay,&xev ); } while ( xev.type != MapNotify || xev.xmap.event != window );
    select_input( NoEventMask );
    flush();
    sync( False );
    gc=::XCreateGC( mDisplay,window,0L,&xgcv );
#ifdef HAVE_XF86VM
    if ( is_vm ) {
	/* Grab the mouse pointer in our window */
	::XGrabPointer(mDisplay, window, True, 0,
		   GrabModeAsync, GrabModeAsync,
		   window, None, CurrentTime);
	::XSetInputFocus(mDisplay, window, RevertToNone, CurrentTime);
    }
#endif
}

void X11_System::select_input(long mask) const
{
    ::XSelectInput( mDisplay,window,mask );
}

void X11_System::flush() const { ::XFlush( mDisplay ); }
void X11_System::sync(int method) const { ::XSync( mDisplay,method ); }

int X11_System::Shmem_Flag() const
{
    return _Shmem_Flag;
}

XImage* X11_System::Image(unsigned idx) const
{
    return myximage[idx];
}

uint8_t* X11_System::ImageData(unsigned idx) const
{
    return (uint8_t*)Image(idx)->data;
}

void X11_System::getMyXImage(unsigned idx,Visual *visual,unsigned _dpth,unsigned w,unsigned h)
{
#ifdef HAVE_SHM
    if ( mLocalDisplay && ::XShmQueryExtension( mDisplay )) _Shmem_Flag=1;
    else {
	_Shmem_Flag=0;
	MSG_V( "Shared memory not supported\nReverting to normal Xlib\n" );
    }
    if ( _Shmem_Flag ) {
	CompletionType=::XShmGetEventBase( mDisplay ) + ShmCompletion;
	myximage[idx]=::XShmCreateImage( mDisplay,visual,_dpth,ZPixmap,NULL,&Shminfo[idx],w,h);
	if ( myximage[idx] == NULL ) {
	    MSG_V( "Shared memory error,disabling ( Ximage error )\n" );
	    goto shmemerror;
	}
	Shminfo[idx].shmid=::shmget( IPC_PRIVATE,
		myximage[idx]->bytes_per_line * myximage[idx]->height,IPC_CREAT|0777);
	if ( Shminfo[idx].shmid < 0 ) {
	    XDestroyImage( myximage[idx] );
	    MSG_V("Shared memory error '%s' disabling ( seg id error )\n",strerror(errno));
	    goto shmemerror;
	}
	Shminfo[idx].shmaddr=(char *)::shmat( Shminfo[idx].shmid,0,0 );

	if (Shminfo[idx].shmaddr == ((char*)-1)) {
	    XDestroyImage( myximage[idx] );
	    if (Shminfo[idx].shmaddr != ((char*)-1)) ::shmdt(Shminfo[idx].shmaddr);
	    MSG_V( "Shared memory error, disabling ( address error )\n" );
	    goto shmemerror;
	}
	myximage[idx]->data=Shminfo[idx].shmaddr;
	Shminfo[idx].readOnly=False;
	::XShmAttach( mDisplay,&Shminfo[idx] );

	::XSync( mDisplay,False );

	if ( gXErrorFlag ) {
	    XDestroyImage( myximage[idx] );
	    shmdt( Shminfo[idx].shmaddr );
	    MSG_V( "Shared memory error,disabling.\n" );
	    gXErrorFlag=0;
	    goto shmemerror;
	} else ::shmctl( Shminfo[idx].shmid,IPC_RMID,0 );
	static int firstTime=1;
	if (firstTime) {
	    MSG_V( "Sharing memory.\n" );
	    firstTime=0;
	}
    } else {
	shmemerror:
	_Shmem_Flag=0;
#endif
	myximage[idx]=::XGetImage(mDisplay,window,0,0,w,h,AllPlanes,ZPixmap );
#ifdef HAVE_SHM
    }
#endif
}

void X11_System::freeMyXImage(unsigned idx)
{
#ifdef HAVE_SHM
    if ( _Shmem_Flag ) {
	::XShmDetach( mDisplay,&Shminfo[idx] );
	XDestroyImage( myximage[idx] );
	::shmdt( Shminfo[idx].shmaddr );
    } else
#endif
    {
	XDestroyImage( myximage[idx] );
    }
    myximage[idx]=NULL;
}

void X11_System::put_image(XImage* image,const vo_rect_t& r) const
{
#ifdef HAVE_SHM
    if( Shmem_Flag()) {
	::XShmPutImage(	mDisplay,window,gc,image,
			r.x,r.y,r.w,r.h,
			image->width,image->height,True );
    }
    else
#endif
    {
	::XPutImage( mDisplay,window,gc,image,
			r.x,r.y,r.w,r.h,
			image->width,image->height);
    }

}

void X11_System::hidecursor ( ) const
{
    ::Cursor no_ptr;
    ::Pixmap bm_no;
    ::XColor black,dummy;
    ::Colormap colormap;
    static char bm_no_data[] = { 0,0,0,0, 0,0,0,0  };

    colormap = DefaultColormap(mDisplay,DefaultScreen(mDisplay));
    ::XAllocNamedColor(mDisplay,colormap,"black",&black,&dummy);
    bm_no = ::XCreateBitmapFromData(mDisplay, window, bm_no_data, 8,8);
    no_ptr=::XCreatePixmapCursor(mDisplay, bm_no, bm_no,&black, &black,0, 0);
    ::XDefineCursor(mDisplay,window,no_ptr);
}


int X11_System::find_depth_from_visuals(Visual **visual_return) const
{
  XVisualInfo visual_tmpl;
  XVisualInfo *visuals;
  int nvisuals, i;
  int bestvisual = -1;
  int bestvisual_depth = -1;

  visual_tmpl.screen = mScreen;
  visual_tmpl.c_class = TrueColor;
  visuals = ::XGetVisualInfo(mDisplay,
			   VisualScreenMask | VisualClassMask, &visual_tmpl,
			   &nvisuals);
  if (visuals != NULL) {
    for (i = 0; i < nvisuals; i++) {
	MSG_V("vo: X11 truecolor visual %#x, depth %d, R:%lX G:%lX B:%lX\n",
	       visuals[i].visualid, visuals[i].depth,
	       visuals[i].red_mask, visuals[i].green_mask,
	       visuals[i].blue_mask);
	/*
	* save the visual index and it's depth, if this is the first
	* truecolor visul, or a visual that is 'preferred' over the
	* previous 'best' visual
	*/
      if (bestvisual_depth == -1
	  || (visuals[i].depth >= 15
	  && (visuals[i].depth < bestvisual_depth
		  || bestvisual_depth < 15))) {
	bestvisual = i;
	bestvisual_depth = visuals[i].depth;
      }
    }

    if (bestvisual != -1 && visual_return != NULL)
	*visual_return = visuals[bestvisual].visual;

    ::XFree(visuals);
  }
  return bestvisual_depth;
}

#ifdef XF86XK_AudioPause
static void __FASTCALL__ vo_x11_putkey_ext(int keysym){
 switch ( keysym )
  {
#ifdef XF86XK_Standby
   case XF86XK_Standby:       mplayer_put_key(KEY_XF86_STANDBY); break;
#endif
#ifdef XF86XK_PowerOff
   case XF86XK_PowerOff:
#endif
#ifdef XF86XK_PowerDown
   case XF86XK_PowerDown:     mplayer_put_key(KEY_XF86_POWER); break;
#endif
#ifdef XF86XK_AudioPausue
   case XF86XK_AudioPause:    mplayer_put_key(KEY_XF86_PAUSE); break;
#endif
#ifdef XF86XK_Stop
   case XF86XK_Stop:
#endif
#ifdef XF86XK_AudioStop
   case XF86XK_AudioStop:     mplayer_put_key(KEY_XF86_STOP); break;
#endif
#ifdef XF86XK_AudioPlay
   case XF86XK_AudioPlay:     mplayer_put_key(KEY_XF86_PLAY); break;
#endif
#ifdef XF86XK_AudioPrev
   case XF86XK_AudioPrev:     mplayer_put_key(KEY_XF86_PREV); break;
#endif
#ifdef XF86XK_AudioNext
   case XF86XK_AudioNext:     mplayer_put_key(KEY_XF86_NEXT); break;
#endif
#ifdef XF86XK_AudioRaiseVolume
   case XF86XK_AudioRaiseVolume: mplayer_put_key(KEY_XF86_VOLUME_UP); break;
#endif
#ifdef XF86XK_AudioLowerVolume
   case XF86XK_AudioLowerVolume: mplayer_put_key(KEY_XF86_VOLUME_DN); break;
#endif
#ifdef XF86XK_AudioMute
   case XF86XK_AudioMute:     mplayer_put_key(KEY_XF86_MUTE); break;
#endif
#ifdef XF86XK_AudioMedia
   case XF86XK_AudioMedia:    mplayer_put_key(KEY_XF86_MENU); break;
#endif
#ifdef XF86XK_Eject
   case XF86XK_Eject:     mplayer_put_key(KEY_XF86_EJECT); break;
#endif
#ifdef XF86XK_Back
   case XF86XK_Back:     mplayer_put_key(KEY_XF86_REWIND); break;
#endif
#ifdef XF86XK_Forward
   case XF86XK_Forward:     mplayer_put_key(KEY_XF86_FORWARD); break;
#endif
#ifdef XF86XK_BrightnessAdjust
   case XF86XK_BrightnessAdjust:     mplayer_put_key(KEY_XF86_BRIGHTNESS); break;
#endif
#ifdef XF86XK_ContrastAdjust
   case XF86XK_ContrastAdjust:     mplayer_put_key(KEY_XF86_CONTRAST); break;
#endif
#ifdef XF86XK_ScreenSaver
   case XF86XK_ScreenSaver:     mplayer_put_key(KEY_XF86_SCREENSAVE); break;
#endif
#ifdef XF86XK_Refresh
   case XF86XK_Refresh:     mplayer_put_key(KEY_XF86_REFRESH); break;
#endif
   default: break;
  }
}
#endif

static void __FASTCALL__ vo_x11_putkey(int key){
 switch ( key )
  {
   case wsLeft:      mplayer_put_key(KEY_LEFT); break;
   case wsRight:     mplayer_put_key(KEY_RIGHT); break;
   case wsUp:        mplayer_put_key(KEY_UP); break;
   case wsDown:      mplayer_put_key(KEY_DOWN); break;
   case wsSpace:     mplayer_put_key(' '); break;
   case wsEscape:    mplayer_put_key(KEY_ESC); break;
   case wsEnter:     mplayer_put_key(KEY_ENTER); break;
   case wsBackSpace: mplayer_put_key(KEY_BS); break;
   case wsDelete:    mplayer_put_key(KEY_DELETE); break;
   case wsInsert:    mplayer_put_key(KEY_INSERT); break;
   case wsHome:      mplayer_put_key(KEY_HOME); break;
   case wsEnd:       mplayer_put_key(KEY_END); break;
   case wsPageUp:    mplayer_put_key(KEY_PAGE_UP); break;
   case wsPageDown:  mplayer_put_key(KEY_PAGE_DOWN); break;
   case wsF1:        mplayer_put_key(KEY_F+1); break;
   case wsF2:        mplayer_put_key(KEY_F+2); break;
   case wsF3:        mplayer_put_key(KEY_F+3); break;
   case wsF4:        mplayer_put_key(KEY_F+4); break;
   case wsF5:        mplayer_put_key(KEY_F+5); break;
   case wsF6:        mplayer_put_key(KEY_F+6); break;
   case wsF7:        mplayer_put_key(KEY_F+7); break;
   case wsF8:        mplayer_put_key(KEY_F+8); break;
   case wsF9:        mplayer_put_key(KEY_F+9); break;
   case wsF10:       mplayer_put_key(KEY_F+10); break;
   case wsq:
   case wsQ:         mplayer_put_key('q'); break;
   case wsp:
   case wsP:         mplayer_put_key('p'); break;
   case wsMinus:
   case wsGrayMinus: mplayer_put_key('-'); break;
   case wsPlus:
   case wsGrayPlus:  mplayer_put_key('+'); break;
   case wsGrayMul:
   case wsMul:       mplayer_put_key('*'); break;
   case wsGrayDiv:
   case wsDiv:       mplayer_put_key('/'); break;
   case wsLess:      mplayer_put_key('<'); break;
   case wsMore:      mplayer_put_key('>'); break;
   case wsGray0:     mplayer_put_key(KEY_KP0); break;
   case wsGrayEnd:
   case wsGray1:     mplayer_put_key(KEY_KP1); break;
   case wsGrayDown:
   case wsGray2:     mplayer_put_key(KEY_KP2); break;
   case wsGrayPgDn:
   case wsGray3:     mplayer_put_key(KEY_KP3); break;
   case wsGrayLeft:
   case wsGray4:     mplayer_put_key(KEY_KP4); break;
   case wsGray5Dup:
   case wsGray5:     mplayer_put_key(KEY_KP5); break;
   case wsGrayRight:
   case wsGray6:     mplayer_put_key(KEY_KP6); break;
   case wsGrayHome:
   case wsGray7:     mplayer_put_key(KEY_KP7); break;
   case wsGrayUp:
   case wsGray8:     mplayer_put_key(KEY_KP8); break;
   case wsGrayPgUp:
   case wsGray9:     mplayer_put_key(KEY_KP9); break;
   case wsGrayDecimal: mplayer_put_key(KEY_KPDEC); break;
   case wsGrayInsert: mplayer_put_key(KEY_KPINS); break;
   case wsGrayDelete: mplayer_put_key(KEY_KPDEL); break;
   case wsGrayEnter: mplayer_put_key(KEY_KPENTER); break;
   case wsm:
   case wsM:	     mplayer_put_key('m'); break;
   case wso:
   case wsO:         mplayer_put_key('o'); break;
   default: if((key>='a' && key<='z')||(key>='A' && key<='Z')||
	       (key>='0' && key<='9')) mplayer_put_key(key);
  }
}
// ----- Motif header: -------

#define MWM_HINTS_FUNCTIONS     (1L << 0)
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_HINTS_INPUT_MODE    (1L << 2)
#define MWM_HINTS_STATUS        (1L << 3)

#define MWM_FUNC_ALL            (1L << 0)
#define MWM_FUNC_RESIZE         (1L << 1)
#define MWM_FUNC_MOVE           (1L << 2)
#define MWM_FUNC_MINIMIZE       (1L << 3)
#define MWM_FUNC_MAXIMIZE       (1L << 4)
#define MWM_FUNC_CLOSE          (1L << 5)

#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
#define MWM_DECOR_RESIZEH       (1L << 2)
#define MWM_DECOR_TITLE         (1L << 3)
#define MWM_DECOR_MENU          (1L << 4)
#define MWM_DECOR_MINIMIZE      (1L << 5)
#define MWM_DECOR_MAXIMIZE      (1L << 6)

#define MWM_INPUT_MODELESS 0
#define MWM_INPUT_PRIMARY_APPLICATION_MODAL 1
#define MWM_INPUT_SYSTEM_MODAL 2
#define MWM_INPUT_FULL_APPLICATION_MODAL 3
#define MWM_INPUT_APPLICATION_MODAL MWM_INPUT_PRIMARY_APPLICATION_MODAL

#define MWM_TEAROFF_WINDOW      (1L<<0)

// Note: always d==0 !
void X11_System::decoration(int d)
{
    if(vo_conf.fsmode&1){
	XSetWindowAttributes attr;
	attr.override_redirect = True;
	::XChangeWindowAttributes(mDisplay, window, CWOverrideRedirect, &attr);
    }

    if(vo_conf.fsmode&8){
	::XSetTransientForHint (mDisplay, window, RootWindow(mDisplay,mScreen));
    }

    MotifHints=::XInternAtom(mDisplay,"_MOTIF_WM_HINTS",0 );
    if ( MotifHints != None ) {
	memset( &MotifWmHints,0,sizeof(MotifWmHints));
	MotifWmHints.flags=MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS;
	if(d) {
	    MotifWmHints.functions=MWM_FUNC_MOVE | MWM_FUNC_CLOSE | MWM_FUNC_MINIMIZE | MWM_FUNC_MAXIMIZE;
	    d=MWM_DECOR_ALL;
	}
	MotifWmHints.decorations=d|((vo_conf.fsmode&2)?MWM_DECOR_MENU:0);
	::XChangeProperty( mDisplay,window,MotifHints,MotifHints,32,
			PropModeReplace,(unsigned char *)&MotifWmHints,(vo_conf.fsmode&4)?4:5 );
    }
}

void X11_System::classhint(const char *name) const
{
    XClassHint wmClass;
    wmClass.res_name = const_cast<char*>(name);
    wmClass.res_class = const_cast<char*>("MPlayerXP");
    ::XSetClassHint(mDisplay,window,&wmClass);
}

const char *evt_names[] = {
"KeyPress",
"KeyRelease",
"ButtonPress",
"ButtonRelease",
"MotionNotify",
"EnterNotify",
"LeaveNotify",
"FocusIn",
"FocusOut",
"KeymapNotify",
"Expose",
"GraphicsExpose",
"NoExpose",
"VisibilityNotify",
"CreateNotify",
"DestroyNotify",
"UnmapNotify",
"MapNotify",
"MapRequest",
"ReparentNotify",
"ConfigureNotify",
"ConfigureRequest",
"GravityNotify",
"ResizeRequest",
"CirculateNotify",
"CirculateRequest",
"PropertyNotify",
"SelectionClear",
"SelectionRequest",
"SelectionNotify",
"ColormapNotify",
"ClientMessage",
"MappingNotify",
"GenericEvent",
"LASTEvent"
};

static const char * __FASTCALL__ evt_name(unsigned num)
{
    if(num >=2 && num <= 36)	return evt_names[num-2];
    else			return "Unknown";
}

uint32_t X11_System::check_events(vo_data_t*vo,vo_adjust_size_t adjust_size) const
{
    uint32_t ret=0;
    XEvent         Event;
    char           buf[100];
    KeySym         keySym;
    static XComposeStatus stat;
    int adj_ret=0;
    unsigned ow,oh,nw,nh;
    while ( ::XPending( mDisplay ) ) {
	::XNextEvent( mDisplay,&Event );
	MSG_V("X11_common: event_type = %lX (%s)\n",Event.type,evt_name(Event.type));
	switch( Event.type ) {
	    case Expose:
		ret|=VO_EVENT_EXPOSE;
		break;
	    case ConfigureNotify:
		nw = Event.xconfigure.width;
		nh = Event.xconfigure.height;
		if(adjust_size) adj_ret = (*adjust_size)(vo,vo->dest.w,vo->dest.h,&nw,&nh);
		ow = vo->dest.w;
		oh = vo->dest.h;
		vo->dest.w=nw;
		vo->dest.h=nh;
		Window root;
		int ifoo;
		unsigned foo;
		Window win;
		::XGetGeometry(mDisplay, window, &root, &ifoo, &ifoo,
			&foo/*width*/, &foo/*height*/, &foo, &foo);
		::XTranslateCoordinates(mDisplay, window, root, 0, 0,
			reinterpret_cast<int*>(&vo->dest.x),
			reinterpret_cast<int*>(&vo->dest.y), &win);
		if(adjust_size && ow != vo->dest.w && oh != vo->dest.h && adj_ret) {
		    ::XResizeWindow( mDisplay,window,vo->dest.w,vo->dest.h );
		    ::XSync( mDisplay,True);
		}
		MSG_V("X11 Window %dx%d-%dx%d\n", vo->dest.x, vo->dest.y, vo->dest.w, vo->dest.h);
		ret|=VO_EVENT_RESIZE;
		break;
	    case KeyPress: {
		int key;
		::XLookupString( &Event.xkey,buf,sizeof(buf),&keySym,&stat );
#ifdef XF86XK_AudioPause
		::vo_x11_putkey_ext( keySym );
#endif
		key=( (keySym&0xff00) != 0?( (keySym&0x00ff) + 256 ):( keySym ) );
		::vo_x11_putkey( key );
		ret|=VO_EVENT_KEYPRESS;
		break;
		}
	    case ButtonPress:
		// Ignore mouse whell press event
		if(Event.xbutton.button > 3) {
		    mplayer_put_key(MOUSE_BTN0+Event.xbutton.button-1);
		    break;
		}
	    case ButtonRelease:
		mplayer_put_key(MOUSE_BTN0+Event.xbutton.button-1);
	    default:
		break;
	}
    }
    if(ret & VO_EVENT_RESIZE) {
	XSetBackground(mDisplay, gc, 0);
	XClearWindow(mDisplay, window);
    }
    return ret;
}

void X11_System::sizehint(int x, int y, int width, int height) const
{
    XSizeHints hint;
    hint.flags=PPosition | PSize | PWinGravity;
    hint.x=x; hint.y=y; hint.width=width; hint.height=height;
    hint.win_gravity=StaticGravity;
    ::XSetWMNormalHints( mDisplay,window,&hint );
}

void X11_System::calcpos(const vo_data_t*vo,XSizeHints* hint, unsigned d_width, unsigned d_height, unsigned flags )
{
    UNUSED(flags);
#ifdef HAVE_XF86VM
    int modeline_width, modeline_height;
    static uint32_t vm_width;
    static uint32_t vm_height;
#endif
    hint->x=(vo_conf.screenwidth-d_width)/2;
    hint->y=(vo_conf.screenheight-d_height)/2;
    hint->width=d_width;
    hint->height=d_height;
#ifdef HAVE_XF86VM
    if ( vo_VM(vo) ) {
	vm_width=d_width; vm_height=d_height;
	vm_switch(vm_width, vm_height,&modeline_width, &modeline_height);
	hint->x=(vo_conf.screenwidth-modeline_width)/2;
	hint->y=(vo_conf.screenheight-modeline_height)/2;
	hint->width=modeline_width;
	hint->height=modeline_height;
    }
    else
#endif
    if ( vo_FS(vo) ) {
      hint->width=vo_conf.screenwidth;
      hint->height=vo_conf.screenheight;
      hint->x=0;
      hint->y=0;
    }
}

void X11_System::fullscreen(vo_data_t*vo)
{
    ::XUnmapWindow( mDisplay,window );
    if ( !vo_FS(vo) ) {
	vo_FS_SET(vo);
	prev=vo->dest;
	vo->dest.x=0;  vo->dest.y=0; vo->dest.w=vo_conf.screenwidth; vo->dest.h=vo_conf.screenheight;
	decoration(0);
    } else {
	vo_FS_UNSET(vo);
	vo->dest=prev;
	decoration(1);
    }
    sizehint(vo->dest.x,vo->dest.y,vo->dest.w,vo->dest.h );
    ::XMoveResizeWindow( mDisplay,window,vo->dest.x,vo->dest.y,vo->dest.w,vo->dest.h );
    ::XMapWindow( mDisplay,window );
    ::XSync( mDisplay,False );
}

void X11_System::saver_on()
{
#ifdef HAVE_XDPMS
    int nothing;
    if (dpms_disabled) {
	if (DPMSQueryExtension(mDisplay, &nothing, &nothing)) {
	    if (!DPMSEnable(mDisplay)) {  // restoring power saving settings
		MSG_WARN("DPMS not available?\n");
	    } else {
		// DPMS does not seem to be enabled unless we call DPMSInfo
		BOOL onoff;
		CARD16 state;
		DPMSInfo(mDisplay, &state, &onoff);
		if (onoff) {
		    MSG_V ("Successfully enabled DPMS\n");
		} else {
		    MSG_ERR ("Could not enable DPMS\n");
		}
	    }
	}
    }
#endif

    if (timeout_save) {
	int dummy, interval, prefer_blank, allow_exp;
	XGetScreenSaver(mDisplay, &dummy, &interval, &prefer_blank, &allow_exp);
	XSetScreenSaver(mDisplay, timeout_save, interval, prefer_blank, allow_exp);
	XGetScreenSaver(mDisplay, &timeout_save, &interval, &prefer_blank, &allow_exp);
    }
}

void X11_System::saver_off()
{
    int interval, prefer_blank, allow_exp;
#ifdef HAVE_XDPMS
    int nothing;

    if (::DPMSQueryExtension(mDisplay, &nothing, &nothing)) {
	BOOL onoff;
	CARD16 state;
	::DPMSInfo(mDisplay, &state, &onoff);
	if (onoff) {
	    Status stat;
	    MSG_V ("Disabling DPMS\n");
	    dpms_disabled=1;
	    stat = ::DPMSDisable(mDisplay);  // monitor powersave off
	    MSG_V ("stat: %d\n", stat);
	}
    }
#endif
    ::XGetScreenSaver(mDisplay, &timeout_save, &interval, &prefer_blank, &allow_exp);
    if (timeout_save) // turning off screensaver
	::XSetScreenSaver(mDisplay, 0, interval, prefer_blank, allow_exp);
}



#ifdef HAVE_XINERAMA
void X11_System::xinerama_move(const XSizeHints*hint) const
{
    if(::XineramaIsActive(mDisplay))
	::XMoveWindow(mDisplay,window,xinerama_x+hint->x,xinerama_y+hint->y);
}
#endif

#ifdef HAVE_XF86VM
void X11_System::vm_switch(uint32_t X, uint32_t Y, int* modeline_width, int* modeline_height)
{
    int vm_event, vm_error;
    int vm_ver, vm_rev;
    int i,j,have_vm=0;

    int modecount;

    if (::XF86VidModeQueryExtension(mDisplay, &vm_event, &vm_error)) {
	::XF86VidModeQueryVersion(mDisplay, &vm_ver, &vm_rev);
	MSG_V("XF86VidMode Extension v%i.%i\n", vm_ver, vm_rev);
	have_vm=1;
    } else
	MSG_WARN("XF86VidMode Extenstion not available.\n");

    if (have_vm) {
	if (vidmodes==NULL)
	    ::XF86VidModeGetAllModeLines(mDisplay,mScreen,&modecount,&vidmodes);
	    j=0;
	    *modeline_width=vidmodes[0]->hdisplay;
	    *modeline_height=vidmodes[0]->vdisplay;

	for (i=1; i<modecount; i++)
	    if((vidmodes[i]->hdisplay >= X) && (vidmodes[i]->vdisplay >= Y))
		if((vidmodes[i]->hdisplay <= *modeline_width ) && (vidmodes[i]->vdisplay <= *modeline_height)) {
		    *modeline_width=vidmodes[i]->hdisplay;
		    *modeline_height=vidmodes[i]->vdisplay;
		    j=i;
		}

	MSG_V("XF86VM: Selected video mode %dx%d for image size %dx%d.\n",*modeline_width, *modeline_height, X, Y);
	::XF86VidModeLockModeSwitch(mDisplay,mScreen,0);
	::XF86VidModeSwitchToMode(mDisplay,mScreen,vidmodes[j]);
	X=(vo_conf.screenwidth-*modeline_width)/2;
	Y=(vo_conf.screenheight-*modeline_height)/2;
	::XF86VidModeSetViewPort(mDisplay,mScreen,X,Y);
    }
}

void X11_System::vm_close()
{
    if (vidmodes!=NULL) {
	int i, modecount;

	delete vidmodes;
	vidmodes=NULL;
	::XF86VidModeGetAllModeLines(mDisplay,mScreen,&modecount,&vidmodes);
	for (i=0; i<modecount; i++)
	    if ((vidmodes[i]->hdisplay == vo_conf.screenwidth) && (vidmodes[i]->vdisplay == vo_conf.screenheight)) {
		MSG_V("\nReturning to original mode %dx%d\n", vo_conf.screenwidth, vo_conf.screenheight);
		break;
	    }
	::XF86VidModeSwitchToMode(mDisplay,mScreen,vidmodes[i]);
	delete vidmodes;
    }
}
#endif

#ifdef HAVE_XV
#include "img_format.h"
Xv_System::Xv_System(const char* DisplayName)
	    :X11_System(DisplayName) {}
Xv_System::~Xv_System() {}

unsigned Xv_System::query_port(uint32_t format)
{
    XvPortID xv_p;
    unsigned int i,ver,rel,req,ev,err;
    unsigned formats;
    port = 0;
    if (Success == ::XvQueryExtension(get_display(),&ver,&rel,&req,&ev,&err)) {
	/* check for Xvideo support */
	if (Success != ::XvQueryAdaptors(get_display(),DefaultRootWindow(get_display()), &adaptors,&ai)) {
	    MSG_ERR("Xv_System: XvQueryAdaptors failed");
	    return 0;
	}
	/* check priv.adaptors */
	for (i = 0; i < adaptors && port == 0; i++) {
	    if ((ai[i].type & XvInputMask) && (ai[i].type & XvImageMask))
	    for (xv_p = ai[i].base_id; xv_p < ai[i].base_id+ai[i].num_ports; ++xv_p) {
		if (!::XvGrabPort(get_display(), xv_p, CurrentTime)) {
		    port = xv_p;
		    break;
		} else {
		    MSG_ERR("Xv: could not grab port %i\n", (int)xv_p);
		}
	    }
	}
	/* check image priv.formats */
	if (port != 0) {
	    fo = ::XvListImageFormats(get_display(), port, (int*)&formats);
	    if(format==IMGFMT_BGR32) format=IMGFMT_RGB32;
	    if(format==IMGFMT_BGR16) format=IMGFMT_RGB16;
	    format = 0;
	    for(i = 0; i < formats; i++) {
		MSG_V("Xvideo image format: 0x%x (%4.4s) %s\n", fo[i].id,(char*)&fo[i].id, (fo[i].format == XvPacked) ? "packed" : "planar");
		if (fo[i].id == (int)format) format = fo[i].id;
	    }
	    if (!format) port = 0;
	} else format = 0;

	if (port != 0) {
	    MSG_V( "using Xvideo port %d for hw scaling\n",port );
	}
    } else format = 0;
    return format;
}

::XvImage* Xv_System::ImageXv(unsigned idx) const {
    return myxvimage[idx];
}
uint8_t* Xv_System::ImageData(unsigned idx) const {
    return reinterpret_cast<uint8_t*>(Shminfo[idx].shmaddr);
}
void Xv_System::getMyXImage(unsigned idx,Visual *visual,unsigned format,unsigned w,unsigned h)
{
    UNUSED(visual);
    myxvimage[idx]=::XvShmCreateImage(get_display(), port, format, 0, w, h, &Shminfo[idx]);
    Shminfo[idx].readOnly = False;
    Shminfo[idx].shmaddr  = (char *) shmat(Shminfo[idx].shmid, 0, 0);
    ::XShmAttach(get_display(), &Shminfo[idx]);
    sync(False);
    ::shmctl(Shminfo[idx].shmid, IPC_RMID, 0);
}
void Xv_System::freeMyXImage(unsigned idx) {
    ::XShmDetach( get_display(),&Shminfo[idx]);
    ::shmdt( Shminfo[idx].shmaddr );
}

void Xv_System::put_image(XvImage*image,const vo_rect_t& r) const {
    XvShmPutImage(get_display(), port, get_window(), get_gc(), image,
	0, 0,  image->width, image->height,
	r.x,r.y,r.w,r.h,False);
}

int Xv_System::get_video_eq(vo_videq_t *info) const
{
    unsigned i;
    XvAttribute *attributes;
    int howmany, xv_min,xv_max,xv_atomka;
/* get available attributes */
    attributes = ::XvQueryPortAttributes(get_display(), port, &howmany);
    for (i = 0; (int)i < howmany && attributes; i++) {
	if (attributes[i].flags & XvGettable) {
	    xv_min = attributes[i].min_value;
	    xv_max = attributes[i].max_value;
	    xv_atomka = ::XInternAtom(get_display(), attributes[i].name, True);
/* since we have SET_DEFAULTS first in our list, we can check if it's available
   then trigger it if it's ok so that the other values are at default upon query */
	    if (xv_atomka != None) {
		int port_value,port_min,port_max,port_mid,has_value=0;;
		::XvGetPortAttribute(get_display(), port, xv_atomka, &port_value);
		MSG_DBG2("vo_xv: get: %s = %i\n",attributes[i].name,port_value);

		port_min = xv_min;
		port_max = xv_max;
		port_mid = (port_min + port_max) / 2;
		port_value = ((port_value - port_mid)*2000)/(port_max-port_min);

		MSG_DBG2("vo_xv: assume: %s = %i\n",attributes[i].name,port_value);

		if(!strcmp(attributes[i].name,"XV_BRIGHTNESS") && !strcmp(info->name,VO_EC_BRIGHTNESS)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_SATURATION") && !strcmp(info->name,VO_EC_SATURATION)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_CONTRAST") && !strcmp(info->name,VO_EC_CONTRAST)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_HUE") && !strcmp(info->name,VO_EC_HUE)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_RED_INTENSITY") && !strcmp(info->name,VO_EC_RED_INTENSITY)) {
		    /* Note: since 22.01.2002 GATOS supports these attrs for radeons (NK) */
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_GREEN_INTENSITY") && !strcmp(info->name,VO_EC_GREEN_INTENSITY)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_BLUE_INTENSITY") && !strcmp(info->name,VO_EC_BLUE_INTENSITY)) {
		    has_value=1;
		    port_value = info->value;
		}
		else continue;
		if(has_value) return 1;
	    }
	}
    }
    return 0;
}

int Xv_System::set_video_eq(const vo_videq_t *info) const
{
    unsigned i;
    XvAttribute *attributes;
    int howmany, xv_min,xv_max,xv_atomka;
 /* get available attributes */
    attributes = XvQueryPortAttributes(get_display(), port, &howmany);
    for (i = 0; (int)i < howmany && attributes; i++) {
	if (attributes[i].flags & XvSettable) {
	    xv_min = attributes[i].min_value;
	    xv_max = attributes[i].max_value;
	    xv_atomka = XInternAtom(get_display(), attributes[i].name, True);
	    /* since we have SET_DEFAULTS first in our list, we can check if it's available
	    then trigger it if it's ok so that the other values are at default upon query */
	    if (xv_atomka != None) {
		int port_value,port_min,port_max,port_mid,has_value=0;
		if(!strcmp(attributes[i].name,"XV_BRIGHTNESS") && !strcmp(info->name,VO_EC_BRIGHTNESS)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_SATURATION") && !strcmp(info->name,VO_EC_SATURATION)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_CONTRAST") && !strcmp(info->name,VO_EC_CONTRAST)) {
		    has_value=1;
		    port_value = info->value;
		} else
#if 0
/*  We may safely skip this parameter since NVidia driver has default == min
    for XV_HUE but not mid. IMHO it's meaningless against RGB. */
		    if(!strcmp(attributes[i].name,"XV_HUE") && !strcmp(info->name,VO_EC_HUE))
		    {
			has_value=1;
			port_value = info->value;
		    }
		    else
#endif
		    /* Note: since 22.01.2002 GATOS supports these attrs for radeons (NK) */
		    if(!strcmp(attributes[i].name,"XV_RED_INTENSITY") && !strcmp(info->name,VO_EC_RED_INTENSITY)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_GREEN_INTENSITY") && !strcmp(info->name,VO_EC_GREEN_INTENSITY)) {
		    has_value=1;
		    port_value = info->value;
		} else if(!strcmp(attributes[i].name,"XV_BLUE_INTENSITY") && !strcmp(info->name,VO_EC_BLUE_INTENSITY)) {
		    has_value=1;
		    port_value = info->value;
		} else continue;
		if(has_value) {
		    port_min = xv_min;
		    port_max = xv_max;
		    port_mid = (port_min + port_max) / 2;
		    port_value = port_mid + (port_value * (port_max - port_min)) / 2000;
		    MSG_DBG2("vo_xv: set gamma %s to %i (min %i max %i mid %i)\n",attributes[i].name,port_value,port_min,port_max,port_mid);
		    XvSetPortAttribute(get_display(), port, xv_atomka, port_value);
		    return 0;
		}
	    }
	}
    }
    return 1;
}

int Xv_System::reset_video_eq() const
{
    unsigned i;
    XvAttribute *attributes;
    int howmany,xv_atomka;
    int was_reset=0;
 /* get available attributes */
    attributes = XvQueryPortAttributes(get_display(), port, &howmany);
    /* first pass try reset */
    for (i = 0; (int)i < howmany && attributes; i++) {
	if (attributes[i].flags & XvSettable && !strcmp(attributes[i].name,"XV_SET_DEFAULTS")) {
	    was_reset = 1;
	    MSG_DBG2("vo_xv: reset gamma correction\n");
	    xv_atomka = XInternAtom(get_display(), attributes[i].name, True);
	    XvSetPortAttribute(get_display(), port, xv_atomka, attributes[i].max_value);
	}
    }
    return was_reset;
}
#endif

#ifdef HAVE_OPENGL
GLX_System::GLX_System(const char* DisplayName)
	    :X11_System(DisplayName) {}
GLX_System::~GLX_System() {}

void GLX_System::create_context(XVisualInfo* vi) {
    ctx=::glXCreateContext(get_display(), vi, NULL, True);
    if (ctx == NULL) {
	MSG_ERR("[GLX_System]: Can't create GLX context\n");
	exit_player("vo error");
    }
    ::glXMakeCurrent(get_display(), get_window(), NULL);
}

void GLX_System::destroy_context() const
{
    ::glXMakeCurrent(get_display(), None, NULL);
    ::glXDestroyContext(get_display(),ctx);
}

void GLX_System::swap_buffers() const
{
    ::glXSwapBuffers(get_display(), get_window());
}
#endif

#endif