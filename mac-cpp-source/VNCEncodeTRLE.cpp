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

#include "GestaltUtils.h"

#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "VNCEncodeTiles.h"
#include "VNCEncodeTRLE.h"
#include "msgbuf.h"

//#define USE_NATIVE_TILES               1
//#define USE_PACKED_PALETTE             1
//#define USE_PACKED_PALETTE_W_PADDING   1
//#define USE_PACKED_PALETTE_16_COLOR    1
//#define USE_RAW_TILES                  1
//#define USE_RLE_TILES                  1
//#define USE_RLE_REUNPACKED             1
//#define USE_SOLID_RLE_TILES            0
//#define ALLOW_PALETTE_REUSE            1
#define USE_FAST_MONO_ENCODER            0 // Force use of monochrome encoder when depth = 1

#ifdef VNC_COMPRESSION_LEVEL
    #if   VNC_COMPRESSION_LEVEL == 0
        #define USE_NATIVE_TILES               1
        #define USE_PACKED_PALETTE             0
        #define USE_PACKED_PALETTE_16_COLOR    0
        #define USE_PACKED_PALETTE_W_PADDING   0
        #define USE_RAW_TILES                  1
        #define USE_RLE_TILES                  0
        #define USE_RLE_REUNPACKED             0
        #define USE_SOLID_RLE_TILES            0
        #define ALLOW_PALETTE_REUSE            0
    #elif VNC_COMPRESSION_LEVEL == 1
        #define USE_NATIVE_TILES               1
        #define USE_PACKED_PALETTE             1
        #define USE_PACKED_PALETTE_16_COLOR    1
        #define USE_PACKED_PALETTE_W_PADDING   0
        #define USE_RAW_TILES                  1
        #define USE_RLE_TILES                  0
        #define USE_RLE_REUNPACKED             0
        #define USE_SOLID_RLE_TILES            0
        #define ALLOW_PALETTE_REUSE            0
    #elif VNC_COMPRESSION_LEVEL == 2
        #define USE_NATIVE_TILES               1
        #define USE_PACKED_PALETTE             1
        #define USE_PACKED_PALETTE_16_COLOR    1
        #define USE_PACKED_PALETTE_W_PADDING   0
        #define USE_RAW_TILES                  1
        #define USE_RLE_TILES                  1
        #define USE_RLE_REUNPACKED             0
        #define USE_SOLID_RLE_TILES            0
        #define ALLOW_PALETTE_REUSE            1
    #elif VNC_COMPRESSION_LEVEL == 3
        #define USE_NATIVE_TILES               1
        #define USE_PACKED_PALETTE             1
        #define USE_PACKED_PALETTE_16_COLOR    1
        #define USE_PACKED_PALETTE_W_PADDING   1
        #define USE_RAW_TILES                  1
        #define USE_RLE_TILES                  1
        #define USE_RLE_REUNPACKED             0
        #define USE_SOLID_RLE_TILES            0
        #define ALLOW_PALETTE_REUSE            1
    #elif VNC_COMPRESSION_LEVEL == 4
        #define USE_NATIVE_TILES               1
        #define USE_PACKED_PALETTE             1
        #define USE_PACKED_PALETTE_16_COLOR    1
        #define USE_PACKED_PALETTE_W_PADDING   1
        #define USE_RAW_TILES                  1
        #define USE_RLE_TILES                  1
        #define USE_RLE_REUNPACKED             1
        #define USE_SOLID_RLE_TILES            0
        #define ALLOW_PALETTE_REUSE            1
    #else
        #error Invalid compression level
    #endif
#endif

#define PLAIN_PACKED_TILE_SIZE(DEPTH)  (1 + (1 << DEPTH) + 32 * DEPTH)  // Packed pallete tile

#define UPDATE_BUFFER_SIZE 2050
#define UPDATE_MAX_TILES   7
#define TILE_MAX_SIZE      1 + 16 * 16 * 4 // Size of raw tile

#define HAS_LAST_PALETTE  ALLOW_PALETTE_REUSE && (USE_PACKED_PALETTE || USE_RLE_TILES)

#define src32 ((unsigned long*)src)

int tile_x, tile_y;
unsigned char lastTile = 0;
#if HAS_LAST_PALETTE
    static ColorInfo lastInfo;
#endif

asm Boolean getChunkMonochrome(int x, int y, int w, int h, wdsEntry *wdsPtr);

Size VNCEncodeTRLE::minBufferSize() {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    #ifdef VNC_FB_WIDTH
        const unsigned long tilesPerRow = VNC_FB_WIDTH / 16;
    #else
        const unsigned long tilesPerRow = fbWidth / 16;
    #endif
    return max(UPDATE_BUFFER_SIZE, tilesPerRow * PLAIN_PACKED_TILE_SIZE(1));
}

int VNCEncodeTRLE::begin() {
    tile_x = 0;
    tile_y = 0;
    #if HAS_LAST_PALETTE
        lastTile = 0;
        lastInfo.nColors = 0;
    #endif
    return EncoderOk;
}

#if !defined(VNC_FB_MONOCHROME)
    #define TileRaw           0
    #define TileSolid         1
    #define TilePacked        2
    #define TilePackedReused  127
    #define TileRLE           128
    #define TileRLEReused     129
    #define TileRLEPalette    130

    unsigned char getDepth(unsigned char nColors);
    unsigned char getDepth(unsigned char nColors) {
        return (nColors <= 2)  ? 1 :
               (nColors <= 4)  ? 2 :
               (nColors <= 16) ? 4 : 8;
    }

    static Boolean canReuseColorsPalette(ColorInfo *lastColorInfo, ColorInfo *currColorInfo);
    static Boolean canReuseColorsPalette(ColorInfo *lastColorInfo, ColorInfo *currColorInfo) {
        if (lastColorInfo->nColors < currColorInfo->nColors) {
            return false;
        }
        if ((lastColorInfo->nColors > currColorInfo->nColors) && (getDepth(lastColorInfo->nColors) != getDepth(currColorInfo->nColors))) {
            return false;
        }
        unsigned char* a = lastColorInfo->colorPal, *b = currColorInfo->colorPal;
        unsigned char* aMax = a + lastColorInfo->nColors;
        unsigned char* bMax = b + currColorInfo->nColors;
        do {
            if (*a == *b) {
                a++;
                b++;
                if(b == bMax) break;
            }
            else if (*a < *b) {
                a++;
            }
            else {
                return false;
            }
        }
        while(a < aMax);
        return b == bMax;
    }

    unsigned char findColor(ColorInfo *colorInfo, unsigned char color);
    unsigned char findColor(ColorInfo *colorInfo, unsigned char color) {
        for(unsigned char i = 0; i < colorInfo->nColors; i++) {
            if(color == colorInfo->colorPal[i])
                return i;
        }
        return 255;
    }

    #define ALIGN_PAD 3
    #define ALIGN_LONG(PTR) (PTR) + (sizeof(unsigned long) - (unsigned long)(PTR) % sizeof(unsigned long))

    unsigned long VNCEncodeTRLE::encodeTile(const unsigned char *src, char rows, char cols, unsigned char *dst, unsigned long bytesAvail, Boolean allowPaletteReuse) {
        const unsigned char *start = dst;
        unsigned char scratchSpace[4096 * 2 + ALIGN_PAD];
        unsigned char *nativeTile = ALIGN_LONG(scratchSpace);
        unsigned char    *rleTile = nativeTile + 4096;

        #if USE_SANITY_CHECKS
            if (((unsigned long)nativeTile % 4) != 0) {
                dprintf("Not long word aligned %ld\n", (unsigned long)nativeTile % 4);
            }
        #endif

        const unsigned long nativeLen = screenToNative(src, nativeTile, rows, cols, 0);
        unsigned char *nativeEnd = nativeTile + nativeLen;

        const short nativeColors = (1 << fbDepth);
        ColorInfo currentInfo;

        #if USE_PACKED_PALETTE
            unsigned char *adjustedEnd = nativeEnd;
            if ((nativeLen % 4) == 2) {
                *adjustedEnd++ = nativeEnd[-1];
                *adjustedEnd++ = nativeEnd[-1];
            }
            nativeToColors(nativeTile, adjustedEnd, &currentInfo);
        #else
            currentInfo.nColors = nativeColors;
        #endif

        unsigned char shortestTile = TileSolid;
        unsigned char tileDepth = 0;

        // Figure out whether we can reuse the palette
        const Boolean mapColors  = (currentInfo.nColors < nativeColors);
        ColorInfo *info = &currentInfo;

        unsigned long shortestLen = 1024;
        unsigned long headerLen = 1;

        #if USE_RLE_TILES
            Boolean emitPlainRLE;
        #endif

        #if HAS_LAST_PALETTE
            Boolean canReusePalette;
            const Boolean hadPalette = (allowPaletteReuse) &&
                                       (lastTile != TileRaw) &&
                                       (lastTile != TileSolid) &&
                                       (lastTile != TileRLE);

            // Check whether we can reuse the palette
            const Boolean canReuse = hadPalette &&
                                    (currentInfo.nColors > 1) &&
                                    (mapColors ? canReuseColorsPalette(&lastInfo, &currentInfo) : (lastInfo.nColors == nativeColors));
            if (canReuse) info = &lastInfo;
        #endif

        if (currentInfo.nColors > 1) {
            #if USE_RAW_TILES
                const unsigned long rawTileLen = bytesPerCPixel << 8; // Assume 256 pixels: 256 * bytesPerCPixel
                if (rawTileLen < shortestLen) {
                    shortestLen  = rawTileLen;
                    shortestTile = TileRaw;
                }
            #endif

            #if USE_NATIVE_TILES
                // We call native tiles those which can be copied directly
                // from the screen buffer without further manipulation of
                // pixels. These tiles will be converted into either VNC
                // packed palette types; or a raw tile, but only when the
                // VNC client takes indexed colors.

                const unsigned long nativeTileLen = fbDepth << 5; // Assume 256 pixels: (256 * fbDepth) / 8;
                if (nativeTileLen < shortestLen) {
                    const Boolean canEmitNativeTileAsRaw    = (fbDepth == 8) && (!fbPixFormat.trueColor);
                    const Boolean canEmitNativeTileAsPacked = (fbDepth <  8);
                    const Boolean canEmitNativeTile = canEmitNativeTileAsRaw || canEmitNativeTileAsPacked;
                    if (canEmitNativeTile) {
                        shortestTile = canEmitNativeTileAsPacked ? TilePacked : TileRaw;
                        shortestLen  = nativeTileLen;
                    }
                }
            #endif

            #if USE_PACKED_PALETTE
            #if USE_PACKED_PALETTE_16_COLOR
                if (currentInfo.nColors <= 16) {
            #else
                if (currentInfo.nColors <= 4) {
            #endif
                    tileDepth = getDepth(currentInfo.nColors);
                    const unsigned long packedTileLen = tileDepth << 5; // Assume 256 pixels
                    if (packedTileLen < shortestLen) {
                        #if USE_PACKED_PALETTE_W_PADDING
                            // Emit tiles that do not have a whole number bytes per row?
                            // Doing so requires extra work to align rows to byte boundaries.
                            shortestTile = TilePacked;
                            shortestLen = packedTileLen;
                        #else
                            const Boolean rowsDivisibleByByte = (((unsigned short)cols * tileDepth) % 8 == 0);
                            if (rowsDivisibleByByte) {
                                shortestTile = TilePacked;
                                shortestLen = packedTileLen;
                            }
                        #endif
                    }
                }
            #endif

            // To accurately compare sizes, compute the true shortest length,
            // as up to this point we have been estimating it as 256 pixels.
            shortestLen = (shortestLen * cols * rows) >> 8;

            #if USE_RLE_TILES
                // To find out whether an RLE tile is shorter, we write it out but,
                // stop as soon as it exceeds the size of other tiles
                emitPlainRLE = (currentInfo.nColors > 127);
                if (emitPlainRLE) {
                    info->colorSize = bytesPerCPixel;
                    info->packRuns  = false;
                } else {
                    info->colorSize = 1;
                    info->packRuns  = true;
                }
                const unsigned long rleLen = nativeToRle(nativeTile, nativeEnd, rleTile, rleTile + shortestLen, fbDepth, info);
                if (rleLen < shortestLen) {
                    if (emitPlainRLE) {
                        shortestLen  = rleLen;
                        shortestTile = TileRLE;
                    }
                    else {
                        shortestLen  = rleLen;
                        shortestTile = TileRLEPalette;
                        #if USE_RLE_REUNPACKED
                            if (!canReuse) {
                               // If there are fewer runs of one than the palette size,
                                // we are better off unpacking the tile
                                const unsigned short paletteLen = info->nColors * bytesPerCPixel;
                                if (info->runsOfOne < paletteLen) {
                                    emitPlainRLE = true;
                                }
                            }
                        #endif
                    }
                }
            #endif
        }

        // Special case: Emit solid tiles as RLE so we can preserve the color palette

        #if USE_SOLID_RLE_TILES && HAS_LAST_PALETTE
            const unsigned char mask  = ((1 << fbDepth) - 1);
            const unsigned char color = nativeTile[0] & mask;
            const unsigned char mappedColor = (hadPalette && (lastInfo.nColors < 128) && (info->nColors == 1)) ? findColor(&lastInfo, color) : 255;
            if ((mappedColor != 255) && ((rows * cols) >> 8) < (lastInfo.nColors * bytesPerCPixel)) {
                *dst++ = TileRLEReused;
                *dst++ = mappedColor | 0x80;
                unsigned short rleCnt = rows * cols - 1;
                while(rleCnt >= 255) {
                    rleCnt -= 255;
                    *dst++ = 255;
                }
                *dst++ = rleCnt;
            } else
        #endif

        {
            // Emit the tile header
            switch(shortestTile) {
                #if USE_RLE_TILES
                case TileRLEPalette:
                    if (emitPlainRLE) {
                        *dst++ = TileRLE;
                        break;
                    }
                #endif
                #if HAS_LAST_PALETTE
                    if (canReuse) {
                        *dst++ = TileRLEReused;
                        break;
                    }
                #endif
                    // Intentional fallthrough
                case TilePacked: {
                #if HAS_LAST_PALETTE
                    if (canReuse) {
                        *dst++ = TilePackedReused;
                        break;
                    }
                #endif
                    const unsigned char rleFlag = shortestTile & TileRLE;
                    if(mapColors) {
                        headerLen += info->nColors * bytesPerCPixel;
                        *dst++ = info->nColors | rleFlag;
                        // Write the palette
                        for(int i = 0; i < info->nColors; i++)
                            emitCPIXEL(dst, info->colorPal[i]);
                        #if HAS_LAST_PALETTE
                            BlockMove(&currentInfo, &lastInfo, sizeof(ColorInfo));
                        #endif
                    } else {
                        headerLen += nativeColors * bytesPerCPixel;
                        *dst++ = nativeColors | rleFlag;
                        // Write the palette
                        for(int i = 0; i < nativeColors; i++)
                            emitCPIXEL(dst, i);
                        #if HAS_LAST_PALETTE
                            lastInfo.nColors = nativeColors;
                        #endif
                    }
                    break;
                }
                default:
                    *dst++ = shortestTile;
            }

            switch(shortestTile) {
                case TileRaw: {
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
                        unsigned short pixels = rows * cols;
                        do {
                            if(bitsLeft == 0) {
                                packed = *src32++;
                                bitsLeft = 32;
                            }
                            emitCPIXEL(dst, (packed & lmask) >> rsft);
                            packed <<= fbDepth;
                            bitsLeft -= fbDepth;
                        } while(--pixels);
                    }
                    break;
                }
                case TilePacked:
                case TilePackedReused: {
                    // Emit a packed palette tile
                    if(mapColors) {
                    #if USE_PACKED_PALETTE_W_PADDING
                        src = nativeTile;
                        const unsigned short paddingPerRow = (((unsigned short)cols * tileDepth) % 8);
                        if (paddingPerRow != 0) {
                            // Write a downsampled tile; a row at a time, with padding
                            const unsigned short dstBytesPerRow = ((unsigned short)cols * tileDepth + 7) / 8;
                            const unsigned short srcBytesPerRow = ((unsigned short)cols * fbDepth) / 8;
                            for (unsigned char row = 0; row < rows; row++) {
                                nativeToPacked(src, dst, src + srcBytesPerRow, fbDepth, tileDepth, info);
                                src += srcBytesPerRow;
                                dst += dstBytesPerRow;
                            }
                        }
                        else
                    #endif
                        {
                            // Write out a downsampled tile
                            dst += nativeToPacked(nativeTile, dst, nativeEnd, fbDepth, tileDepth, info);
                        }
                    } else {
                        BlockMove(nativeTile,dst,nativeLen);
                        dst += nativeLen;
                    }
                    break;
                }
            #if USE_RLE_TILES
                case TileRLE: {
                    const unsigned char *rleEnd = rleTile + shortestLen;
                    if (fbPixFormat.trueColor) {
                        // Need to scan through the tile emitting true color values
                        for (unsigned char *c = rleTile; c < rleEnd;) {
                            // Write out the color
                            emitCPIXEL(dst, *c);
                            c += info->colorSize;
                            // Copy the count bytes
                            while ((*dst++ = *c++) == 255);
                        }
                    } else {
                        // Emit the tile as packed
                        BlockMove(rleTile,dst,rleEnd - rleTile);
                        dst += rleEnd - rleTile;
                    }
                    break;
                }
                case TileRLEReused:
                case TileRLEPalette: {
                    const unsigned char *rleEnd = rleTile + shortestLen;
                    if (emitPlainRLE) {
                        shortestLen += info->runsOfOne;
                        // Need to scan through the tile undoing any packed values
                        for (unsigned char *c = rleTile; c < rleEnd;) {
                            const unsigned char rleVal = *c++;
                            // Write out the color
                            emitCPIXEL(dst, mapColors ? info->colorPal[rleVal & 0x7F] : rleVal & 0x7F);
                            // Copy the run bytes
                            if (rleVal & 0x80) {
                                // Copy the count bytes
                                while ((*dst++ = *c++) == 255);
                            } else {
                                *dst++ = 0; // Unpack the pixel
                            }
                        }
                    } else {
                        // Emit the tile as packed
                        BlockMove(rleTile,dst,rleEnd - rleTile);
                        dst += rleEnd - rleTile;
                    }
                    break;
                }
            #endif // USE_RLE_TILES
                default: {
                    shortestLen = bytesPerCPixel;
                    // Emit a solid tile
                    const unsigned char mask  = ((1 << fbDepth) - 1);
                    const unsigned char color = nativeTile[0] & mask;
                    emitCPIXEL(dst, color);
                    break;
                }
            }
        }
        #if HAS_LAST_PALETTE
            lastTile = start[0];
        #endif
        #if USE_SANITY_CHECKS
            if ((headerLen + shortestLen) != (dst - start)) {
                dprintf("Incorrect tile %d length: %ld != %ld\n", start[0], (headerLen + shortestLen), (dst - start));
            }
        #endif
        return dst - start;
    }

    Boolean VNCEncodeTRLE::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
        if ((!hasColorQD) || ((fbDepth == 1) && USE_FAST_MONO_ENCODER)) {
            return getChunkMonochrome(x,y,w,h,wds);
        }

        unsigned char *dst = fbUpdateBuffer, *src, rows, cols;

        unsigned long bytesAvail = UPDATE_BUFFER_SIZE;

        goto beginLoop;

        while (bytesAvail > TILE_MAX_SIZE) {
            const unsigned long len = encodeTile(src, rows, cols, dst, bytesAvail, true);
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
