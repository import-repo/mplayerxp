/*
 * Modified for use with MPlayer, detailed changelog at
 * http://svn.mplayerhq.hu/mplayer/trunk/
 * $Id: driver.c,v 1.3 2006/09/13 08:13:07 nickols_k Exp $
 */

#include "mpxp_config.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef __FreeBSD__
#include <sys/time.h>
#endif

#include "win32.h"
#include "wine/driver.h"
#include "wine/pe_image.h"
#include "wine/winreg.h"
#include "wine/vfw.h"
#include "registry.h"
#ifdef ENABLE_WIN32LOADER
#include "ldt_keeper.h"
#endif
#include "driver.h"
#ifndef __MINGW32__
#include "ext.h"
#endif


#ifndef ENABLE_WIN32LOADER
char* def_path=WIN32_PATH;
#else
extern char* def_path;
#endif

#if 1

/*
 * STORE_ALL/REST_ALL seems like an attempt to workaround problems due to
 * WINAPI/no-WINAPI bustage.
 *
 * There should be no need for the STORE_ALL/REST_ALL hack once all
 * function definitions agree with their prototypes (WINAPI-wise) and
 * we make sure, that we do not call these functions without a proper
 * prototype in scope.
 */

#define STORE_ALL
#define REST_ALL
#else
// this asm code is no longer needed
#define STORE_ALL \
    __asm__ __volatile__ ( \
    "push %%ebx\n\t" \
    "push %%ecx\n\t" \
    "push %%edx\n\t" \
    "push %%esi\n\t" \
    "push %%edi\n\t"::)

#define REST_ALL \
    __asm__ __volatile__ ( \
    "pop %%edi\n\t" \
    "pop %%esi\n\t" \
    "pop %%edx\n\t" \
    "pop %%ecx\n\t" \
    "pop %%ebx\n\t"::)
#endif

static int needs_free=0;
void SetCodecPath(const char* path)
{
    if(needs_free)mp_free(def_path);
    if(path==0)
    {
	def_path=WIN32_PATH;
	needs_free=0;
	return;
    }
    def_path = (char*) mp_malloc(strlen(path)+1);
    strcpy(def_path, path);
    needs_free=1;
}

static DWORD dwDrvID = 0;

LRESULT WINAPI SendDriverMessage(HDRVR hDriver, UINT message,
				 LPARAM lParam1, LPARAM lParam2)
{
    DRVR* module=(DRVR*)hDriver;
    int result;
#ifndef __svr4__
    char qw[300];
#endif
#ifdef DETAILED_OUT
    printf("SendDriverMessage: driver %X, message %X, arg1 %X, arg2 %X\n", hDriver, message, lParam1, lParam2);
#endif
    if (!module || !module->hDriverModule || !module->DriverProc) return -1;
#ifndef __svr4__
    __asm__ __volatile__ ("fsave (%0)\n\t": :"r"(&qw));
#endif

#ifdef ENABLE_WIN32LOADER
    Setup_FS_Segment();
#endif

    STORE_ALL;
    result=module->DriverProc(module->dwDriverID, hDriver, message, lParam1, lParam2);
    REST_ALL;

#ifndef __svr4__
    __asm__ __volatile__ ("frstor (%0)\n\t": :"r"(&qw));
#endif

#ifdef DETAILED_OUT
    printf("\t\tResult: %X\n", result);
#endif
    return result;
}

void DrvClose(HDRVR hDriver)
{
    if (hDriver)
    {
	DRVR* d = (DRVR*)hDriver;
	if (d->hDriverModule)
	{
#ifdef ENABLE_WIN32LOADER
	    Setup_FS_Segment();
#endif
	    if (d->DriverProc)
	    {
		SendDriverMessage(hDriver, DRV_CLOSE, 0, 0);
		d->dwDriverID = 0;
		SendDriverMessage(hDriver, DRV_FREE, 0, 0);
	    }
	    FreeLibrary(d->hDriverModule);
	}
	mp_free(d);
    }
#ifdef ENABLE_WIN32LOADER
    CodecRelease();
#endif
}

#ifndef TRACE
#define TRACE(...)
#endif

//DrvOpen(LPCSTR lpszDriverName, LPCSTR lpszSectionName, LPARAM lParam2)
HDRVR DrvOpen(LPARAM lParam2)
{
    NPDRVR hDriver;
    int i;
    char unknown[0x124];
    const char* filename = (const char*) ((ICOPEN*) lParam2)->pV1Reserved;

#ifdef MPLAYER
#ifdef ENABLE_WIN32LOADER
    Setup_LDT_Keeper();
#endif
    printf("Loading codec DLL: '%s'\n",filename);
#endif

    hDriver = (NPDRVR) mp_mallocz(sizeof(DRVR));
    if (!hDriver)
	return ((HDRVR) 0);

#ifdef ENABLE_WIN32LOADER
    CodecAlloc();
    Setup_FS_Segment();
#endif

    hDriver->hDriverModule = LoadLibraryA(filename);
    if (!hDriver->hDriverModule)
    {
	printf("Can't open library %s\n", filename);
	DrvClose((HDRVR)hDriver);
	return ((HDRVR) 0);
    }

    hDriver->DriverProc = (DRIVERPROC) GetProcAddress(hDriver->hDriverModule,
						      "DriverProc");
    if (!hDriver->DriverProc)
    {
	printf("Library %s is not a valid VfW/ACM codec\n", filename);
	DrvClose((HDRVR)hDriver);
	return ((HDRVR) 0);
    }

    TRACE("DriverProc == %X\n", hDriver->DriverProc);
    SendDriverMessage((HDRVR)hDriver, DRV_LOAD, 0, 0);
    TRACE("DRV_LOAD Ok!\n");
    SendDriverMessage((HDRVR)hDriver, DRV_ENABLE, 0, 0);
    TRACE("DRV_ENABLE Ok!\n");
    hDriver->dwDriverID = ++dwDrvID; // generate new id

    // open driver and remmeber proper DriverID
    hDriver->dwDriverID = SendDriverMessage((HDRVR)hDriver, DRV_OPEN, (LPARAM) unknown, lParam2);
    TRACE("DRV_OPEN Ok!(%X)\n", hDriver->dwDriverID);

    printf("Loaded DLL driver %s at %x\n", filename, hDriver->hDriverModule);
    return (HDRVR)hDriver;
}