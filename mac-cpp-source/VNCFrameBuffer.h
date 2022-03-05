/************************************************************

    VNCFrameBuffer.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 12/14/21

       LAST REVISION: 12/14/21

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#pragma once

#include "VNCConfig.h"
#include "VNCTypes.h"

#ifndef VNC_FB_WIDTH
    extern unsigned int fbStride;
    extern unsigned int fbWidth;
    extern unsigned int fbHeight;
#endif
#ifndef VNC_FB_BITS_PER_PIX
    extern unsigned long fbDepth;
#endif

extern BitMap vncBits;

class VNCFrameBuffer {
    public:
        static OSErr setup();
        static OSErr destroy();
        static void  copy();
        static void  fill();
        static VNCColor *getPalette();
        static unsigned char *getBaseAddr();
        static Boolean checkScreenResolution();
        static Boolean hasColorQuickdraw();

        static unsigned char *getPixelAddr(unsigned int x, unsigned int y) {
            #if !defined(VNC_BYTES_PER_LINE) && !defined(VNC_FB_BITS_PER_PIX)
                return getBaseAddr() + (unsigned long)fbStride * y + x * fbDepth / 8;
            #elif !defined(VNC_BYTES_PER_LINE)
                return getBaseAddr() + (unsigned long)fbStride * y + x/VNC_FB_PIX_PER_BYTE;
            #else
                return getBaseAddr() + (unsigned long)VNC_BYTES_PER_LINE * y + x/VNC_FB_PIX_PER_BYTE;
            #endif
        }
};

