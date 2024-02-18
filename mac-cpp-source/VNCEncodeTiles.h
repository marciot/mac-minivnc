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

#define USE_ASM_CODE 1

struct ColorInfo {
    unsigned char colorPal[127];
    unsigned char colorMap[256];
    unsigned int  runsOfOne;
    unsigned int  nColors;
    unsigned char colorSize;
    unsigned char packRuns;
};

unsigned short screenToNative(const unsigned char *src, unsigned char *dst, short rows, short cols, ColorInfo *colorInfo);
unsigned short nativeToRle(const unsigned char *src, unsigned char *end, unsigned char *dst, const unsigned char *stop, unsigned char depth, ColorInfo *cInfo);
unsigned short nativeToPacked(const unsigned char *src, unsigned char *dst, const unsigned char* end, const char inDepth, const char outDepth, ColorInfo *colorInfo);
unsigned short nativeToColors(const unsigned char *start, unsigned char *end, ColorInfo *colorInfo);
