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

#include "VNCTypes.h"

extern unsigned char bytesPerColor;
extern VNCPixelFormat fbPixFormat;

class VNCPalette {
    public:
        static unsigned char black, white;

        static OSErr setup();
        static OSErr destroy();

        static Size minBufferSize();

        static Boolean hasChangesPending();
        static Boolean hasWaitingColorMapUpdate();
        static VNCColor *getWaitingColorMapUpdate(unsigned int *paletteSize);

        static OSErr fbSyncTasks();
        static void idleTask();

        static void beginNewSession(const VNCPixelFormat &format);
        static void setPixelFormat(const VNCPixelFormat &format);

        static void setIndexedColor(unsigned int i, int red, int green, int blue);

        static void prepareColorRoutines(Boolean isCPIXEL);

        static void checkColorTable();
        static OSErr updateColorTable();

        static void prepareTrueColorRoutines(Boolean isCPIXEL);
        static unsigned char *emitTrueColor(unsigned char *dst, unsigned char color);

        #define setupPIXEL()   {VNCPalette::prepareColorRoutines(false);}
        #define setupCPIXEL()  {VNCPalette::prepareColorRoutines(true);}

        #define emitColor(A,B)  {if (!fbPixFormat.trueColor) {*A++ = B;} else {A = VNCPalette::emitTrueColor(A, B);}  }
};