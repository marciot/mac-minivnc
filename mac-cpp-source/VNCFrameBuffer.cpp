/************************************************************

    VNCFrameBuffer.cpp

       AUTHOR: Marcio Luis Teixeira
       CREATED: 12/14/21

       LAST REVISION: 12/14/21

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#include <stdio.h>
#include <string.h>
#include <QDOffscreen.h>

#include "VNCTypes.h"
#include "VNCFrameBuffer.h"
#include "VNCServer.h"

#ifndef USE_STDOUT
    #define printf ShowStatus
    int ShowStatus(const char* format, ...);
#endif

int ShowStatus(const char* format, ...);

const unsigned long &ScrnBase = *(unsigned long*) 0x824;

BitMap vncBits = {0};
PixPatHandle deskPat;

VNCColor *ctColors = 0;
unsigned long ctSeed = 10;

#ifndef VNC_FB_WIDTH
    unsigned int  fbStride;
    unsigned int  fbWidth;
    unsigned int  fbHeight;
#endif
#ifndef VNC_FB_BITS_PER_PIX
    unsigned long fbDepth;
#endif

//unsigned long LMGetDeskHook() {return * (unsigned long*) 0xA6C;}

OSErr VNCFrameBuffer::setup() {
    // Create the color palette
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    const unsigned int paletteSize = 1 << fbDepth;
    ctColors = (VNCColor*) NewPtr(sizeof(VNCColor) * paletteSize);
    if (MemError() != noErr)
        return MemError();

    #if defined(VNC_FB_MONOCHROME)
        ctColors[0].red   = -1;
        ctColors[0].green = -1;
        ctColors[0].blue  = -1;
        ctColors[1].red   = 0;
        ctColors[1].green = 0;
        ctColors[1].blue  = 0;
    #endif

    if(checkScreenResolution()) {
        vncBits.baseAddr = (Ptr) ScrnBase;
    }
    #if defined(VNC_FB_MONOCHROME)
    else {
        // We are on a color Mac, create a virtual B&W map
        vncBits.rowBytes = VNC_FB_WIDTH/8;
        vncBits.baseAddr = NewPtr((unsigned long)VNC_FB_WIDTH/8 * VNC_FB_HEIGHT);
        SetRect(&vncBits.bounds, 0, 0, VNC_FB_WIDTH, VNC_FB_HEIGHT);
        OSErr err = MemError();
        if (err != noErr)
            return err;
    }
    #endif

    if(hasColorQuickdraw()) {
        //LMSetDeskHook(NULL);
        // Set a solid background
        //deskPat = GetPixPat(128);
        //DetachResource((Handle)deskPat);
        //printf("DeskHook: %ld\n", LMGetDeskHook());
        //SetDeskCPat(deskPat);
        //printf("DeskHook: %ld\n", LMGetDeskHook());
    }

    copy();
    return noErr;
}

OSErr VNCFrameBuffer::destroy() {
    if(vncBits.baseAddr && vncBits.baseAddr != (Ptr) ScrnBase) {
        DisposePtr((Ptr)vncBits.baseAddr);
        vncBits.baseAddr = 0;
    }
    if(ctColors) {
        DisposePtr((Ptr)ctColors);
    }
    if(hasColorQuickdraw()) {
        //SetDeskCPat(NULL);
        //DisposePixPat(deskPat);
    }
    return noErr;
}

Boolean VNCFrameBuffer::hasColorQuickdraw() {
    long attr;
    OSErr err = Gestalt(gestaltQuickdrawVersion, &attr);
    return attr >= gestalt8BitQD;
}

Boolean VNCFrameBuffer::checkScreenResolution() {
    // Assume a monochrome screen

    int gdWidth = qd.screenBits.bounds.right;
    int gdHeight = qd.screenBits.bounds.bottom;
    int gdDepth = 1;
    int gdStride = gdWidth/8;

    // Update values if this machine has Color QuickDraw

    if(hasColorQuickdraw()) {
        GDPtr gdp = *GetMainDevice();
        PixMapPtr gpx = *(gdp->gdPMap);

        gdWidth  = gdp->gdRect.right;
        gdHeight = gdp->gdRect.bottom;
        gdDepth  = gpx->pixelSize;
        gdStride = gpx->rowBytes & 0x3FFF;
    }

    #if defined(VNC_FB_WIDTH) && defined(VNC_FB_HEIGHT) && defined(VNC_FB_BITS_PER_PIX)
        Boolean isMatch = gdWidth == VNC_FB_WIDTH && gdHeight == VNC_FB_HEIGHT && gdDepth == VNC_FB_BITS_PER_PIX;
        if(!isMatch) {
            ShowStatus("This build of Mini VNC will only work at %d x %d with %d colors.", VNC_FB_WIDTH, VNC_FB_HEIGHT, VNC_FB_PALETTE_SIZE);
        }
    #elif defined(VNC_FB_BITS_PER_PIX)
        Boolean isMatch = gdDepth == VNC_FB_BITS_PER_PIX;
        if(!isMatch) {
            ShowStatus("This build of Mini VNC will only work with %d colors.", VNC_FB_PALETTE_SIZE);
        }
    #else
        Boolean isMatch = (gdDepth == 1) || (gdDepth == 2) || (gdDepth == 4) || (gdDepth == 8);
        if(!isMatch) {
            ShowStatus("Please set your monitor to Black & White, 4, 16 or 256 grays or colors.");
        }
    #endif

    // Update the constants
    #ifndef VNC_FB_WIDTH
        fbWidth = gdWidth;
        fbHeight = gdHeight;
    #endif
    #ifndef VNC_FB_BITS_PER_PIX
        fbDepth = gdDepth;
        fbStride = gdStride;
    #endif

    return isMatch;
}

void VNCFrameBuffer::copy() {
    if(!vncBits.baseAddr) return;

    #if defined(VNC_FB_MONOCHROME)
        if(vncBits.baseAddr != (Ptr) ScrnBase) {
            // We are on a color Mac, do a dithered copy
            CopyBits(&qd.screenBits, &vncBits, &vncBits.bounds, &vncBits.bounds, srcCopy, NULL);
        }
    #else
        #ifdef VNC_FB_BITS_PER_PIX
            const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
        #endif
        const unsigned int paletteSize = 1 << fbDepth;
        // Update the color table if needed
        GDHandle gdh = GetMainDevice();
        PixMapHandle gpx = (*gdh)->gdPMap;
        CTabHandle gct = (*gpx)->pmTable;
        if(gct && ((*gct)->ctSeed != ctSeed) && (paletteSize == (*gct)->ctSize + 1)) {
            for(int i = 0; i < paletteSize; i++) {
                ctColors[i].red   = (*gct)->ctTable[i].rgb.red;
                ctColors[i].green = (*gct)->ctTable[i].rgb.green;
                ctColors[i].blue  = (*gct)->ctTable[i].rgb.blue;
            }
            ctSeed = (*gct)->ctSeed;
            fbColorMapNeedsUpdate = true;
        }
    #endif
}

unsigned char *VNCFrameBuffer::getBaseAddr() {
    return (unsigned char*) vncBits.baseAddr;
}

VNCColor *VNCFrameBuffer::getPalette() {
    return ctColors;
}