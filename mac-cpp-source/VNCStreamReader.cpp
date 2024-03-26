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

#include "VNCStreamReader.h"

StreamReader inStream;

void StreamReader::setPosition(unsigned short pos) {
    position = pos;
    for (rdsPtr = myRDS; rdsPtr->length != 0; rdsPtr++) {
        if (pos >= rdsPtr->length) {
            pos -= rdsPtr->length;
        } else {
            src     = rdsPtr->ptr    + pos;
            rdsLeft = rdsPtr->length - pos;
            break;
        }
    }
}

/*
 * Returns a pointer to a contiguous block of unread
 * data and populates length with the bytes available.
 *
 * On the 68000, returns a pointer only if the data
 * word aligned.
 */
void const *StreamReader::getAlignedBlock(size_t *length) {
    if (hasColorQD || (((unsigned long)src & 0x01) == 0)) {
        *length = rdsLeft;
        return src;
    } else {
        return 0;
    }
}

size_t StreamReader::copyTo(void *dst, size_t len) {
    size_t bytesCopied = 0;
    while (rdsLeft) {
        const size_t bytesToCopy = min(len - bytesCopied, rdsLeft);
        if (dst) {
            BlockMove(src, dst, bytesToCopy);
            (Ptr)dst += bytesToCopy;
        }
        src         += bytesToCopy;
        bytesCopied += bytesToCopy;
        rdsLeft     -= bytesToCopy;
        position    += bytesToCopy;

        if (bytesCopied == len) {
            break;
        }

        // Advance to the next rds
        if (rdsPtr->length != 0) {
            rdsPtr++;
            src = rdsPtr->ptr;
            rdsLeft = rdsPtr->length;
        }
    }
    return bytesCopied;
}