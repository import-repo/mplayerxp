#ifndef __ASF_H
#define __ASF_H

//#include "cp_config.h"	/* for WORDS_BIGENDIAN */
#include <inttypes.h>
#include "win32sdk/mmreg.h"
#include "win32sdk/avifmt.h"
#include "win32sdk/vfw.h"
#include "osdep/bswap.h"

///////////////////////
// MS GUID definition
///////////////////////
#ifndef GUID_DEFINED
#define GUID_DEFINED
// Size of GUID is 16 bytes!
typedef struct __attribute__((packed)) {
	uint32_t	Data1;		// 4 bytes
	uint16_t	Data2;		// 2 bytes
	uint16_t	Data3;		// 2 bytes
	uint8_t		Data4[8];	// 8 bytes
} GUID_t;
#endif

///////////////////////
// ASF Object Header
///////////////////////
typedef struct __attribute__((packed)) {
  uint8_t guid[16];
  uint64_t size;
} ASF_obj_header_t;

////////////////
// ASF Header
////////////////
typedef struct __attribute__((packed)) {
  ASF_obj_header_t objh;
  uint32_t cno; // number of subchunks
  uint8_t v1; // unknown (0x01)
  uint8_t v2; // unknown (0x02)
} ASF_header_t;

/////////////////////
// ASF File Header
/////////////////////
typedef struct __attribute__((packed)) {
  uint8_t stream_id[16]; // stream GUID
  uint64_t file_size;
  uint64_t creation_time; //File creation time FILETIME 8
  uint64_t num_packets;    //Number of packets UINT64 8
  uint64_t play_duration; //Timestamp of the end position UINT64 8
  uint64_t send_duration;  //Duration of the playback UINT64 8
  uint64_t preroll; //Time to bufferize before playing UINT32 4
  uint32_t flags; //Unknown, maybe flags ( usually contains 2 ) UINT32 4
  uint32_t min_packet_size; //Min size of the packet, in bytes UINT32 4
  uint32_t max_packet_size; //Max size of the packet  UINT32 4
  uint32_t max_bitrate; //Maximum bitrate of the media (sum of all the stream)
} ASF_file_header_t;

///////////////////////
// ASF Stream Header
///////////////////////
typedef struct __attribute__((packed)) {
  uint8_t type[16]; // Stream type (audio/video) GUID 16
  uint8_t concealment[16]; // Audio error concealment type GUID 16
  uint64_t unk1; // Unknown, maybe reserved ( usually contains 0 ) UINT64 8
  uint32_t type_size; //Total size of type-specific data UINT32 4
  uint32_t stream_size; //Size of stream-specific data UINT32 4
  uint16_t stream_no; //Stream number UINT16 2
  uint32_t unk2; //Unknown UINT32 4
} ASF_stream_header_t;

///////////////////////////
// ASF Content Description
///////////////////////////
typedef struct  __attribute__((packed)) {
  uint16_t title_size;
  uint16_t author_size;
  uint16_t copyright_size;
  uint16_t comment_size;
  uint16_t rating_size;
} ASF_content_description_t;

////////////////////////
// ASF Segment Header
////////////////////////
typedef struct __attribute__((packed)) {
  uint8_t streamno;
  uint8_t seq;
  uint32_t x;
  uint8_t flag;
} ASF_segmhdr_t;

//////////////////////
// ASF Stream Chunck
//////////////////////
typedef struct __attribute__((packed)) {
	uint16_t	type;
	uint16_t	size;
	uint32_t	sequence_number;
	uint16_t	unknown;
	uint16_t	size_confirm;
} ASF_stream_chunck_t;

// Definition of the stream type
#ifdef WORDS_BIGENDIAN
enum {
    ASF_STREAMING_CLEAR	=0x2443,// $C
    ASF_STREAMING_DATA	=0x2444,// $D
    ASF_STREAMING_END_TRANS=0x2445,// $E
    ASF_STREAMING_HEADER=0x2448// $H
};
#else
enum {
    ASF_STREAMING_CLEAR	=0x4324,// $C
    ASF_STREAMING_DATA	=0x4424,// $D
    ASF_STREAMING_END_TRANS=0x4524,// $E
    ASF_STREAMING_HEADER=0x4824// $H
};
#endif

/*
 * Some macros to swap little endian structures read from an ASF file
 * into machine endian format
 */
#ifdef WORDS_BIGENDIAN
static inline void le2me_ASF_obj_header_t(ASF_obj_header_t*h) { h->size = le2me_64(h->size); }
static inline void le2me_ASF_header_t(ASF_header_t*h) {
    le2me_ASF_obj_header_t(&h->objh);
    h->cno = le2me_32(h->cno);
}
static inline void le2me_ASF_stream_header_t(ASF_stream_header_t*h) {
    h->unk1 = le2me_64(h->unk1);
    h->type_size = le2me_32(h->type_size);
    h->stream_size = le2me_32(h->stream_size);
    h->stream_no = le2me_16(h->stream_no);
    h->unk2 = le2me_32(h->unk2);
}
static inline void le2me_ASF_file_header_t(ASF_file_header_t*h) {
    h->file_size = le2me_64(h->file_size);
    h->creation_time = le2me_64(h->creation_time);
    h->num_packets = le2me_64(h->num_packets);
    h->play_duration = le2me_64(h->play_duration);
    h->send_duration = le2me_64(h->send_duration);
    h->preroll = le2me_64(h->preroll);
    h->flags = le2me_32(h->flags);
    h->min_packet_size = le2me_32(h->min_packet_size);
    h->max_packet_size = le2me_32(h->max_packet_size);
    h->max_bitrate = le2me_32(h->max_bitrate);
}
static inline void le2me_ASF_content_description_t(ASF_content_description_t*h) {
    h->title_size = le2me_16(h->title_size);
    h->author_size = le2me_16(h->author_size);
    h->copyright_size = le2me_16(h->copyright_size);
    h->comment_size = le2me_16(h->comment_size);
    h->rating_size = le2me_16(h->rating_size);
}
static inline void le2me_BITMAPINFOHEADER(BITMAPINFOHEADER*h) {
    h->biSize = le2me_32(h->biSize);
    h->biWidth = le2me_32(h->biWidth);
    h->biHeight = le2me_32(h->biHeight);
    h->biPlanes = le2me_16(h->biPlanes);
    h->biBitCount = le2me_16(h->biBitCount);
    h->biCompression = le2me_32(h->biCompression);
    h->biSizeImage = le2me_32(h->biSizeImage);
    h->biXPelsPerMeter = le2me_32(h->biXPelsPerMeter);
    h->biYPelsPerMeter = le2me_32(h->biYPelsPerMeter);
    h->biClrUsed = le2me_32(h->biClrUsed);
    h->biClrImportant = le2me_32(h->biClrImportant);
}
static inline void le2me_WAVEFORMATEX(WAVEFORMATEX*h) {
    h->wFormatTag = le2me_16(h->wFormatTag);
    h->nChannels = le2me_16(h->nChannels);
    h->nSamplesPerSec = le2me_32(h->nSamplesPerSec);
    h->nAvgBytesPerSec = le2me_32(h->nAvgBytesPerSec);
    h->nBlockAlign = le2me_16(h->nBlockAlign);
    h->wBitsPerSample = le2me_16(h->wBitsPerSample);
    h->cbSize = le2me_16(h->cbSize);
}
static inline void le2me_ASF_stream_chunck_t(ASF_stream_chunck_t*h) {
    h->size = le2me_16(h->size);
    h->sequence_number = le2me_32(h->sequence_number);
    h->unknown = le2me_16(h->unknown);
    h->size_confirm = le2me_16(h->size_confirm);
}
#else
static inline void le2me_ASF_obj_header_t(ASF_obj_header_t*h) { UNUSED(h);}
static inline void le2me_ASF_header_t(ASF_header_t*h) { UNUSED(h);}
static inline void le2me_ASF_stream_header_t(ASF_stream_header_t*h){ UNUSED(h);}
static inline void le2me_ASF_file_header_t(ASF_file_header_t*h){ UNUSED(h);}
static inline void le2me_ASF_content_description_t(ASF_content_description_t*h){ UNUSED(h);}
static inline void le2me_BITMAPINFOHEADER(BITMAPINFOHEADER*h){ UNUSED(h);}
static inline void le2me_WAVEFORMATEX(WAVEFORMATEX*h){ UNUSED(h);}
static inline void le2me_ASF_stream_chunck_t(ASF_stream_chunck_t*h){ UNUSED(h);}
#endif

#endif
