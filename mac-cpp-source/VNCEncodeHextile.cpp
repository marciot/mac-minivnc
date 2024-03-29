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

#include "VNCServer.h"
#include "VNCPalette.h"
#include "VNCEncodeTiles.h"
#include "VNCEncodeHextile.h"

#include "DebugLog.h"

#define UPDATE_BUFFER_SIZE 1040
#define UPDATE_MAX_TILES   7
#define DEBUG_SUBRECTS     0

#define src32 ((unsigned long*)src)

unsigned int lastBg, lastFg;

Size VNCEncodeHextile::minBufferSize() {
    return UPDATE_BUFFER_SIZE;
}

void VNCEncodeHextile::begin() {
    lastBg = lastFg = -1;
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
                (lastRect->x == candidate->x) &&
                (lastRect != sRects)          &&
               // If so, grow the found rectangle
               (candidate->b++, true));
    }

    unsigned long VNCEncodeHextile::encodeSolidTile(const EncoderPB &epb) {
        const unsigned char mask  = ((1 << fbDepth) - 1);
        const unsigned char color = epb.src[0] & mask;
        unsigned char *dst = epb.dst;
        *dst++ = BackgroundSpecified;
        setupPIXEL();
        emitColor(dst, color);
        return dst - epb.dst;
    }

    unsigned long VNCEncodeHextile::encodeTile(const EncoderPB &epb) {
        unsigned char *start = epb.dst;
        unsigned char *dst = epb.dst;
        unsigned char *src = epb.src;

        unsigned char scratchSpace[768 +  ALIGN_PAD];
        unsigned char *nativeTile = ALIGN_LONG(scratchSpace);
        unsigned char    *rleTile = nativeTile + 256;

        setupPIXEL();

        ColorInfo info;
        info.nColors = 256;
        info.colorSize = 1;
        info.packRuns = false;
        const unsigned long nativeLen = screenToNative(epb.src, nativeTile, epb.rows, epb.cols, 0);
        const unsigned long len = nativeToRle(nativeTile, nativeTile + nativeLen, rleTile, rleTile + 512, fbDepth, &info);

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
        for(int y = 0; y < epb.rows; y++)
        for(int x = 0; x < epb.cols; ) {
            if((rleCount >= epb.cols) && (x == 0)) {
                const unsigned char h = rleCount / epb.cols;
                lastRect->c = rleColor;
                lastRect->x = 0;
                lastRect->y = y;
                lastRect->w = epb.cols - 1;
                lastRect->h(h - 1);
                rleCount -= h * epb.cols;
                y += h;
                if(y == epb.rows) {
                    x = epb.cols; // Force inner loop to exit
                }
            } else {
                const unsigned char w = min(rleCount, epb.cols - x);
                lastRect->c = rleColor;
                lastRect->x = x;
                lastRect->y = y;
                lastRect->w = w - 1;
                lastRect->h(0);
                rleCount -= w;
                x        += w;
            }
            if (!mergeTop(sRects, lastRect, cRects)) {
                cRects[lastRect->x] = lastRect - sRects;  // Record subrect in column lookup table
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

        if (nColors > 1) {
            const unsigned int rawTileLen = 1 + 256 * bytesPerColor;

            // Emit two-colored tiles with subrects
            if (nColors == 2) {
                const unsigned char emitBgColor = (lastBg != bgColor);
                const unsigned char emitFgColor = (lastFg != fgColor);
                unsigned long twoColorTileLen = 2 + (emitBgColor + emitFgColor) * bytesPerColor + nFgRects * 2;
                if ((twoColorTileLen <= rawTileLen) && (twoColorTileLen <= epb.bytesAvail)) {
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
                const unsigned long multiColorTileLen = 2 + (emitBgColor * bytesPerColor) + (nFgRects * (2 + bytesPerColor));
                if ((multiColorTileLen <= rawTileLen) && (multiColorTileLen <= epb.bytesAvail)) {
                    *dst++ = AnySubrects | SubrectsColored;
                    if(lastBg != bgColor) {
                        *start |= BackgroundSpecified;
                        emitColor(dst, bgColor);
                        lastBg = bgColor;
                    }
                    *dst++ = nFgRects;
                    for(int i = 0; i < nRects; i++) {
                        if(sRects[i].c != bgColor) {
                            #if DEBUG_SUBRECTS
                                emitColor(dst, (i * 7) % (1 << fbDepth));
                            #else
                                emitColor(dst, sRects[i].c);
                            #endif
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

            if (rawTileLen <= epb.bytesAvail) {
                // If we get here, emit a raw tile
                *dst++ = Raw;
                const Boolean canEmitNativeTileAsRaw = (fbDepth == 8) && (!fbPixFormat.trueColor);
                if(canEmitNativeTileAsRaw) {
                    BlockMove(nativeTile,dst,nativeLen);
                    dst += nativeLen;
                } else {
                    // Emit a raw tile
                    src = nativeTile;
                    // Rewrite the tile with expanded colors
                    const unsigned char rsft = 32 - fbDepth;
                    const unsigned long lmask = ((unsigned long)-1) << rsft; // Mask for leftmost color in block
                    unsigned char bitsLeft = 0;
                    unsigned long packed;
                    unsigned short pixels = epb.rows * epb.cols;
                    do {
                        if(bitsLeft == 0) {
                            packed = *src32++;
                            bitsLeft = 32;
                        }
                        emitColor(dst, (packed & lmask) >> rsft);
                        packed <<= fbDepth;
                        bitsLeft -= fbDepth;
                    } while(--pixels);
                }

                lastBg = lastFg = -1;
                #if USE_SANITY_CHECKS
                    if (rawTileLen != (dst - start)) {
                        dprintf("Incorrect tile %d length: %d != %ld\n", start[0], rawTileLen, (dst - start));
                    }
                #endif
                return dst - start;
            }
        } else {
            // Otherwise emit a solid tile
            const unsigned short solidTileLen = 1 + ((lastBg == bgColor) ? 0 : bytesPerColor);
            if (solidTileLen <= epb.bytesAvail) {
                *dst++ = 0;
                if(lastBg != bgColor) {
                    *start = BackgroundSpecified;
                    emitColor(dst, bgColor);
                    lastBg = bgColor;
                }
                #if USE_SANITY_CHECKS
                    if (solidTileLen != (dst - start)) {
                        dprintf("Incorrect tile %d length: %d != %ld\n", start[0], solidTileLen, (dst - start));
                    }
                #endif
                return dst - start;
            }
        }
        return 0;
    }
#endif
