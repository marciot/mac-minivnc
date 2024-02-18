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

#include <stdio.h>
#include <string.h>
#include <QDOffscreen.h>

#include "GestaltUtils.h"
#include "VNCServer.h"
#include "VNCTypes.h"
#include "VNCFrameBuffer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "msgbuf.h"

int ShowStatus(const char* format, ...);

const unsigned long &ScrnBase = *(unsigned long*) 0x824;

BitMap vncBits = {0};
//PixPatHandle deskPat;

unsigned long ctSeed;

#ifndef VNC_FB_WIDTH
    unsigned int  fbStride;
    unsigned int  fbWidth;
    unsigned int  fbHeight;
#endif
#ifndef VNC_FB_BITS_PER_PIX
    unsigned long fbDepth;
#endif

VNCPixelFormat     pendingPixFormat;
VNCPixelFormat     fbPixFormat;
unsigned char   *fbUpdateBuffer;

//unsigned long LMGetDeskHook() {return * (unsigned long*) 0xA6C;}

OSErr VNCFrameBuffer::setup() {
    ctSeed = 10;
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

    // Create the color palette
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif

    OSErr err = VNCPalette::setup();
    if (err != noErr)
        return err;

    if(HasColorQD()) {
        //LMSetDeskHook(NULL);
        // Set a solid background
        //deskPat = GetPixPat(128);
        //DetachResource((Handle)deskPat);
        //dprintf("DeskHook: %ld\n", LMGetDeskHook());
        //SetDeskCPat(deskPat);
        //dprintf("DeskHook: %ld\n", LMGetDeskHook());
    }

    copy();
    return noErr;
}

OSErr VNCFrameBuffer::destroy() {
    if(vncBits.baseAddr && vncBits.baseAddr != (Ptr) ScrnBase) {
        DisposePtr((Ptr)vncBits.baseAddr);
        vncBits.baseAddr = 0;
    }
    VNCPalette::destroy();
    if(hasColorQD) {
        //SetDeskCPat(NULL);
        //DisposePixPat(deskPat);
    }
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
        if(!isMatch) {
            dprintf("-This build of Mini VNC will only work at %d x %d with %d colors.", VNC_FB_WIDTH, VNC_FB_HEIGHT, VNC_FB_PALETTE_SIZE);
        }
    #elif defined(VNC_FB_BITS_PER_PIX)
        Boolean isMatch = gdDepth == VNC_FB_BITS_PER_PIX;
        if(!isMatch) {
            dprintf("-This build of Mini VNC will only work with %d colors.", VNC_FB_PALETTE_SIZE);
        }
    #else
        Boolean isMatch = (gdDepth == 1) || (gdDepth == 2) || (gdDepth == 4) || (gdDepth == 8);
        if(!isMatch) {
            dprintf("-Please set your monitor to Black & White, 4, 16 or 256 grays or colors.");
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
    #if defined(VNC_FB_MONOCHROME)
        if(vncBits.baseAddr && vncBits.baseAddr != (Ptr) ScrnBase) {
            // We are on a color Mac, do a dithered copy
            CopyBits(&qd.screenBits, &vncBits, &vncBits.bounds, &vncBits.bounds, srcCopy, NULL);
        }
    #endif
}

void VNCFrameBuffer::idleTask() {
    #if !defined(VNC_FB_MONOCHROME)
        if(hasColorQD) {
            // Find the color table associated with the device
            GDHandle gdh = GetMainDevice();
            PixMapHandle gpx = (*gdh)->gdPMap;
            CTabHandle gct = (*gpx)->pmTable;

            // If the color table has changed, inform the interrupt routine
            if(gct && (*gct)->ctSeed != ctSeed) {
                vncFlags.fbColorMapNeedsUpdate = true;
            }
        }
    #endif
}

void VNCFrameBuffer::fbSyncTasks() {
    if(!vncBits.baseAddr) return;

    #if !defined(VNC_FB_MONOCHROME)

        // Handle any changes to the pixel format

        if (pendingPixFormat.bitsPerPixel) {
            memcpy(&fbPixFormat, &pendingPixFormat, sizeof(VNCPixelFormat));
            pendingPixFormat.bitsPerPixel = 0;
            bytesPerCPixel = fbPixFormat.bitsPerPixel / 8;

            if(fbPixFormat.trueColor) {
                // Determine representation of CPIXEL

                const unsigned long colorBits = ((unsigned long)fbPixFormat.redMax   << fbPixFormat.redShift) |
                                                ((unsigned long)fbPixFormat.greenMax << fbPixFormat.greenShift) |
                                                ((unsigned long)fbPixFormat.blueMax  << fbPixFormat.blueShift);

                if((fbPixFormat.bitsPerPixel == 32) && (colorBits == 0x00FFFFFF)) {
                    bytesPerCPixel = 3;
                }

                dprintf("Bytes per CPIXEL %d (ColorBits: %lx)\n", bytesPerCPixel, colorBits);
            }
            VNCPalette::preparePaletteRoutines();
            vncFlags.fbColorMapNeedsUpdate = true;
        }

        if(!hasColorQD) return;

        // Handle any changes to the color palette

        if (vncFlags.fbColorMapNeedsUpdate) {
            // Find the color table associated with the device
            GDHandle gdh = GetMainDevice();
            PixMapHandle gpx = (*gdh)->gdPMap;
            CTabHandle gct = (*gpx)->pmTable;
            if(gct) {
                #ifdef VNC_FB_BITS_PER_PIX
                    const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
                #endif
                const unsigned int paletteSize = 1 << fbDepth;

                if(paletteSize == ((*gct)->ctSize + 1)) {
                    // Store a copy of the indexed color table so that
                    // the interrupt routine can find it
                    for(unsigned int  i = 0; i < paletteSize; i++) {
                        const RGBColor &rgb = (*gct)->ctTable[i].rgb;
                        VNCPalette::setColor(i, rgb.red, rgb.green, rgb.blue);
                    }
                    ctSeed = (*gct)->ctSeed;
                    // Grab the white and black indices
                    GrafPtr savedPort;
                    GetPort (&savedPort);
                    CGrafPort cPort;
                    OpenCPort(&cPort);
                    VNCPalette::black = cPort.fgColor;
                    VNCPalette::white = cPort.bkColor;
                    CloseCPort(&cPort);
                    SetPort(savedPort);

                    dprintf("Color palette ready (size:%d b:%d w:%d)\n", paletteSize, VNCPalette::black, VNCPalette::white);
                } else {
                    dprintf("Palette size mismatch!\n");
                }
            } else {
                dprintf("Failed to get graphics device!\n");
            }
        } // vncFlags.fbColorMapNeedsUpdate
    #endif
}

unsigned char *VNCFrameBuffer::getBaseAddr() {
    return (unsigned char*) vncBits.baseAddr;
}