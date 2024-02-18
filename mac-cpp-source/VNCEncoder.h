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

#pragma once

#include "MacTCP.h"
#include "VNCFrameBuffer.h"

enum {
    EncoderOk = 0,
    EncoderNeedsCompression = 1,
    EncoderDefer = 2,
    EncoderError = -1
};

class VNCEncoder {
    public:
        static OSErr setup();
        static OSErr destroy();
        static void clear();
        static int begin();
        static void clientEncoding(unsigned long encoding, Boolean hasMore);
        static unsigned long getEncoding();
        static char *getEncoderName(unsigned long encoding);
        static Boolean getChunk(int x, int y, int w, int h, wdsEntry *wds);

        static void compressBegin();
        static void compressReset();
        static void fbSyncTasks();

        static Boolean getCompressedChunk(wdsEntry *wds);
};
