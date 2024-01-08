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
#include "VNCScreenToRLE.h"
#include "VNCFrameBuffer.h"
#include "VNCEncodeHextile.h"
#include "msgbuf.h"

#include <stdlib.h>

#define UPDATE_BUFFER_SIZE 1040
#define UPDATE_MAX_TILES   7

extern int tile_x, tile_y;
unsigned int lastBg, lastFg;

Size VNCEncodeHextile::minBufferSize() {
        return UPDATE_BUFFER_SIZE;
}

int VNCEncodeHextile::begin() {
    tile_x = 0;
    tile_y = 0;
    lastBg = lastFg = -1;
    return EncoderOk;
}

#if !defined(VNC_FB_MONOCHROME)
    #define Raw                 1
    #define BackgroundSpecified 2
    #define ForegroundSpecified 4
    #define AnySubrects         8
    #define SubrectsColored     16

    struct Subrect {
        unsigned char c;
        unsigned char x;
        unsigned char y;
        unsigned char w;
        unsigned char b;

        inline unsigned char h() {return b - y - 1;};
        inline void h(unsigned char h) {b = y + h + 1;};
    };



    #define RLE_LOOP_START(src, len)                                        \
        for (unsigned char *c = (src), *end = (src) + (len); c != end;) {   \
            unsigned char color = *c++;                                     \
            unsigned char count = *c++;
    #define RLE_LOOP_END }

    inline Boolean mergeTop(Subrect *sRects, const Subrect *lastRect, unsigned char *cRects) {
        Subrect *candidate = &sRects[cRects[lastRect->x]];
        // Make this a single return statement so the compiler can inline it!
        return ((lastRect->y == candidate->b) && // Can we merge with this rectangle?
                (lastRect->c == candidate->c) &&
                (lastRect->w == candidate->w) &&
                (lastRect != sRects)          &&
               // If so, grow the found rectangle
               (candidate->b++, true));
    }

    static unsigned short encodeTile(const unsigned char *src, unsigned char *dst, char rows) {
        int len = writeScreenTileAsRLE(src, dst + 1, 0, rows, 16) + 1;

        struct RLEPair {
            unsigned char color;
            unsigned char count;
        } *rle;

        // Create a list of subrects to cover the tile

        unsigned char histogram[256] = {};

        Subrect sRects[256];
        Subrect *lastRect = sRects;
        rle = (RLEPair*) (dst + 1);

        unsigned char cRects[16] = {0};
        unsigned char pixels = rows * 16 - 1;
        unsigned char rleCount = rle->count;
        unsigned char rleColor = rle->color;
        do {
            const unsigned char x = 15 - (pixels & 0x0F);
            const unsigned char y = (rows - 1) - (pixels >> 4);
            if((rleCount > 15) && (x == 0)) {
                const unsigned char h = rleCount / 16;
                lastRect->c = rleColor;
                lastRect->x = 0;
                lastRect->y = y;
                lastRect->w = 15;
                lastRect->h(h - 1);
                pixels   -= h * 16;
                rleCount -= h * 16;
            } else {
                const unsigned char w = min(rleCount, 15 - x);
                lastRect->c = rleColor;
                lastRect->x = x;
                lastRect->y = y;
                lastRect->w = w;
                lastRect->h(0);
                pixels   -= w + 1;
                rleCount -= w + 1;
            }
            if (!mergeTop(sRects, lastRect, cRects)) {
                cRects[x] = lastRect - sRects;  // Record subrect in column lookup table
                histogram[ rleColor ]++;        // Tally color
                lastRect++;                     // Move to next tile
            }
            // Move to the next RLE element
            if (rleCount == 255) {
                rle++;
                rleColor = rle->color;
                rleCount = rle->count;
            }
        } while(pixels != 255);

        // Generate a histogram of rectangles in the tile

        unsigned char bgColor = 0;
        unsigned char bgCount = 0;
        unsigned char nColors = 0;
        unsigned char fgColor;
        for(unsigned int i = 0; i < 256; i++) {
            if(histogram[i]) {
                if(histogram[i] >= bgCount) {
                    fgColor = bgColor;
                    bgColor = i;
                    bgCount = histogram[i];
                } else {
                    fgColor = i;
                }
                nColors++;
            }
        }

        // Figure out how many rects are non-background

        const unsigned int nRects = lastRect - sRects;
        const unsigned int nFgRects = nRects - bgCount;

        // Figure out colors
        const unsigned char bytesPerPixel = fbPixFormat.bitsPerPixel >> 3;
        const unsigned int  rawTileSize   = 1 + 256 * bytesPerPixel;

        //#define DEBUG_SUBRECTS
        #ifdef  DEBUG_SUBRECTS
            // Paint all subrects different colors for verification
            if( rawTileSize > 2 + bytesPerPixel * 2 + nFgRects * 2 ) {
                unsigned char *tile = dst;
                *dst++ = BackgroundSpecified | AnySubrects | SubrectsColored;
                emitColor(dst, bgColor);
                *dst++ = nFgRects;
                for(int i = 0; i < nRects; i++) {
                    if(sRects[i].c != bgColor) {
                        emitColor(dst, sRects[i].c + i * 130);
                        *dst++ = (sRects[i].x << 4) | sRects[i].y;
                        *dst++ = (sRects[i].w << 4) | sRects[i].h();
                    }
                }
                return dst - tile;
            } else {
                // Emit a raw tile
                return len;
            }
        #endif

        // Emit a solid tile if all 256 pixels are equal
        if(nColors == 1) {
            unsigned char *tile = dst;
            *dst++ = 0;
            if(lastBg != bgColor) {
                *tile = BackgroundSpecified;
                emitColor(dst, bgColor);
                lastBg = bgColor;
                return dst - tile;
            } else {
                return 1;
            }
        }

        // Emit two-colored tiles with subrects
        if( (nColors == 2) && ((((unsigned int)2) + bytesPerPixel * 2 + nFgRects * 2) <= rawTileSize) ) {
            unsigned char *tile = dst;
            *dst++ = AnySubrects;
            if(lastBg != bgColor) {
                *tile |= BackgroundSpecified;
                emitColor(dst, bgColor);
                lastBg = bgColor;
            }
            if(lastFg != fgColor) {
                *tile |= ForegroundSpecified;
                emitColor(dst, fgColor);
                lastFg = fgColor;
            }
            *dst++ = nFgRects;
            for(int i = 0; i < nRects; i++) {
                if(sRects[i].c != bgColor) {
                    *dst++ = (sRects[i].x << 4) | sRects[i].y;
                    *dst++ = (sRects[i].w << 4) | sRects[i].h();
                }
            }
            return dst - tile;
        }

        // Emit multi-colored tiles with subrects
        if((nColors > 2) && ((((unsigned int)2) + bytesPerPixel + (nFgRects * (2 + bytesPerPixel))) <= rawTileSize) ) {
            unsigned char *tile = dst;
            *dst++ = AnySubrects | SubrectsColored;
            if(lastBg != bgColor) {
                *tile |= BackgroundSpecified;
                emitColor(dst, bgColor);
                lastBg = bgColor;
            }
            *dst++ = nFgRects;
            for(int i = 0; i < nRects; i++) {
                if(sRects[i].c != bgColor) {
                    emitColor(dst, sRects[i].c);
                    *dst++ = (sRects[i].x << 4) | sRects[i].y;
                    *dst++ = (sRects[i].w << 4) | sRects[i].h();
                }
            }
            lastFg = -1;
            return dst - tile;
        }

        // Else emit a raw tile

        // Move data to end of buffer to make way for color expansion
        unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
        BlockMove(dst, copyTo, len);

        // Rewrite the tile with expanded colors
        unsigned char *tile = dst;
        *dst++ = Raw;
        RLE_LOOP_START(copyTo+1, len-1)
            for(int i = count + 1; i; i--)
                emitColor(dst, color);
            if(dst >= c) dprintf("Overrrun!\n");
        RLE_LOOP_END

        lastBg = lastFg = -1;

        return dst - tile;

    }

    Boolean VNCEncodeHextile::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
        unsigned char *dst = fbUpdateBuffer, *src, rows, tiles = UPDATE_MAX_TILES;

        const unsigned char bytesPerPixel = fbPixFormat.bitsPerPixel >> 3;
        const unsigned int  maxTileSize   = 1 + 256 * bytesPerPixel;

        goto beginLoop;

        while(tiles-- && (dst - fbUpdateBuffer <= (UPDATE_BUFFER_SIZE - maxTileSize))) {
            dst += encodeTile(src, dst, rows);

            // Advance to next tile in row
            #ifdef VNC_FB_BITS_PER_PIX
                src += BYTES_PER_TILE_ROW;
            #else
                src += 2 * fbDepth;
            #endif
            tile_x += 16;
            if (tile_x < w)
                continue;

            // Prepare for the next row
            tile_y += 16;
            if (tile_y >= h)
                break;
            tile_x = 0;

        beginLoop:
            rows = min(16, h - tile_y);
            src = VNCFrameBuffer::getPixelAddr(x + tile_x, y + tile_y);
        }

        wds->length = dst - fbUpdateBuffer;
        wds->ptr = (Ptr) fbUpdateBuffer;

        return tile_y < h;
    }
#endif
