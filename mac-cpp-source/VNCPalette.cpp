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

#include "VNCConfig.h"
#include "VNCPalette.h"
#include "VNCFrameBuffer.h"
#include "msgbuf.h"

#include "VNCTypes.h"

union VNCColorTable {
    Ptr           ptr;
    VNCColor      *vncColors;
    unsigned long *packedColors;
};

VNCColorTable ctColors = {0};
unsigned char VNCPalette::black, VNCPalette::white, bytesPerCPixel;
static unsigned char cPixelShift, pixelShift, bytesPerPixel;
static unsigned long cPixelMask, pixelMask;

OSErr VNCPalette::setup() {
    const unsigned int paletteSize = 1 << fbDepth;
    ctColors.ptr = NewPtr(paletteSize * sizeof(VNCColor));
    if (MemError() != noErr)
        return MemError();

    if(fbDepth == 1) {
        // Set up the monochrome palette
        ctColors.vncColors[0].red   = -1;
        ctColors.vncColors[0].green = -1;
        ctColors.vncColors[0].blue  = -1;
        ctColors.vncColors[1].red   = 0;
        ctColors.vncColors[1].green = 0;
        ctColors.vncColors[1].blue  = 0;
        VNCPalette::black = 1;
        VNCPalette::white = 0;
    }
    return noErr;
}

OSErr VNCPalette::destroy() {
    if (ctColors.vncColors) {
        DisposePtr(ctColors.ptr);
        ctColors.ptr = 0;
    }
    return noErr;
}

void VNCPalette::setColor(unsigned int i, int red, int green, int blue) {
    if(fbPixFormat.trueColor) {
        unsigned long r = ((unsigned long)red)   * fbPixFormat.redMax   / 0xFFFF;
        unsigned long g = ((unsigned long)green) * fbPixFormat.greenMax / 0xFFFF;
        unsigned long b = ((unsigned long)blue)  * fbPixFormat.blueMax  / 0xFFFF;
        unsigned long color = (r << fbPixFormat.redShift) | (g << fbPixFormat.greenShift) | (b << fbPixFormat.blueShift);
        if(fbPixFormat.bigEndian) {
            ctColors.packedColors[i] = color;
        } else {
            ctColors.packedColors[i] = ((color & 0x000000ff) << 24u) |
                                       ((color & 0x0000ff00) << 8u)  |
                                       ((color & 0x00ff0000) >> 8u)  |
                                       ((color & 0xff000000) >> 24u);
        }
    } else {
        ctColors.vncColors[i].red   = red;
        ctColors.vncColors[i].green = green;
        ctColors.vncColors[i].blue  = blue;
    }
}

VNCColor *VNCPalette::getPalette() {
    return (VNCColor*) ctColors.vncColors;
}

void VNCPalette::preparePaletteRoutines() {
    cPixelShift = (sizeof(unsigned long) - bytesPerCPixel) * 8;
    pixelShift  = (sizeof(unsigned long) * 8) - fbPixFormat.bitsPerPixel;
    cPixelMask  = (1UL << cPixelShift) - 1;
    pixelMask   = (1UL << pixelShift) - 1 ;
    if (!fbPixFormat.bigEndian) {
        cPixelShift = 0;
        pixelShift  = 0;
    }
    bytesPerPixel = fbPixFormat.bitsPerPixel >> 3;
}

#pragma a6frames off
#pragma optimize_for_size off
#pragma code68020 on

unsigned char *VNCPalette::emitTrueColorPixel(unsigned char *dst, unsigned char color) {
    const unsigned long packed = ctColors.packedColors[color] << pixelShift;
    *(unsigned long *)dst = ((*(unsigned long *)dst ^ packed) & pixelMask) ^ packed;
    return dst + bytesPerPixel;
}

unsigned char *VNCPalette::emitTrueColorCPIXEL(unsigned char *dst, unsigned char color) {
    const unsigned long packed = ctColors.packedColors[color] << cPixelShift;
    *(unsigned long *)dst = (((*(unsigned long *)dst) ^ packed) & cPixelMask) ^ packed;
    return dst + bytesPerCPixel;
}

#pragma optimize_for_size reset
#pragma a6frames reset
#pragma code68020 reset
