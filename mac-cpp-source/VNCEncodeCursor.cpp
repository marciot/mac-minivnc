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

#include "VNCServer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "VNCEncodeCursor.h"

unsigned long cursorChecksum;

void VNCEncodeCursor::clear() {
    // Force an update
    cursorChecksum = !cursorChecksum;
}

Size VNCEncodeCursor::minBufferSize() {
    return sizeof(VNCFBUpdateRect) + 16 * 16 * 4 + 32;
}

Boolean VNCEncodeCursor::needsUpdate() {
    const unsigned long *TheCrsr = (unsigned long*) 0x0844;
    unsigned long sum = 0;
    for(int i = 0; i < 68 / sizeof(unsigned long); i++) {
        sum += TheCrsr[i];
    }
    if(cursorChecksum != sum) {
        cursorChecksum = sum;
        /*#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
        #define BYTE_TO_BINARY(byte)  \
            ((byte) & 0x80 ? '1' : '0'), \
            ((byte) & 0x40 ? '1' : '0'), \
            ((byte) & 0x20 ? '1' : '0'), \
            ((byte) & 0x10 ? '1' : '0'), \
            ((byte) & 0x08 ? '1' : '0'), \
            ((byte) & 0x04 ? '1' : '0'), \
            ((byte) & 0x02 ? '1' : '0'), \
            ((byte) & 0x01 ? '1' : '0')
        unsigned char *TheCrsr = (unsigned char*) 0x0844;
        for(int i = 0; i < 68; i += 2) {
            dprintf(BYTE_TO_BINARY_PATTERN BYTE_TO_BINARY_PATTERN "\n", BYTE_TO_BINARY(TheCrsr[i]), BYTE_TO_BINARY(TheCrsr[i+1]));
        }*/
        return true;
    }
    return false;
}

Boolean VNCEncodeCursor::getChunk(wdsEntry *wds) {
    const unsigned short *TheCrsr = (unsigned short*) 0x0844;
    VNCFBUpdateRect *cursor = (VNCFBUpdateRect*)fbUpdateBuffer;
    cursor->rect.x = TheCrsr[33];
    cursor->rect.y = TheCrsr[32];
    cursor->rect.w = 16;
    cursor->rect.h = 16;
    cursor->encodingType = mCursorEncoding;

    unsigned char *dst = fbUpdateBuffer + sizeof(VNCFBUpdateRect);

    setupPIXEL();

    // Send the pixel values for the cursor
    for(int y = 0; y < 16; y++) {
        unsigned short bits = TheCrsr[y];
        for(int x = 0; x < 16; x++) {
            emitColor(dst, (bits & 0x8000) ? VNCPalette::black : VNCPalette::white);
            bits <<= 1;
        }
    }
    // Send the bitmask; note that the Mac paints the cursor differently
    // than the VNC server does. On the Mac, the mask indicates which screen
    // pixels are erased, then the cursor bitmap is XORed onto the screen.
    // Certain cursors, such as the i-beam, have no mask and are meant to
    // invert the contents of the screen, but would show up blank on the VNC
    // server.To fix this, we combine the mask with the cursor bitmap, which
    // forces it to be painted, albeit without the XOR effect.
    for(int y = 0; y < 16; y++) {
        unsigned short mask = TheCrsr[y + 16] | TheCrsr[y];
        *dst++ = mask >> 8;
        *dst++ = mask & 0xFF;
    }

    wds->length = dst - fbUpdateBuffer;
    wds->ptr = (Ptr) fbUpdateBuffer;
    return 0;
}

void VNCEncodeCursor::adjustCursorVisibility (Boolean allowHiding) {
    static Boolean hidden = false;

    if(vncConfig.hideCursor && vncServerActive()) {
        // If the user tried to move the mouse, unhide it.
        Point mousePosition;
        GetMouse(&mousePosition);
        if((vncLastMousePosition.h != mousePosition.h) ||
           (vncLastMousePosition.h != mousePosition.h)) {
            allowHiding = false;
        }

        if((!hidden) && allowHiding) {
            HideCursor();
            hidden = true;
        }
    }

    if(hidden && !(allowHiding && vncServerActive())) {
        ShowCursor();
        hidden = false;
    }
}

void VNCEncodeCursor::idleTask() {
    adjustCursorVisibility(true);
}