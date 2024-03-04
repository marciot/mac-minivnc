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

#include "DebugLog.h"
#include "GestaltUtils.h"

#include "VNCConfig.h"
#include "VNCServer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"

#include "VNCTypes.h"

static unsigned char pixelShift;
static unsigned long pixelMask;

unsigned long ctSeed;
extern unsigned long *vncTrueColors;

extern VNCPixelFormat pendingPixFormat;

static void setTrueColor(unsigned int i, int red, int green, int blue) {
    const unsigned long r = ((unsigned long)red)   * fbPixFormat.redMax   / 0xFFFF;
    const unsigned long g = ((unsigned long)green) * fbPixFormat.greenMax / 0xFFFF;
    const unsigned long b = ((unsigned long)blue)  * fbPixFormat.blueMax  / 0xFFFF;
    const unsigned long color = (r << fbPixFormat.redShift) | (g << fbPixFormat.greenShift) | (b << fbPixFormat.blueShift);
    if(fbPixFormat.bigEndian) {
        vncTrueColors[i] = color;
    } else {
        vncTrueColors[i] = ((color & 0x000000ff) << 24u) |
                           ((color & 0x0000ff00) << 8u)  |
                           ((color & 0x00ff0000) >> 8u)  |
                           ((color & 0xff000000) >> 24u);
    }
}

void VNCPalette::checkColorTable() {
    // Find the color table associated with the device
    GDHandle gdh = GetMainDevice();
    PixMapHandle gpx = (*gdh)->gdPMap;
    CTabHandle gct = (*gpx)->pmTable;

    // If the color table has changed, inform the interrupt routine
    if(gct && (*gct)->ctSeed != ctSeed) {
        vncFlags.fbColorMapNeedsUpdate = true;
    }
}

OSErr VNCPalette::updateColorTable() {
    #if !defined(VNC_FB_MONOCHROME)
        // Handle any changes to bits per pixel

        if (pendingPixFormat.bitsPerPixel) {
            BlockMove(&pendingPixFormat, &fbPixFormat, sizeof(VNCPixelFormat));
            pendingPixFormat.bitsPerPixel = 0;
            dprintf("Changed pixel format.\n");
            vncFlags.fbColorMapNeedsUpdate = true;
        }

        // Handle any changes to the color palette

        if (vncFlags.fbColorMapNeedsUpdate) {
            #ifdef VNC_FB_BITS_PER_PIX
                const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
            #endif
            const unsigned int nColors = 1 << fbDepth;

            // Allocate the color table, if necessary

            if (fbPixFormat.trueColor && (vncTrueColors == NULL)) {
                const unsigned long size = nColors * sizeof(unsigned long);
                vncTrueColors = (unsigned long *) NewPtr(size);
                if (MemError() != noErr) {
                    dprintf("Failed to allocate true color table\n", size);
                    return MemError();
                }
                dprintf("Reserved %ld bytes for true color table\n", size);
            }

            // Find the color table associated with the device
            GDHandle gdh = GetMainDevice();
            PixMapHandle gpx = (*gdh)->gdPMap;
            CTabHandle gct = (*gpx)->pmTable;
            if(gct) {
                if(nColors == ((*gct)->ctSize + 1)) {
                    // Store a copy of the indexed color table so that
                    // the interrupt routine can find it
                    for(unsigned int i = 0; i < nColors; i++) {
                        const RGBColor &rgb = (*gct)->ctTable[i].rgb;
                        if (fbPixFormat.trueColor) {
                            setTrueColor(i, rgb.red, rgb.green, rgb.blue);
                        } else {
                            setIndexedColor(i, rgb.red, rgb.green, rgb.blue);
                        }
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

                    dprintf("Color palette ready (size:%d b:%d w:%d)\n", nColors, VNCPalette::black, VNCPalette::white);
                } else {
                    dprintf("Palette size mismatch!\n");
                }
            } else {
                dprintf("Failed to get graphics device!\n");
            }
        } // vncFlags.fbColorMapNeedsUpdate
    #endif
    return noErr;
}

void VNCPalette::prepareTrueColorRoutines(Boolean isCPIXEL) {
    if (isCPIXEL) {
        // Determine representation of CPIXEL
        const unsigned long colorBits = ((unsigned long)fbPixFormat.redMax   << fbPixFormat.redShift) |
                                        ((unsigned long)fbPixFormat.greenMax << fbPixFormat.greenShift) |
                                        ((unsigned long)fbPixFormat.blueMax  << fbPixFormat.blueShift);
        if((fbPixFormat.bitsPerPixel == 32) && (colorBits == 0x00FFFFFF)) {
            bytesPerColor = 3;
        }
        //dprintf("Bytes per CPIXEL %d (ColorBits: %lx)\n", bytesPerColor, colorBits);
    }
    pixelShift  = (sizeof(unsigned long) - bytesPerColor)  * 8;
    pixelMask   = (1UL <<  pixelShift) - 1 ;
    if (!fbPixFormat.bigEndian) {
        pixelShift  = 0;
    }
}

#pragma a6frames off
#pragma optimize_for_size off
#pragma code68020 on

unsigned char *VNCPalette::emitTrueColor(unsigned char *dst, unsigned char color) {
    const unsigned long packed = vncTrueColors[color] << pixelShift;
    *(unsigned long *)dst = (((*(unsigned long *)dst) ^ packed) & pixelMask) ^ packed;
    return dst + bytesPerColor;
}

#pragma optimize_for_size reset
#pragma a6frames reset
#pragma code68020 reset
