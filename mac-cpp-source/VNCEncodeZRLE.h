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
#include "VNCEncoder.h"

#define TileRaw        0
#define TileSolid      1
#define Tile2Color     2
#define TilePlainRLE 128

class VNCEncodeZRLE {
    public:
		static Size minBufferSize();

        static int begin();
		static void doIdle();
        static Boolean getChunk(int x, int y, int w, int h, unsigned char *&ptr, unsigned long &length);

        static long getEncoding() {return 6;}
};

