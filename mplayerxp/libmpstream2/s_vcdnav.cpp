#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
    s_vcdnav - libVCD's stream interface (based on xine's input plugin)
*/
#ifdef USE_LIBVCD
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "stream.h"
#include "stream_internal.h"
#include "stream_msg.h"

#include <libvcd/inf.h>
#include <libvcd/logging.h>
#include "mrl.h"
#include "mpxp_help.h"

namespace	usr {
    struct vcdsector_t {
	uint8_t		subheader[CDIO_CD_SUBHEADER_SIZE];
	uint8_t		data[M2F2_SECTOR_SIZE];
	uint8_t		spare[4];
    };

    struct vcd_item_info_t {
	lsn_t	start_LSN;	/* LSN where play item starts */
	size_t	size;		/* size in sector units of play item. */
    };

    class VcdNav_Stream_Interface : public Stream_Interface {
	public:
	    VcdNav_Stream_Interface(libinput_t& libinput);
	    virtual ~VcdNav_Stream_Interface();

	    virtual MPXP_Rc	open(const std::string& filename,unsigned flags);
	    virtual int		read(stream_packet_t * sp);
	    virtual off_t	seek(off_t off);
	    virtual off_t	tell() const;
	    virtual void	close();
	    virtual MPXP_Rc	ctrl(unsigned cmd,any_t* param);
	    virtual Stream::type_e type() const;
	    virtual off_t	start_pos() const;
	    virtual off_t	size() const;
	    virtual off_t	sector_size() const;
	    virtual std::string mime_type() const;
	private:
	    void		inc_lsn();

	    vcdinfo_obj_t*	fd;
	    unsigned		ntracks;
	    vcd_item_info_t*	track;
	    unsigned		nentries;
	    vcd_item_info_t*	entry;
	    unsigned		nsegments;
	    vcd_item_info_t*	segment;
	    unsigned		nlids;
	    lsn_t		start;
	    lsn_t		lsn;
	    unsigned		total;
	/* cache */
	    vcdsector_t		vcd_sector;
	    lsn_t		vcd_sector_lsn;
    };

VcdNav_Stream_Interface::VcdNav_Stream_Interface(libinput_t& libinput)
			:Stream_Interface(libinput) {}
VcdNav_Stream_Interface::~VcdNav_Stream_Interface() {
    vcdinfo_close(fd);
    if(track) delete track;
    if(entry) delete entry;
    if(segment) delete segment;
}

static void __FASTCALL__ _cdio_detect_media(const std::string& device)
{
  CdIo_t *img;
  img=cdio_open(device.c_str(),DRIVER_UNKNOWN);
  if(img)
  {
    discmode_t mode=cdio_get_discmode(img);
    mpxp_hint<<"Detected "<<discmode2str[mode]<<" disk"<<std::endl;
  }
}

MPXP_Rc VcdNav_Stream_Interface::open(const std::string& filename,unsigned flags)
{
    const char *param;
    char *device,*dev;
    unsigned i;
    int vcd_track=-1;
    vcdinfo_open_return_t open_rc;
    UNUSED(flags);
    if(filename=="help") {
	mpxp_hint<<"Usage: vcdnav://<@device><#trackno>"<<std::endl;
	return MPXP_False;
    }
    param=mrl_parse_line(filename,NULL,NULL,&device,NULL);
    if(param) vcd_track=atoi(param);
//    vcdinfo_init(priv->fd);
    if(mp_conf.verbose>1) vcd_loglevel_default=VCD_LOG_DEBUG;
    else if(mp_conf.verbose) vcd_loglevel_default=VCD_LOG_INFO;
    open_rc=vcdinfo_open(&fd,&device,DRIVER_UNKNOWN,NULL);
    if(!fd) {
	dev=DEFAULT_CDROM_DEVICE;
	open_rc=vcdinfo_open(&fd,device?&device:&dev,DRIVER_UNKNOWN,NULL);
	delete device;
	if(!fd) {
	    mpxp_err<<"Can't open stream"<<std::endl;
	    _cdio_detect_media(device?device:dev);
	    return MPXP_False;
	}
    }
    nlids=vcdinfo_get_num_LIDs(fd);
    if(vcdinfo_read_psd(fd)) vcdinfo_visit_lot (fd, false);
    mpxp_dbg2<<"VCDNAV geometry:"<<std::endl;
    if((ntracks=vcdinfo_get_num_tracks(fd))>0) {
	track=new(zeromem) vcd_item_info_t[ntracks];
	for(i=0;i<ntracks;i++) {
	    track[i].size=vcdinfo_get_track_sect_count(fd,i+1);
	    track[i].start_LSN=vcdinfo_get_track_lsn(fd,i+1);
	    mpxp_dbg2<<"track="<<i<<" start="<<track[i].start_LSN<<" size="<<track[i].size<<std::endl;
	}
	start=track[0].start_LSN;
	total=track[i-1].size;
    }
    if((nentries=vcdinfo_get_num_entries(fd))>0) {
	entry=new(zeromem) vcd_item_info_t [nentries];
	for(i=0;i<nentries;i++) {
	    entry[i].size=vcdinfo_get_entry_sect_count(fd,i);
	    entry[i].start_LSN=vcdinfo_get_entry_lsn(fd,i);
	    mpxp_dbg2<<"entry="<<i<<" start="<<entry[i].start_LSN<<" size=%i"<<entry[i].size<<std::endl;
	}
    }
    if((nsegments=vcdinfo_get_num_segments(fd))>0) {
	segment=new(zeromem) vcd_item_info_t[nsegments];
	for(i=0;i<nsegments;i++) {
	    segment[i].size=vcdinfo_get_seg_sector_count(fd,i);
	    segment[i].start_LSN=vcdinfo_get_seg_lsn(fd,i);
	    mpxp_dbg2<<"segment="<<i<<" start="<<segment[i].start_LSN<<" size="<<segment[i].size<<std::endl;
	}
    }
    mpxp_info<<"This VCD contains: tracks="<<ntracks<<" entries="<<nentries<<" segments="<<nsegments<<std::endl;
    if(vcd_track!=-1) {
	if(vcd_track>0 && (unsigned)vcd_track<=ntracks) {
	    start=track[vcd_track-1].start_LSN;
	    total=track[vcd_track-1].size;
	}
	else mpxp_err<<"Wrong track number "<<vcd_track<<" Playing whole VCD!"<<std::endl;
    }
    lsn=start;
    read(NULL); /* Find first non empty segment */
    lsn--;
    start=lsn;
    mpxp_dbg2<<"vcdnav_open start="<<lsn<<" end="<<total<<std::endl;
    return MPXP_Ok;
}
Stream::type_e VcdNav_Stream_Interface::type() const { return Stream::Type_Seekable|Stream::Type_Program; }
off_t	VcdNav_Stream_Interface::start_pos() const { return start*sizeof(vcdsector_t); }
off_t	VcdNav_Stream_Interface::size() const { return (start+total)*sizeof(vcdsector_t); }
off_t	VcdNav_Stream_Interface::sector_size() const { return sizeof(vcdsector_t); }
std::string VcdNav_Stream_Interface::mime_type() const { return "application/octet-stream"; }

void VcdNav_Stream_Interface::inc_lsn()
{
    unsigned i;
    int j=-1;
    for(i=0;i<ntracks;i++) {
	if(lsn >= track[i].start_LSN && lsn < track[i].start_LSN+track[i].size) {
	    j=i;
	    break;
	}
    }
    if(j!=-1 && lsn>=track[j].start_LSN+track[j].size)
	    lsn=(j==ntracks)?lsn+1:track[j+1].start_LSN;
    else lsn++;
}

int VcdNav_Stream_Interface::read(stream_packet_t*sp)
{
    CdIo *img=vcdinfo_get_cd_image(fd);
    mpxp_dbg2<<"vcdnav_read: lsn="<<lsn<<" total="<<total<<std::endl;
    if(sp) sp->type=0;
    do {
	if(lsn!=vcd_sector_lsn) {
	    if (cdio_read_mode2_sector(img, &vcd_sector, lsn, true)!=0) {
		mpxp_err<<"vcdnav: read error"<<std::endl;
		inc_lsn();
		return 0;
	    }
	    mpxp_dbg3<<"LSN="<<lsn<<" SUBHEADER: "
		<<std::hex<<vcd_sector.subheader[0]<<" "
		<<std::hex<<vcd_sector.subheader[1]<<" "
		<<std::hex<<vcd_sector.subheader[2]<<" "
		<<std::hex<<vcd_sector.subheader[3]<<" "
		<<std::hex<<vcd_sector.subheader[4]<<" "
		<<std::hex<<vcd_sector.subheader[5]<<" "
		<<std::hex<<vcd_sector.subheader[6]<<" "
		<<std::hex<<vcd_sector.subheader[7]<<std::endl;
	    mpxp_dbg3<<"DATA: "
		<<std::hex<<vcd_sector.data[0]<<" "
		<<std::hex<<vcd_sector.data[1]<<" "
		<<std::hex<<vcd_sector.data[2]<<" "
		<<std::hex<<vcd_sector.data[3]<<" "
		<<std::hex<<vcd_sector.data[4]<<" "
		<<std::hex<<vcd_sector.data[5]<<" "
		<<std::hex<<vcd_sector.data[6]<<" "
		<<std::hex<<vcd_sector.data[7]<<std::endl;
	    mpxp_dbg3<<"SPARE: "
		<<std::hex<<vcd_sector.spare[0]<<" "
		<<std::hex<<vcd_sector.spare[1]<<" "
		<<std::hex<<vcd_sector.spare[2]<<" "
		<<std::hex<<vcd_sector.spare[3]<<std::endl;
	}
	vcd_sector_lsn=lsn;
	inc_lsn();

	if ( lsn >= start+total ) {
	    /* We've run off of the end of this entry. Do we continue or stop? */
	    mpxp_dbg2<<"end reached in reading, cur: "<<lsn<<", end: "<<total<<std::endl;
	    break;
	}
      /* Check header ID for a padding sector and simply discard
	 these.  It is alleged that VCD's put these in to keep the
	 bitrate constant.
      */
    } while((vcd_sector.subheader[2]&~0x01)==0x60);

    if(sp) { /* sp may be NULL in case of internal usage */
	memcpy (sp->buf, vcd_sector.data, M2F2_SECTOR_SIZE);
	sp->len=M2F2_SECTOR_SIZE;
    }
    return sp?sp->len:0;
}

off_t VcdNav_Stream_Interface::seek(off_t pos)
{
    lsn_t oldlsn=lsn;
    CdIo *img = vcdinfo_get_cd_image(fd);
    lsn=pos/sizeof(vcdsector_t);
    if(lsn < start) lsn=start;
    if(lsn > start+total) lsn=start+total;
    lsn--;
    inc_lsn();
    cdio_lseek(img,lsn,SEEK_SET);
    mpxp_dbg2<<"vcdnav_seek: lsn="<<oldlsn<<" newlsn="<<lsn<<" pos="<<pos<<std::endl;
    return lsn*sizeof(vcdsector_t);
}

off_t VcdNav_Stream_Interface::tell() const
{
    mpxp_dbg2<<"vcdnav_tell: lsn="<<lsn<<std::endl;
    return lsn*sizeof(vcdsector_t);
}

void VcdNav_Stream_Interface::close()
{
    mpxp_dbg2<<"vcdnav_close"<<std::endl;
}

MPXP_Rc VcdNav_Stream_Interface::ctrl(unsigned cmd,any_t*args) {
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

static Stream_Interface* query_interface(libinput_t& libinput) { return new(zeromem) VcdNav_Stream_Interface(libinput); }

extern const stream_interface_info_t vcdnav_stream =
{
    "vcdnav://",
    "reads multimedia stream from libVCD's interface",
    query_interface
};
} // namespace	usr
#endif
