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

extern unsigned long ctSeed;

unsigned char VNCPalette::black, VNCPalette::white, bytesPerColor;
VNCPixelFormat fbPixFormat;
VNCPixelFormat pendingPixFormat;

unsigned long *vncTrueColors = 0;

OSErr VNCPalette::setup() {
    ctSeed = 10;
    return noErr;
}

OSErr VNCPalette::destroy() {
    if (vncTrueColors) {
        DisposePtr((Ptr)vncTrueColors);
        vncTrueColors = 0;
    }
    return noErr;
}

Size VNCPalette::minBufferSize() {
    return 256 * sizeof(VNCColor);
}

void VNCPalette::beginNewSession(const VNCPixelFormat &format) {
    pendingPixFormat.bitsPerPixel = 0;
    BlockMove(&format, &fbPixFormat, sizeof(VNCPixelFormat));
    vncFlags.fbColorMapNeedsUpdate = true;
}

void VNCPalette::setPixelFormat(const VNCPixelFormat &format) {
    BlockMove(&format, &pendingPixFormat, sizeof(VNCPixelFormat));
}

Boolean VNCPalette::hasChangesPending() {
    return VNCPalette::hasWaitingColorMapUpdate() || pendingPixFormat.bitsPerPixel;
}

Boolean VNCPalette::hasWaitingColorMapUpdate() {
    return vncFlags.fbColorMapNeedsUpdate && !fbPixFormat.trueColor;
}

VNCColor *VNCPalette::getWaitingColorMapUpdate(unsigned int *paletteSize) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    VNCColor *result = NULL;
    if (hasWaitingColorMapUpdate()) {
        *paletteSize = 1 << fbDepth;
        result = (VNCColor*) fbUpdateBuffer;
    }
    vncFlags.fbColorMapNeedsUpdate = false;
    return result;
}

void VNCPalette::setIndexedColor(unsigned int i, int red, int green, int blue) {
    VNCColor *vncColors = (VNCColor *)fbUpdateBuffer;
    vncColors[i].red   = red;
    vncColors[i].green = green;
    vncColors[i].blue  = blue;
}

void VNCPalette::idleTask() {
    #if !defined(VNC_FB_MONOCHROME)
        if (hasColorQD) {
            checkColorTable();
        }
    #endif
}

OSErr VNCPalette::fbSyncTasks() {
    if (!vncBits.baseAddr) return noErr;

    // Handle any changes to the pixel format

    if (!hasColorQD
        #if defined(VNC_FB_MONOCHROME)
            || true
        #endif
    ) {
        if (vncFlags.fbColorMapNeedsUpdate) {
            // Set up the monochrome palette
            setIndexedColor(0, -1, -1, -1);
            setIndexedColor(1,  0,  0,  0);
            VNCPalette::white = 0;
            VNCPalette::black = 1;
        }
        return noErr;
    }

    #if !defined(VNC_FB_MONOCHROME)
        return updateColorTable();
    #endif
}

void VNCPalette::prepareColorRoutines(Boolean isCPIXEL) {
    bytesPerColor = fbPixFormat.bitsPerPixel / 8;
    if (fbPixFormat.trueColor) {
        prepareTrueColorRoutines(isCPIXEL);
    }
}