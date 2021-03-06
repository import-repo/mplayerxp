#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/* The name speaks for itself this filter is a dummy and will not blow
   up regardless of what you do with it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "af.h"
#include "af_internal.h"
#include "pp_msg.h"

struct af_lp_t {
    unsigned fin;
    unsigned fake_out;
};

static MPXP_Rc __FASTCALL__ config_af(af_instance_t* af,const af_conf_t* arg)
{
    af_lp_t* s   = (af_lp_t*)af->setup;
    memcpy(&af->conf,arg,sizeof(af_conf_t));
    s->fin=af->conf.rate;
    af->delay=(float)s->fake_out/af->conf.rate;
    af->conf.rate=s->fake_out;
    return MPXP_Ok;
}

// Initialization and runtime control_af
static MPXP_Rc __FASTCALL__ control_af(af_instance_t* af, int cmd, any_t* arg)
{
  af_lp_t* s   = (af_lp_t*)af->setup;
  switch(cmd){
  case AF_CONTROL_SHOWCONF:
    mpxp_info<<"[af_lp] in "<<((af_lp_t*)af->setup)->fin<<" faked out "<<((af_lp_t*)af->setup)->fake_out<<std::endl;
    return MPXP_Ok;
  case AF_CONTROL_COMMAND_LINE:{
    sscanf((char*)arg,"%i", &s->fake_out);
    // Sanity check
    if(s->fake_out < 8000 || s->fake_out > 192000){
      mpxp_err<<"[af_lp] The output sample frequency must be between 8kHz and 192kHz. Current value is "<<s->fake_out<<std::endl;
      return MPXP_Error;
    }
    s->fin=af->conf.rate;
    af->delay=(float)s->fake_out/af->conf.rate;
    af->conf.rate=s->fake_out;
    return MPXP_Ok;
  }
  case AF_CONTROL_POST_CREATE:
    s->fin=s->fake_out=af->conf.rate;
    return MPXP_Ok;
  default: break;
  }
  return MPXP_Unknown;
}

// Deallocate memory
static void __FASTCALL__ uninit(af_instance_t* af)
{
    if(af->setup) delete af->setup;
}

// Filter data through filter
static mp_aframe_t __FASTCALL__ play(af_instance_t* af,const mp_aframe_t& data)
{
    // Do something necessary to get rid of annoying warning during compile
    if(!af) mpxp_err<<"EEEK: Argument af == NULL in af_lp.cpp:play()"<<std::endl;
    return data;
}

// Allocate memory and set function pointers
static MPXP_Rc __FASTCALL__ af_open(af_instance_t* af){
  af->config_af=config_af;
  af->control_af=control_af;
  af->uninit=uninit;
  af->play=play;
  af->mul.d=1;
  af->mul.n=1;
  af->setup=new(zeromem) af_lp_t;

    check_pin("afilter",af->pin,AF_PIN);
  return MPXP_Ok;
}

// Description of this filter
extern const af_info_t af_info_lp = {
    "LongPlay audio filer",
    "lp",
    "Nickols_K",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
