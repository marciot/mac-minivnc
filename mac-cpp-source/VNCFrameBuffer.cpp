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
#include "msgbuf.h"

int ShowStatus(const char* format, ...);

const unsigned long &ScrnBase = *(unsigned long*) 0x824;

BitMap vncBits = {0};
//PixPatHandle deskPat;

ColorTableEntry *ctColors = 0;
unsigned char ctBlack, ctWhite, cPixelBytes;
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

    const unsigned int paletteSize = 1 << fbDepth;
    ctColors = (ColorTableEntry*) NewPtr(sizeof(VNCColor) * paletteSize);
    if (MemError() != noErr)
        return MemError();

    if(fbDepth == 1) {
        // Set up the monochrome palette
        ctColors[0].vncColor.red   = -1;
        ctColors[0].vncColor.green = -1;
        ctColors[0].vncColor.blue  = -1;
        ctColors[1].vncColor.red   = 0;
        ctColors[1].vncColor.green = 0;
        ctColors[1].vncColor.blue  = 0;
    }

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
    if(ctColors) {
        DisposePtr((Ptr)ctColors);
        ctColors = 0;
    }
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
    #if !defined(VNC_FB_MONOCHROME)
        if(!vncBits.baseAddr) return;
        if(!hasColorQD) return;

        // Handle any changes to the pixel format

        if (pendingPixFormat.bitsPerPixel) {
            memcpy(&fbPixFormat, &pendingPixFormat, sizeof(VNCPixelFormat));
            pendingPixFormat.bitsPerPixel = 0;
            cPixelBytes = fbPixFormat.bitsPerPixel / 8;

            // Determine representation of CPIXEL

            const unsigned long colorBits = ((unsigned long)fbPixFormat.redMax   << fbPixFormat.redShift) |
                                            ((unsigned long)fbPixFormat.greenMax << fbPixFormat.greenShift) |
                                            ((unsigned long)fbPixFormat.blueMax  << fbPixFormat.blueShift);

            if((fbPixFormat.bitsPerPixel == 32) && (colorBits == 0x00FFFFFF)) {
                cPixelBytes = 3;
            } else {
                cPixelBytes = fbPixFormat.bitsPerPixel / 8;
            }

            dprintf("Bytes per CPIXEL %d (ColorBits: %lx)\n", cPixelBytes, colorBits);

            vncFlags.fbColorMapNeedsUpdate = true;
        }

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
                    for(int i = 0; i < paletteSize; i++) {
                        const RGBColor &src = (*gct)->ctTable[i].rgb;
                        if(fbPixFormat.trueColor) {
                            unsigned long r = ((unsigned long)src.red)   * fbPixFormat.redMax   / 0xFFFF;
                            unsigned long g = ((unsigned long)src.green) * fbPixFormat.greenMax / 0xFFFF;
                            unsigned long b = ((unsigned long)src.blue)  * fbPixFormat.blueMax  / 0xFFFF;
                            unsigned long color = (r << fbPixFormat.redShift) | (g << fbPixFormat.greenShift) | (b << fbPixFormat.blueShift);
                            if(fbPixFormat.bigEndian) {
                                ctColors[i].packedColor = color <<= (sizeof(unsigned long) - cPixelBytes) * 8;
                            } else {
                                ctColors[i].packedColor = ((color & 0x000000ff) << 24u) |
                                                          ((color & 0x0000ff00) << 8u)  |
                                                          ((color & 0x00ff0000) >> 8u)  |
                                                          ((color & 0xff000000) >> 24u);
                            }
                        } else {
                            ctColors[i].vncColor.red   = src.red;
                            ctColors[i].vncColor.green = src.green;
                            ctColors[i].vncColor.blue  = src.blue;
                        }
                    }
                    ctSeed = (*gct)->ctSeed;

                    // Grab the white and black indices
                    GrafPtr savedPort;
                    GetPort (&savedPort);
                    CGrafPort cPort;
                    OpenCPort(&cPort);
                    ctBlack = cPort.fgColor;
                    ctWhite = cPort.bkColor;
                    CloseCPort(&cPort);
                    SetPort(savedPort);

                    dprintf("Color palette ready (size:%d b:%d w:%d)\n", paletteSize, ctBlack, ctWhite);
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

VNCColor *VNCFrameBuffer::getPalette() {
    return (VNCColor*) ctColors;
}