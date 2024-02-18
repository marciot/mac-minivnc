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
#include "VNCPalette.h"
#include "VNCEncodeTiles.h"
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

    static unsigned short encodeTile(const unsigned char *src, char rows, char cols, unsigned char *dst, unsigned long bytesAvail) {
        unsigned char *start = dst;
        unsigned char nativeTile[256];
        unsigned char *rleTile = dst + 1;

        ColorInfo info;
        info.nColors = 256;
        info.colorSize = 1;
        info.packRuns   = false;
        const unsigned long nativeLen = screenToNative(src, nativeTile, rows, cols, 0);
        const unsigned long len = nativeToRle(nativeTile, nativeTile + nativeLen, rleTile, rleTile + 512, fbDepth, &info) + 1;

        struct RLEPair {
            unsigned char color;
            unsigned char count;
        } *rle;

        // Create a list of subrects to cover the tile

        unsigned char histogram[256] = {};

        Subrect sRects[256];
        Subrect *lastRect = sRects;
        rle = (RLEPair*) rleTile;

        unsigned char cRects[16] = {0};
        unsigned short rleCount = rle->count + 1;
        unsigned char  rleColor = rle->color;
        for(int y = 0; y < rows; y++)
        for(int x = 0; x < cols; ) {
            if((rleCount >= cols) && (x == 0)) {
                const unsigned char h = rleCount / cols;
                lastRect->c = rleColor;
                lastRect->x = 0;
                lastRect->y = y;
                lastRect->w = cols - 1;
                lastRect->h(h - 1);
                rleCount -= h * cols;
                y += h;
                if(y == rows) {
                    cols = 0; // Force inner loop to exit
                }
            } else {
                const unsigned char w = min(rleCount, cols - x);
                lastRect->c = rleColor;
                lastRect->x = x;
                lastRect->y = y;
                lastRect->w = w - 1;
                lastRect->h(0);
                rleCount -= w;
                x        += w;
            }
            if (!mergeTop(sRects, lastRect, cRects)) {
                cRects[x] = lastRect - sRects;  // Record subrect in column lookup table
                histogram[ rleColor ]++;        // Tally color
                lastRect++;                     // Move to next tile
            }
            // Move to the next RLE element
            if (rleCount == 0) {
                rle++;
                rleColor = rle->color;
                rleCount = rle->count + 1;
            }
        }

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

        if (nColors > 1) {
            const unsigned int rawTileLen = 1 + 256 * bytesPerPixel;

            // Emit two-colored tiles with subrects
            if(nColors == 2) {
                const unsigned char emitBgColor = (lastBg != bgColor);
                const unsigned char emitFgColor = (lastFg != fgColor);
                unsigned long twoColorTileLen = ((unsigned int)emitBgColor + emitFgColor) + bytesPerPixel * 2 + nFgRects * 2;
                if ((twoColorTileLen <= rawTileLen) && (twoColorTileLen <= bytesAvail)) {
                    *dst++ = AnySubrects;
                    if(emitBgColor) {
                        *start |= BackgroundSpecified;
                        emitColor(dst, bgColor);
                        lastBg = bgColor;
                    }
                    if(emitFgColor) {
                        *start |= ForegroundSpecified;
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
                    #if USE_SANITY_CHECKS
                        if (twoColorTileLen != (dst - start)) {
                            dprintf("Incorrect tile %d length: %ld != %ld\n", start[0], twoColorTileLen, (dst - start));
                        }
                    #endif
                    return dst - start;
                }
            }

            // Emit multi-colored tiles with subrects
            else {
                const unsigned char emitBgColor = (lastBg != bgColor);
                const unsigned long multiColorTileLen = ((unsigned int)2) + (emitBgColor && bytesPerPixel) + (nFgRects * (2 + bytesPerPixel));
                if ((multiColorTileLen <= rawTileLen) && (multiColorTileLen <= bytesAvail)) {
                    *dst++ = AnySubrects | SubrectsColored;
                    if(lastBg != bgColor) {
                        *start |= BackgroundSpecified;
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
                    #if USE_SANITY_CHECKS
                        if (multiColorTileLen != (dst - start)) {
                            dprintf("Incorrect tile %d length: %ld != %ld\n", start[0], multiColorTileLen, (dst - start));
                        }
                    #endif
                    return dst - start;
                }
            }

            if (rawTileLen <= bytesAvail) {
                // If we get here, emit a raw tile

                // Move data to end of buffer to make way for color expansion
                unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
                BlockMove(dst, copyTo, len);

                // Rewrite the tile with expanded colors
                *dst++ = Raw;
                for (unsigned char *c = copyTo+1, *end = copyTo + len; c != end;) {
                    unsigned char color = *c++, count = *c++ + 1;
                    while(count--) emitColor(dst, color);
                    #if USE_SANITY_CHECKS
                        if(dst >= c) dprintf("Overrrun!\n");
                    #endif
                }

                lastBg = lastFg = -1;
                #if USE_SANITY_CHECKS
                    if (rawTileLen != (dst - start)) {
                        dprintf("Incorrect tile %d length: %ld != %ld\n", start[0], rawTileLen, (dst - start));
                    }
                #endif
                return dst - start;
            }
        }

        // Otherwise emit a solid tile

        *dst++ = 0;
        if(lastBg != bgColor) {
            *start = BackgroundSpecified;
            emitColor(dst, bgColor);
            lastBg = bgColor;
            return dst - start;
        } else {
            return 1;
        }
    }

    Boolean VNCEncodeHextile::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
        unsigned char *dst = fbUpdateBuffer, *src, rows, cols;

        const unsigned char bytesPerPixel = fbPixFormat.bitsPerPixel >> 3;
        const unsigned int  maxTileSize   = 1 + 256 * bytesPerPixel;

        unsigned long bytesAvail = UPDATE_BUFFER_SIZE;

        goto beginLoop;

        while (bytesAvail > maxTileSize) {
            const unsigned long len = encodeTile(src, rows, cols, dst, bytesAvail);
            bytesAvail -= len;
            dst += len;

            // Advance to next tile in row
            #ifdef VNC_FB_BITS_PER_PIX
                src += BYTES_PER_TILE_ROW;
            #else
                src += 2 * fbDepth;
            #endif
            tile_x += 16;
            cols = min(16, w - tile_x);
            if (tile_x < w)
                continue;

            // Prepare for the next row
            tile_y += 16;
            if (tile_y >= h)
                break;
            tile_x = 0;

        beginLoop:
            cols = min(16, w - tile_x);
            rows = min(16, h - tile_y);
            src = VNCFrameBuffer::getPixelAddr(x + tile_x, y + tile_y);
        }

        wds->length = dst - fbUpdateBuffer;
        wds->ptr = (Ptr) fbUpdateBuffer;

        return tile_y < h;
    }
#endif
