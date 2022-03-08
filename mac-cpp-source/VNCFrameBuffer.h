/****************************************************************************
 *   MiniVNC (c) 2022 Marcio Teixeira                                       *
 *                                                                          *
 *   This program is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation, either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 *                                                                          *
 *   This program is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU General Public License for more details.                           *
 *                                                                          *
 *   To view a copy of the GNU General Public License, go to the following  *
 *   location: <http://www.gnu.org/licenses/>.                              *
 ****************************************************************************/

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

