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
    EncoderDefer = 0,
    EncoderReady = 1,
    EncoderError = -1
};

struct EncoderPB {
    unsigned int rows, cols;
    unsigned char *src;
    unsigned char *dst;
    unsigned long bytesAvail;
    unsigned long bytesWritten;
};

class VNCEncoder {
    public:
        static OSErr setup();
        static OSErr destroy();
        static OSErr freeMemory();

        static void clear();
        static int begin();
        static void clientEncoding(unsigned long encoding, Boolean hasMore);
        static unsigned long getEncoding();
        static char *getEncoderName(unsigned long encoding);
        static Boolean getUncompressedChunk(EncoderPB &epb);
        static Boolean getChunk(wdsEntry *wds);
        static OSErr fbSyncTasks();

        static int encoderSetup();

        static OSErr compressSetup();
        static void compressBegin();
        static void compressReset();
        static void compressDestroy();

        static unsigned int numOfSubrects();
        static void getSubrect(VNCRect *rect);
        static Boolean isNewSubrect();

        static Boolean getCompressedChunk(EncoderPB &epb);
        static Boolean getCompressedChunk(wdsEntry *wds);
};

extern unsigned char selectedEncoder;
extern unsigned char *fbUpdateBuffer;

#define ALIGN_PAD 3
#define ALIGN_LONG(PTR) (PTR) + (sizeof(unsigned long) - (unsigned long)(PTR) % sizeof(unsigned long))
