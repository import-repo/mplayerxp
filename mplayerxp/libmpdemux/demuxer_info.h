#ifndef __DEMUXER_INFO_H_INCLUDED
#define __DEMUXER_INFO_H_INCLUDED 1
#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include "xmpcore/xmp_enums.h"
#include <string>

namespace	usr {
    enum {
	INFOT_NULL		=0,
	INFOT_AUTHOR		=1,
	INFOT_NAME		=2,
	INFOT_SUBJECT		=3,
	INFOT_COPYRIGHT		=4,
	INFOT_DESCRIPTION	=5,
	INFOT_ALBUM		=6,
	INFOT_DATE		=7,
	INFOT_TRACK		=8,
	INFOT_GENRE		=9,
	INFOT_ENCODER		=10,
	INFOT_SOURCE_MEDIA	=11,
	INFOT_WWW		=12,
	INFOT_MAIL		=13,
	INFOT_RATING		=14,
	INFOT_COMMENTS		=15,
	INFOT_MAX		=15
    };

    struct Demuxer_Info : public Opaque {
	public:
	    Demuxer_Info();
	    virtual ~Demuxer_Info();

	    MPXP_Rc	add(unsigned opt, const char *param);
	    const char*	get(unsigned opt) const;
	    int		print(const std::string& filename) const;

	private:
	    char *id[INFOT_MAX];
    };
} // namespace	usr
#endif
