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
#include <string.h>

#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCEncodeRAW.h"

int line;

Size VNCEncodeRaw::minBufferSize() {
    return 0;
}

int VNCEncodeRaw::begin() {
    line = 0;
    return EncoderOk;
}

Boolean VNCEncodeRaw::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
    wds->length = w;
    wds->ptr = (Ptr) VNCFrameBuffer::getPixelAddr(x, y + line);
    return ++line < h;
}
