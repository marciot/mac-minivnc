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
#include "VNCFrameBuffer.h"
#include "VNCEncoder.h"
#include "VNCEncodeZRLE.h"
#include "VNCScreenToRLE.h"
#include "msgbuf.h"

#include "MacTCP.h"

#if !defined(VNC_FB_MONOCHROME)
    #define TILE_MAX_SIZE      16385
    #define UPDATE_BUFFER_SIZE 16385
    #define UPDATE_MAX_TILES    7

    extern int tile_x, tile_y;

    Size VNCEncodeZRLE::minBufferSize() {
        return UPDATE_BUFFER_SIZE;
    }

    int VNCEncodeZRLE::begin() {
        tile_x = 0;
        tile_y = 0;
        return EncoderNeedsCompression;
    }

    #define RLE_LOOP_START(src, len)                                        \
        for (unsigned char *c = (src), *end = (src) + (len); c != end;) {   \
            unsigned char  color = *c++, rleVal = 0;                        \
            unsigned short count = 0;                                       \
            do {                                                            \
                rleVal = *c++;                                              \
                count += rleVal;                                            \
            } while(rleVal == 255);

    #define RLE_LOOP_END }

    static unsigned long encodeTile(const unsigned char *src, unsigned char *dst, char rows, char cols) {
        unsigned char *start = dst;

        dst[0] = TilePlainRLE;
        unsigned long len = writeScreenTileAsRLE(src, dst + 1, 0, rows, cols) + 1;

        // Generate a color histogram of the tile

        unsigned int histogram[256] = {0}, colorBytes = 0, runsOfOne = 0;
        RLE_LOOP_START(dst+1,len-1)
            colorBytes++;
            histogram[color] += count + 1;
            if(count == 0) runsOfOne++;
        RLE_LOOP_END

        // Generate a color palette and mapping table for the tile
        unsigned char cPal[127];
        unsigned char cMap[256] = {0};
        unsigned char nColors = 0;
        for(unsigned int i = 0; i < 256; i++) {
            if(histogram[i]) {
                cMap[i] = nColors & 0x7F;
                cPal[nColors & 0x7F] = i;
                nColors++;
            }
        }

        #define VERIFY_TILES 0
        #if VERIFY_TILES
            unsigned int nPixels = 0;
            for(unsigned int i = 0; i < 256; i++) nPixels += histogram[i];
            if(nPixels != rows*cols) dprintf("RLE tile has invalid number of pixels %d\n", nPixels);
        #endif

        const unsigned short countBytes = len - 1 - colorBytes;
        const unsigned char   tileDepth = (nColors <= 2) ? 1 :
                                          (nColors <= 4) ? 2 :
                                          (nColors <= 16) ? 4 : 8;

        const unsigned short rleTileLen = 1 + colorBytes * cPixelBytes + countBytes;
        const unsigned short rawTileLen = 1 + cols * rows * cPixelBytes;
        const unsigned short sldTileLen = (nColors == 1 )  ? 1 +  cPixelBytes : -1;
        const unsigned short pakTileLen = (nColors <= 16)  ? 1 +  (nColors * cPixelBytes + cols * rows * tileDepth / 8) : -1;
        const unsigned short prlTileLen = (nColors <= 127) ? 1 +  (nColors * cPixelBytes + colorBytes + countBytes - runsOfOne) : -1;

        const unsigned short shortestTile = min(sldTileLen, min(rleTileLen, min(rawTileLen, min(pakTileLen, prlTileLen))));

        // Emit a solid tile
        if(shortestTile == sldTileLen) {
            const unsigned char color = dst[1];
            *dst++ = TileSolid;
            emitCPIXEL(dst, color);
        }
        // Emit a packed palette tile
        else if(shortestTile == pakTileLen) {
            // Move data to end of buffer to make way for color expansion
            unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
            BlockMove(dst, copyTo, len);

            *dst++ = nColors; // Packed palleted
            // Write the palette
            for(int i = 0; i < nColors; i++)
                emitCPIXEL(dst, cPal[i]);
            // Write the packed tile
            const unsigned char nPix = sizeof(unsigned long) * 8 / tileDepth;
            unsigned long accumulator = 0;
            unsigned char bitsLeft = sizeof(unsigned long) * 8;
            RLE_LOOP_START(copyTo+1, len-1)
                for(int i = count + 1; i; i--) {
                    accumulator <<= tileDepth;
                    accumulator |= cMap[color];
                    bitsLeft -= tileDepth;
                    if(bitsLeft == 0) {
                        *(unsigned long*)dst = accumulator;
                        dst += sizeof(unsigned long);
                        accumulator = 0;
                        bitsLeft = sizeof(unsigned long) * 8;
                    }
                }
                if(dst >= c) dprintf("Overrrun!\n");
            RLE_LOOP_END
            // Write out remaining bits
            if(bitsLeft == 16) {
                *(unsigned short*)dst = accumulator;
                dst += sizeof(unsigned short);
            }
        }
        // Emit a raw tile
        else if(shortestTile == rawTileLen) {
            // Move data to end of buffer to make way for color expansion
            unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
            BlockMove(dst, copyTo, len);

            // Rewrite the tile with expanded colors
            *dst++ = TileRaw;
            RLE_LOOP_START(copyTo+1, len-1)
                for(int i = count + 1; i; i--)
                    emitCPIXEL(dst, color);
                if(dst >= c) dprintf("Overrrun!\n");
            RLE_LOOP_END
        }
        // Emit an Packed RLE tile
        else if(shortestTile == prlTileLen) {
            // Move data to end of buffer to make way for color expansion
            unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
            BlockMove(dst, copyTo, len);

            *dst++ = 128 + nColors;
            // Write the palette
            for(int i = 0; i < nColors; i++)
                emitCPIXEL(dst, cPal[i]);
            // Rewrite the tile with expanded colors
            RLE_LOOP_START(copyTo+1, len-1)
                if(count == 0) {
                    *dst++ = cMap[color];
                } else {
                    *dst++ = cMap[color] | 0x80;
                    for(int i = count/255; i; i--) *dst++ = 255;
                    *dst++ = count % 255;
                }
                if(dst >= c) dprintf("Overrrun!\n");
            RLE_LOOP_END
        }
        // Otherwise, emit a RLE encoded tile
        else if(fbPixFormat.trueColor) {
            // Move data to end of buffer to make way for color expansion
            unsigned char *copyTo = dst - len + UPDATE_BUFFER_SIZE;
            BlockMove(dst, copyTo, len);

            // Rewrite the tile with expanded colors
            *dst++ = TilePlainRLE;
            RLE_LOOP_START(copyTo+1, len-1)
                emitCPIXEL(dst, color);
                for(int i = count/255; i; i--) *dst++ = 255;
                *dst++ = count % 255;
                if(dst >= c) dprintf("Overrrun!\n");
            RLE_LOOP_END
        } else {
            // If we are using indexed color, the RLE tile has already been generated
            dst[0] = TilePlainRLE;
            dst += len;
        }

        if(shortestTile != (dst - start)) {
            dprintf("Expected length of %d, got %ld for tile type %d of (Size: %dx%d, cPixelBytes:%d, tileDepth:%d)\n", shortestTile, dst - start, start[0], rows, cols, cPixelBytes, tileDepth);
        }

        return dst - start;
    }

    Boolean VNCEncodeZRLE::getChunk(int x, int y, int w, int h, unsigned char *&ptr, size_t &length) {
        unsigned char *dst = fbUpdateBuffer, *src, rows, cols, tiles = UPDATE_MAX_TILES;

        while(tiles-- && (dst - fbUpdateBuffer <= (UPDATE_BUFFER_SIZE - TILE_MAX_SIZE))) {
            const unsigned char *src = VNCFrameBuffer::getPixelAddr(x + tile_x, y + tile_y);
            const char cols = min(w - tile_x, 64);
            const char rows = min(h - tile_y, 64);
            dst += encodeTile(src, dst, rows, cols);

            // Advance to next tile in row
            tile_x += 64;
            if (tile_x < w)
                continue;

            // Prepare for the next row
            tile_y += 64;
            if (tile_y >= h)
                break;
            tile_x = 0;
        }

        length = dst - fbUpdateBuffer;
        ptr    = fbUpdateBuffer;

        return tile_y < h;
    }
#endif