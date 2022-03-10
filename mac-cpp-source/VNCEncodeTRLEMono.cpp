/****************************************************************************
 *   MiniVNC (c) 2022 Marcio Teixeira                                       *
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
#include "VNCEncodeTRLE.h"

#define DEBUG_SOLID_TILE 0 // Set to one to show solid tiles
#define USE_ENCODER      0 // 0: Assembly; 1: Hybrid; 2: C++

#define min(A,B) ((A) < (B) ? (A) : (B))

extern int              tile_y;
extern unsigned char    lastPaletteSize;
extern unsigned char   *fbUpdateBuffer;

/**
 * This is a fast version of a B&W tile encoder. On entry:
 *
 *    a0: Points to the source tile on the screen
 *    a1: Points to the output buffer
 *    d0: The number of rows in the tile
 *
 * This function will modify registers a0-a2 and d0-d2
 */
static asm unsigned short _encodeTile(const unsigned char *src:__A0, unsigned char *dst:__A1, unsigned short rows:__D0) {
    #define src     a0
    #define dst     a1
    #define stride  a2

    #define rows    d0
    #define tmp     d1
    #define start   d2

    #ifndef VNC_BYTES_PER_LINE
        movea.w fbStride,stride
        suba.w #2,stride
    #else
        movea.w #(VNC_BYTES_PER_LINE - 2),stride
    #endif
    move.l  dst,start           // Make a copy of dst
    btst #0,start
    bne uStart                  // Is dst starting on an odd address?
    bra aStart

aStart:
    move.b #Tile2Color,tmp
    cmp.b lastPaletteSize,tmp
    beq aReusePalette
aWritePalette:
    move.b tmp,lastPaletteSize
    move.b tmp,(dst)+            // Packed tile type
    move.b #00,(dst)+
    move.b #01,(dst)+
    bra uCopyWhitePixels
aReusePalette:
    move.b #127,(dst)+           // Packed tile type with reused palette
    bra uCopyWhitePixels

uStart:
    move.b #Tile2Color,tmp
    cmp.b lastPaletteSize,tmp
    beq uReusePalette
uWritePalette:
    move.b tmp,lastPaletteSize
    move.b tmp,(dst)+            // Packed tile type
    move.w #0001,(dst)+
    bra aCopyWhitePixels
uReusePalette:
    move.b #127,(dst)+           // Packed tile type with reused palette
    bra aCopyWhitePixels

// Aligned writes for 68000
aCopyWhitePixels:
    clr.b tmp                   // Clear condition codes
    bra aEntry
    // Loop and copy white pixels, break on non-white
aCopy1:
    move.w (src)+,(dst)+        // Copy two bytes
    adda.w stride,src           // Move to next row
aEntry:
    dbne rows, aCopy1            // ...fall through on non-white pixel
    bne aNonWhiteTile
    bra uSolidWhiteTile

    // Continue copying pixels, not a all-white tile
aCopy2:
    move.w (src)+,(dst)+        // Copy two bytes
    adda.w stride,src           // Move to next row
aNonWhiteTile:
    dbra rows, aCopy2
    bra done

// Unaligned writes for 68000
uCopyWhitePixels:
    clr.b tmp                   // Clear condition codes
    bra uEntry
uCopy1:
    move.b (src)+,(dst)+        // Copy two bytes
    bne uNonWhiteTile1
    move.b (src)+,(dst)+
    adda.w stride,src           // Move to next row
uEntry:
    dbne rows, uCopy1
    bne uNonWhiteTile
    bra aSolidWhiteTile

    // Continue copying pixels, not a all-white tile
uCopy2:
    move.b (src)+,(dst)+        // Copy two bytes
uNonWhiteTile1:
    move.b (src)+,(dst)+
    adda.w stride,src           // Move to next row
uNonWhiteTile:
    dbra rows, uCopy2

done:
    // Copy number of bytes written to dst to return value
    suba.l   start,dst
    move.l   dst,d0
    rts

aSolidWhiteTile:
    move.b #TileSolid,lastPaletteSize
    // Rewrite tile as solid
    movea.l start,dst
    move.w #(TileSolid<<8)+DEBUG_SOLID_TILE,(dst)+
    moveq #2,d0
    rts

uSolidWhiteTile:
    moveq #TileSolid,tmp
    move.b tmp,lastPaletteSize
    // Rewrite tile as solid
    movea.l start,dst
    move.b tmp,(dst)+
    move.b #DEBUG_SOLID_TILE,(dst)+
    moveq #2,d0
    rts

    #undef src
    #undef dst
    #undef stride
    #undef start
    #undef rows
}

#if defined(VNC_FB_MONOCHROME)
    #define GET_CHUNK VNCEncoder::getChunk
#else
    #define GET_CHUNK  getChunkMonochrome
#endif

#if USE_ENCODER == 0
    asm Boolean getChunkMonochrome(int x, int y, int w, int h, wdsEntry *wdsPtr);

    asm Boolean GET_CHUNK(int x, int y, int w, int h, wdsEntry *wdsPtr) {
        #define xArg    8(a6)
        #define yArg   10(a6)
        #define wArg   12(a6)
        #define hArg   14(a6)
        #define wdsArg 16(a6)

        #define tmp     d1
        #define tiles   d3
        #define rows    d4

        #define src     a3
        #define dst     a4
        #define wds     a3

        link    a6,#0000            // Link for debugger
        movem.l d3-d4/a2-a4,-(a7)   // Save registers

        movea.l fbUpdateBuffer,dst

        // Compute the source location
        //    src = vncBits.baseAddr + width/8 * (tile_y + y) + x/8
        lea     vncBits,src
        move.l  struct(BitMap.baseAddr)(src),src
        // Add width/8 * (tile_y + y)
        move.w  yArg,tmp
        add.w   tile_y,tmp
        #if VNC_FB_WIDTH == 512
            lsl.w   #6,tmp
            ext.l   tmp
        #else
            #if !defined(VNC_FB_BITS_PER_PIX)
                mulu fbStride,tmp
            #else
                mulu #VNC_BYTES_PER_LINE,tmp
            #endif
        #endif
        adda.l tmp,src
        // Add x/8
        move.w  xArg,tmp
        lsr.w   #3,tmp
        ext.l   tmp
        adda.l  tmp,src

        // Compute tiles:
        //   tiles = w/16
        move.w wArg,tiles
        lsr.w #4,tiles

        // Figure out rows
        //   rows = min(16, h - tile_y)
        move.w    hArg,rows
        sub.w     tile_y,rows
        moveq     #16,tmp
        cmp.w     tmp,rows
        blt       tileEntry
        move.w    tmp,rows
        bra       tileEntry
    tileLoop:
        movea.l   src,a0
        movea.l   dst,a1
        move.w    rows,d0
        jsr _encodeTile

        adda.w d0,dst
        addq.w #2,src
    tileEntry:
        dbra tiles,tileLoop

        // Figure out the length of the data
        suba.l fbUpdateBuffer,dst
        // Write out the wds entry
        movea.l wdsArg,wds
        move.l fbUpdateBuffer, struct(wdsEntry.ptr)(wds)
        move.w dst, struct(wdsEntry.length)(wds)

        // Prepare for the next row
        addi.w #16,tile_y
        move.w hArg,d0
        cmp.w tile_y,d0
        sgt d0

        movem.l (a7)+,d3-d4/a2-a4          // Save registers
        unlk     a6
        rts

        #undef xArg
        #undef yArg
        #undef wArg
        #undef hArg
        #undef wdsArg

        #undef tmp
        #undef tiles
        #undef rows

        #undef src
        #undef dst
        #undef wds
    }
#elif USE_ENCODER == 1
    /**
     * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write
     * "rows" as a two-color or solid tile and return the number of bytes written.
     */
    static asm unsigned short encodeTile(const unsigned char *src:__A0, unsigned char *dst:__A1, unsigned short rows:__D0) {
        link    a6,#0000               // Link for debugger
        movem.l a2,-(a7)               // Save registers
        jsr _encodeTile
        movem.l (a7)+,a2               // Restore registers
        unlk     a6
        rts
    }

    Boolean GET_CHUNK(int x, int y, int w, int h, wdsEntry *wds);

    Boolean GET_CHUNK(int x, int y, int w, int h, wdsEntry *wds) {
        const char rows = min(16, h - tile_y);
        unsigned char *dst = fbUpdateBuffer;
        unsigned char *src = VNCFrameBuffer::getPixelAddr(x, y + tile_y);
        unsigned char tiles = w/16;
        while(tiles--) {
            dst += encodeTile(src, dst, rows);
            src += 2;
        }

        wds->length = dst - fbUpdateBuffer;
        wds->ptr = (Ptr) fbUpdateBuffer;

        // Prepare for the next row
        tile_y += 16;

        return tile_y < h;
    }
#else
    // C++ Reference Encoder

    #define TILE_LINE(ROW) if(rows == ROW) goto done; \
                         *dst++ = src[0 + ROW * VNC_BYTES_PER_LINE]; \
                         *dst++ = src[1 + ROW * VNC_BYTES_PER_LINE];

    unsigned short encodeTile(const unsigned char *src, unsigned char *dst, unsigned short rows);

    unsigned short encodeTile(const unsigned char *src, unsigned char *dst, unsigned short rows) {
        unsigned char *start = dst;

        if(tile_y && dst != fbUpdateBuffer) {
            // Packed pallete type with pallete reused from previous tile
            *dst++ = 127;
        } else {
            // Packed pallete type with palleteSize = 2
            *dst++ = 2; // Packed pallete type
            *dst++ = 0; // Black CPIXEL
            *dst++ = 1; // White CPIXEL
        }

        // Encode the packed data for a 16x16 tile
        TILE_LINE(0);
        TILE_LINE(1);
        TILE_LINE(2);
        TILE_LINE(3);
        TILE_LINE(4);
        TILE_LINE(5);
        TILE_LINE(6);
        TILE_LINE(7);
        TILE_LINE(8);
        TILE_LINE(9);
        TILE_LINE(10);
        TILE_LINE(11);
        TILE_LINE(12);
        TILE_LINE(13);
        TILE_LINE(14);
        TILE_LINE(15);
    done:

        return dst - start;
    }

    Boolean VNCEncoder::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
        const char rows = min(16, h - tile_y);
        unsigned char *dst = fbUpdateBuffer;
        unsigned char *src = VNCFrameBuffer::getPixelAddr(x, y + tile_y);
        unsigned char tiles = w/16;
        while(tiles--) {
            dst += encodeTile(src, dst, rows);
            src += 2;
        }

        wds->length = dst - fbUpdateBuffer;
        wds->ptr = (Ptr) fbUpdateBuffer;

        // Prepare for the next row
        tile_y += 16;

        return tile_y < h;
    }
#endif