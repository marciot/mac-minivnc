/****************************************************************************
 *   MiniVNC (c) 2022-2024 Marcio Teixeira                                  *
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

#include "GestaltUtils.h"
#include "DialogUtils.h"

#include "VNCServer.h"
#include "VNCPalette.h"
#include "VNCFrameBuffer.h"

const unsigned long &ScrnBase = *(unsigned long*) 0x824;

BitMap vncBits = {0};

#ifndef VNC_FB_WIDTH
    unsigned int  fbStride;
    unsigned int  fbWidth;
    unsigned int  fbHeight;
#endif
#ifndef VNC_FB_BITS_PER_PIX
    unsigned long fbDepth;
#endif

OSErr VNCFrameBuffer::setup() {
    if (checkScreenResolution()) {
        vncBits.baseAddr = (Ptr) ScrnBase;
    }
    #if defined(VNC_FB_MONOCHROME)
        else {
            #ifdef VNC_FB_WIDTH
                const unsigned int fbWidth = VNC_FB_WIDTH;
            #endif
            #ifdef VNC_FB_HEIGHT
                const unsigned int fbHeight = VNC_FB_HEIGHT;
            #endif
            // We are on a color Mac, create a virtual B&W map
            vncBits.rowBytes = fbWidth/8;
            vncBits.baseAddr = NewPtr((unsigned long)fbWidth/8 * fbHeight);
            SetRect(&vncBits.bounds, 0, 0, fbWidth, fbHeight);
            OSErr err = MemError();
            if (err != noErr)
                return err;
        }
    #endif

    // Create the color palette

    OSErr err = VNCPalette::setup();
    if (err != noErr)
        return err;

    idleTask();
    return noErr;
}

OSErr VNCFrameBuffer::destroy() {
    if(vncBits.baseAddr && vncBits.baseAddr != (Ptr) ScrnBase) {
        DisposePtr((Ptr)vncBits.baseAddr);
        vncBits.baseAddr = 0;
    }
    VNCPalette::destroy();
    return noErr;
}

Boolean VNCFrameBuffer::checkScreenResolution() {
    // Assume a monochrome screen

    int gdWidth = qd.screenBits.bounds.right;
    int gdHeight = qd.screenBits.bounds.bottom;
    int gdDepth = 1;
    int gdStride = gdWidth/8;

    // Update values if this machine has Color QuickDraw

    if(HasColorQD()) {
        GDPtr gdp = *GetMainDevice();
        PixMapPtr gpx = *(gdp->gdPMap);

        gdWidth  = gdp->gdRect.right;
        gdHeight = gdp->gdRect.bottom;
        gdDepth  = gpx->pixelSize;
        gdStride = gpx->rowBytes & 0x3FFF;
    }

    #if defined(VNC_FB_WIDTH) && defined(VNC_FB_HEIGHT) && defined(VNC_FB_BITS_PER_PIX)
        Boolean isMatch = gdWidth == VNC_FB_WIDTH && gdHeight == VNC_FB_HEIGHT && gdDepth == VNC_FB_BITS_PER_PIX;
        if (!isMatch) {
            ShowAlert('ERR', 128, "This build of Mini VNC will only work at %d x %d with %d colors.", VNC_FB_WIDTH, VNC_FB_HEIGHT, VNC_FB_PALETTE_SIZE);
        }
    #elif defined(VNC_FB_BITS_PER_PIX)
        Boolean isMatch = gdDepth == VNC_FB_BITS_PER_PIX;
        if (!isMatch) {
            ShowAlert('ERR', 128, "This build of Mini VNC will only work with %d colors.", VNC_FB_PALETTE_SIZE);
        }
    #else
        Boolean isMatch = (gdDepth == 1) || (gdDepth == 2) || (gdDepth == 4) || (gdDepth == 8);
        if (!isMatch) {
            ShowAlert('ERR', 128, "Please set your monitor to Black & White, 4, 16 or 256 grays or colors.");
        }
    #endif

    // Update the constants
    #ifndef VNC_FB_WIDTH
        fbWidth  = gdWidth;
        fbHeight = gdHeight;
        fbStride = gdStride;
    #endif
    #ifndef VNC_FB_BITS_PER_PIX
        fbDepth = gdDepth;
    #endif

    return isMatch;
}

void VNCFrameBuffer::idleTask() {
    #if defined(VNC_FB_MONOCHROME)
        if (vncBits.baseAddr && (vncBits.baseAddr != (Ptr) ScrnBase)) {
            // We are on a color Mac, do a dithered copy
            CopyBits(&qd.screenBits, &vncBits, &vncBits.bounds, &vncBits.bounds, srcCopy, NULL);
        }
    #endif
}

unsigned char *VNCFrameBuffer::getBaseAddr() {
    return (unsigned char*) vncBits.baseAddr;
}