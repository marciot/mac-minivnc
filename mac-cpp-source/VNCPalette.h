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

extern unsigned char bytesPerCPixel;

class VNCPalette {
    public:
        static unsigned char black, white;

        static OSErr setup();
        static OSErr destroy();

        static void setColor(unsigned int i, int red, int green, int blue);
        static VNCColor *getPalette();

        static void preparePaletteRoutines();
        static unsigned char *emitTrueColorPixel(unsigned char *dst, unsigned char color);
        static unsigned char *emitTrueColorCPIXEL(unsigned char *dst, unsigned char color);

        #define emitColor(A,B)  {if (!fbPixFormat.trueColor) {*A++ = B;} else {A = VNCPalette::emitTrueColorPixel(A, B);}  }
        #define emitCPIXEL(A,B) {if (!fbPixFormat.trueColor) {*A++ = B;} else {A = VNCPalette::emitTrueColorCPIXEL(A, B);} }
};