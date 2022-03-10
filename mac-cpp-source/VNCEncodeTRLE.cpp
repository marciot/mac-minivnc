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

//#define USE_16_COLOR_TILES
//#define USE_4_COLOR_TILES
//#define USE_2_COLOR_TILES
//#define USE_RLE_COLOR_TILES
//#define USE_RAW_COLOR_TILES
//#define USE_NATIVE_COLOR_TILES
//#define DEBUG_TILE_TYPES

#ifdef VNC_COMPRESSION_LEVEL
    #if   VNC_COMPRESSION_LEVEL == 0
        #define USE_FAST_MONO_ENCODER
        #define USE_NATIVE_COLOR_TILES
    #elif VNC_COMPRESSION_LEVEL == 1
        #define USE_FAST_MONO_ENCODER
        #define USE_RLE_COLOR_TILES
    #elif VNC_COMPRESSION_LEVEL == 2
        #define USE_FAST_MONO_ENCODER
        #define USE_RLE_COLOR_TILES
        #define USE_NATIVE_COLOR_TILES
    #elif VNC_COMPRESSION_LEVEL == 3
        #define USE_RLE_COLOR_TILES
        #define USE_NATIVE_COLOR_TILES
        #define USE_2_COLOR_TILES
    #elif VNC_COMPRESSION_LEVEL == 4
        #define USE_RLE_COLOR_TILES
        #define USE_NATIVE_COLOR_TILES
        #define USE_2_COLOR_TILES
        #define USE_4_COLOR_TILES
    #else
        #error Invalid compression level
    #endif
#endif

#if defined(USE_NATIVE_COLOR_TILES) && defined(USE_RLE_COLOR_TILES)
    #define USE_RLE_EARLY_ABORT
#endif

#if defined(USE_2_COLOR_TILES) || defined(USE_4_COLOR_TILES) || defined(USE_16_COLOR_TILES)
    #define RLE_GATHERS_PALETTE
#endif

#ifndef USE_STDOUT
    #define printf ShowStatus
    int ShowStatus(const char* format, ...);
#endif

int              tile_x, tile_y;
unsigned char    lastPaletteSize;
unsigned char   *fbUpdateBuffer = 0;

#define TileSolid      1
#define Tile2Color     2
#define TileReuse    127
#define TilePlainRLE 128

#define UPDATE_BUFFER_SIZE 1040
#define UPDATE_MAX_TILES    7

#define PLAIN_RLE_MAX_TILE_SIZE (1 + 512)
#define PLAIN_PACKED_TILE_SIZE(DEPTH)  (1 + (1 << DEPTH) + 32 * DEPTH)  // Packed pallete tile

#if defined(USE_RLE_COLOR_TILES)
    #define TILE_MAX_SIZE   (PLAIN_RLE_MAX_TILE_SIZE)
#else
    #define TILE_MAX_SIZE   (PLAIN_PACKED_TILE_SIZE(fbDepth))
#endif

OSErr VNCEncoder::setup() {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    #ifdef VNC_FB_WIDTH
        const unsigned long tilesPerRow = VNC_FB_WIDTH / 16;
    #else
        const unsigned long tilesPerRow = fbWidth / 16;
    #endif
    #if defined(VNC_FB_MONOCHROME) || defined(USE_FAST_MONO_ENCODER)
        fbUpdateBuffer = (unsigned char*) NewPtr(tilesPerRow * PLAIN_PACKED_TILE_SIZE(1));
    #else
        fbUpdateBuffer = (unsigned char*) NewPtr(UPDATE_BUFFER_SIZE);
    #endif
    if (MemError() != noErr)
        return MemError();
    return noErr;
}

OSErr VNCEncoder::destroy() {
    DisposPtr((Ptr)fbUpdateBuffer);
    fbUpdateBuffer = NULL;
    return MemError();
}

void VNCEncoder::begin() {
    tile_x = 0;
    tile_y = 0;
    lastPaletteSize = 0;
}

#if !defined(VNC_FB_MONOCHROME)
        /**
         * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write to
         * a RLE encoded VNC tile to "dst" and return the number of bytes written. Only the first "rows" of
         * the tile will be considered.
         *
         * This function will use the correct depth and linestride for the device.
         */
        static asm unsigned short encodeTilePlainRLE(const unsigned char *src, unsigned char *dst, const unsigned char* end, char rows
            #ifdef RLE_GATHERS_PALETTE
                ,unsigned char palette[5]
            #endif
        ) {
            machine 68020
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : Maximum value to write
             *   A3                      : Linestride of screen
             *   A4                      : palettePtr
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Offset from src
             *   D1                      : Depth of screen
             *   D2                      : Rows to process
             *   D3                      : Pixel counter
             *   D4                      : RLE value
             *   D5                      : RLE count
             *   D6                      : RLE value
             *   D6                      : RLE alternate
             */

            #define srcArg  8(a6)
            #define dstArg  12(a6)
            #define endArg  16(a6)
            #define rowArg  20(a6)
            #define palArg  22(a6)

            #define src     a0
            #define dst     a1
            #define end     a2
            #define stride  a3
            #define palette a4
            #define offset  d0
            #define depth   d1
            #define rows    d2
            #define pixCnt  d3
            #define pixVal  d4
            #define rleCnt  d5
            #define rleVal  d6
            #define rleAlt  d7

            link    a6,#0000            // Link for debugger
            movem.l d3-d7/a2-a4,-(a7)   // Save registers

            movea.l  srcArg,src         // Load the src ptr into A0
            movea.l  dstArg,dst         // Load the dst ptr into A1
            movea.l  endArg,end         // Load the end ptr into A2
            clr.w    rows               // Load rows into D2
            move.b   rowArg,rows
            movea.l  palArg,palette     // Load the palette ptr in A4

            #ifdef VNC_FB_BITS_PER_PIX
                move.l #VNC_FB_BITS_PER_PIX,depth
            #else
                move.l fbDepth,depth
            #endif
            #ifndef VNC_BYTES_PER_LINE
                move.w fbStride,offset
                #ifdef VNC_FB_BITS_PER_PIX
                    subi.w #BYTES_PER_TILE_ROW,offset
                #else
                    sub.w depth,offset
                    sub.w depth,offset
                #endif
                asl.w #3,offset // Multiply by eight
                movea.w offset,stride
            #else
                movea.w #((VNC_BYTES_PER_LINE - BYTES_PER_TILE_ROW)*8),stride
            #endif
            clr.l offset
            clr.w pixVal

            #ifdef RLE_GATHERS_PALETTE
                clr.l rleAlt
                clr.l rleVal
            #endif

            bfextu (src) {0:depth},rleVal  // Load 1st pixel
            move.b #-1,rleCnt              // Initialize count to -1

            #ifdef RLE_GATHERS_PALETTE
                bset #31,rleVal             // Mark as occupied
                move.b rleVal, (palette)+   // Set first five palette
                move.b rleVal,0(palette)    // ...values to be the same
                move.b rleVal,1(palette)    // ...as the first pixel
                move.b rleVal,2(palette)    // ...of the tile
                move.b rleVal,3(palette)
            #endif

            move.b #TilePlainRLE,(dst)+ // Plain RLE tile type

        // Outer loop, process all rows
            bra nextRow
        encodeRow:
        // Inner loop, process a row of sixteen pixels
            move.w #16,pixCnt           // Max of 16 pixels per tile row
            bra nextPixel
        encodePixel:
            bfextu (src) {offset:depth},pixVal // Read a pixel
            add.l depth,offset
            cmp.b pixVal,rleVal         // Is it the same as the last pixel?
            bne writeRun
            addq.b #1,rleCnt            // Increment count
        nextPixel:
            dbra pixCnt, encodePixel    // Last pixel of tile row?
        // End of inner loop
            add.l stride,offset         // ...otherwise move to next row
        nextRow:
            dbra rows, encodeRow        // Have we processed all rows?
        // End of outer loop

            bra tileFinished

        writeRun:
            // If dst is zero, don't write out any values
            cmpa.w #0,dst
            beq noWriteRun
            move.b rleVal,(dst)+        // Write an RLE value
            move.b rleCnt,(dst)+        // ...and the count
            #ifdef USE_RLE_EARLY_ABORT
                cmpa.l dst,end          // If our RLE tile exceeds a RAW tile in size...
                bne noWriteRun
                suba.l dst,dst          // ...clear dst so we no longer write out runs
                cmpa.w #0,palette
                beq earlyAbort
            #endif
        noWriteRun:
            #ifdef RLE_GATHERS_PALETTE
                // If palette is zero, skip the search
                    cmpa.w #0,palette
                #ifdef USE_RLE_EARLY_ABORT
                    beq checkEarlyAbort
                #else
                    beq pixFound
                #endif
                #if defined(USE_4_COLOR_TILES) || defined(USE_16_COLOR_TILES)
                    // We have a pixel value that does not match the low byte
                    // of rleVal, but we also check the three previously seen
                    // pixel values, which are stored in the other register
                    // halves of rleVal and rleAlt.
                        swap rleVal                 // Load 1st slot...
                        bpl testSlot2               // ...slot empty if sign bit clear
                        cmp.b pixVal,rleVal         // Compare pix to slot 1
                        beq pixFound
                    testSlot2:
                        exg rleVal,rleAlt           // Load 2nd slot...
                        tst.l rleVal
                        bpl testSlot3               // ...slot empty if sign bit clear
                        cmp.b pixVal,rleVal         // Compare pix to slot 2
                        beq pixFound
                    testSlot3:
                        swap rleVal                 // Load 3rd slot...
                        bpl foundFreeSlot           // ...slot empty if sign bit clear
                        cmp.b pixVal,rleVal         // Compare pix to slot 3
                        beq pixFound
                    // If a pixel isn't found, then attempt to find a free
                    // slot to store it in for next time.
                    pixNotFound:
                        swap rleVal                 // Load 1st slot
                        bpl foundFreeSlot           // ...slot empty if sign bit clear
                        exg rleVal,rleAlt           // Load 2nd slot
                        tst.l rleVal
                        bpl foundFreeSlot           // ...slot empty if sign bit clear
                        swap rleVal                 // Load 3rd slot
                        bmi noFreeSlots             // ...slot taken if sign bit set
                    foundFreeSlot:
                        bset #31,rleVal             // ...and mark it occupied
                        move.b pixVal,(palette)+    // ...write out the color
                        bra pixFound
                    noFreeSlots:
                        move.b pixVal,(palette)     // ...write out the last color
                        suba.l palette,palette      // Clear palette so we skip the search
                #else
                        swap rleVal                 // Load 1st slot...
                        bpl foundFreeSlot           // ...slot empty if sign bit clear
                        cmp.b pixVal,rleVal         // Compare pix to slot 1
                        beq pixFound
                        bra noFreeSlots
                    foundFreeSlot:
                        bset #31,rleVal             // ...and mark it occupied
                        move.b pixVal,(palette)+    // ...write out the color
                        bra pixFound
                    noFreeSlots:
                        move.b pixVal,(palette)+    // ...write out the color
                        suba.l palette,palette      // Clear palette so we skip the search
                #endif // USE_4_COLOR_TILES
            #endif // RLE_GATHERS_PALETTE
        pixFound:
            move.b pixVal,rleVal        // Set RLE value to current pixel
            clr.b rleCnt                // ...and set RLE count to 0, indicating one pixel
            bra nextPixel

        tileFinished:
    #ifdef USE_RLE_EARLY_ABORT
            cmpa.w #0,dst
            beq earlyAbort
    #endif

            // Do we have a solid tile?
            cmp.b #255,rleCnt
            beq writeSolidTile

            move.b rleVal,(dst)+        // Write RLE value and count
            move.b rleCnt,(dst)+
            bra done

    #ifdef USE_RLE_EARLY_ABORT
        checkEarlyAbort:
            cmpa.w #0,dst
            beq earlyAbort
            bra pixFound

        earlyAbort:
            movea.l end,dst
            bra done
    #endif

        writeSolidTile:
            movea.l dstArg,dst           // Rewrite tile as a solid tile
            move.b #TileSolid,(dst)+     // Solid tile
            move.b rleVal,(dst)+

        done:
            // Copy number of bytes written to dst to return value
            suba.l   dstArg,dst
            move.l   dst,d0

        exit:
            movem.l (a7)+,d3-d7/a2-a4  // Restore registers
            unlk    a6
            rts

            #undef srcArg
            #undef dstArg
            #undef rowArg
            #undef palArg

            #undef src
            #undef dst
            #undef end
            #undef stride
            #undef palette
            #undef offset
            #undef depth
            #undef rows
            #undef pixCnt
            #undef pixVal
            #undef rleCnt
            #undef rleVal
            #undef rleAlt
        }

        /**
         * With "src" and "size" pointing to an RLE encoded tile (as written by encodeTilePlainRLE) this function
         * will count the number of colors in a tile up to 17. The unique colors are written starting in "dst"
         *
         * For efficiency, this function uses eight CPU registers as a 256 bit field for tallying colors, but
         * it requires a reread pass of the RLE encoded data. For this reason, when RLE_GATHERS_PALETTE is set,
         * "encodeTilePlainRLE" will count up to four colors during that 1st pass.
         */
        static asm unsigned short countRLEColors(const unsigned char *src, unsigned char *dst, unsigned int size) {
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : End
             *   A3                      : Color Table 224 - 255
             *   A4                      : Storage for D6
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Color Table   0 -  31
             *   D1                      : Color Table  32 -  63
             *   D2                      : Color Table  64 -  95
             *   D3                      : Color Table  96 - 127
             *   D4                      : Color Table 128 - 159
             *   D5                      : Color Table 160 - 191
             *   D6                      : Color Table 192 - 223
             *   D7                      : Temporary
             */

            #define src     A0
            #define dst     A1
            #define end     A2
            #define table7  A3
            #define saveD6  A4
            #define table0  D0
            #define table1  D1
            #define table2  D2
            #define table3  D3
            #define table4  D4
            #define table5  D5
            #define table6  D6
            #define tmp     D7
            #define count   16(a6)

            link    a6,#0000            // Link for debugger
            movem.l d3-d7/a2-a4,-(a7)   // Save registers

            movea.l 8(a6),src           // Load the src ptr into A0
            movea.l 12(a6),dst          // Load the dst ptr into A1
            move.w  16(a6),end          // Load length into end
            adda.l  src,end             // A1 now points to the end

            // Read the tile type
            move.b (src)+,d0

            // Consider maximum of 16 colors
            move.b #17,count

            // Clear the color tables
            moveq #0,table0
            moveq #0,table1
            moveq #0,table2
            moveq #0,table3
            moveq #0,table4
            moveq #0,table5
            moveq #0,table6
            move.l table6,table7

        loop:
            move.b (src),tmp
            bclr #7,tmp
            bne b1xx
        b0xx:
            bclr #6,tmp
            bne b01x
        b00x:
            bclr #5,tmp
            bne b001
        b000:
            bset tmp,table0
            bra tallyColor
        b1xx:
            bclr #6,tmp
            bne b11x
        b10x:
            bclr #5,tmp
            bne b101
        b100:
            bset tmp,table4
            bra tallyColor
        b01x:
            bclr #5,tmp
            bne b011
        b010:
            bset tmp,table2
            bra tallyColor
        b001:
            bset tmp,table1
            bra tallyColor
        b011:
            bset tmp,table3
            bra tallyColor
        b11x:
            bclr #5,tmp
            bne b111
        b101:
            bset tmp,table5
            bra tallyColor
        b110:
            bset tmp,table6
            bra tallyColor
        b111:
            // Setting a bit in the color last color table is a bit harder,
            // as we have to load it into a data register
            exg  saveD6,d6
            exg  table7,d6
            bset tmp,d6
            exg  table7,d6
            exg  saveD6,d6
            bra tallyColor

        tallyColor:
            // Was a bit set?
            beq nextPixel
            move.b (src),(dst)+
            subq.b #1,count
            beq tooManyColors // Did we find 17 colors?

        nextPixel:
            addq.l #2,src // Skip the RLE
            cmpa.l end, src
            blt loop

        tooManyColors:
            // Copy number of colors found to return value
            move.l  #17,d0
            sub.b   count,d0

            movem.l (a7)+,d3-d7/a2-a4  // Restore registers
            unlk    a6
            rts

            #undef src
            #undef dst
            #undef end
            #undef table0
            #undef table1
            #undef table2
            #undef table3
            #undef table4
            #undef table5
            #undef table6
            #undef table7
            #undef saveD6
            #undef count
            #undef tmp
        }

        /**
         * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write
         * "rows" as a two-color tile and return the number of bytes written.
         */
        static asm unsigned short encodeTileTwoColor(const unsigned char *src, unsigned char *dst, unsigned short size, unsigned char rows, unsigned char *palette) {
            machine 68020
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : Copy of dest ptr
             *   A3                      : Linestride of screen
             *   A4                      : Unused
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Rows to process
             *   D1                      : Depth of screen
             *   D2                      : First color in palette
             *   D3                      : Pixel count
             *   D4,D5,D6,D7             : Unused
             */

            #define src     a0
            #define dst     a1
            #define start   a2
            #define stride  a3
            #define palette a4

            #define rows    d0
            #define depth   d1
            #define color1  d2
            #define pixCnt  d3
            #define pixVal  d4
            #define output  d5
            #define offset  d6

            link    a6,#0000            // Link for debugger
            movem.l d3-d6/a2-a4,-(a7)   // Save registers

            movea.l 8(a6),src           // Load the src ptr into A0
            movea.l 12(a6),dst          // Load the dst ptr into A1
            clr.w   rows                // Load rows into D2
            move.b  18(a6),rows
            movea.l 20(a6),palette      // Load the palette ptr in A4

            movea.l  dst,start

            #ifdef VNC_FB_BITS_PER_PIX
                move.b #VNC_FB_BITS_PER_PIX,depth
            #else
                move.l fbDepth,depth
            #endif

            clr.w output

            /***********************************************************
             * If monitor depth is eight, transcode tile
             ***********************************************************/
        test8bit:
            cmp.b #8,depth              // Is the screen depth 8?
            bne test4or2bit
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                movea.w fbStride,stride
                suba.w #16,stride
            #else
                movea.w #(VNC_BYTES_PER_LINE - 16),stride
            #endif
            // Outer loop, process all rows
            bra depth8
        encRow8:
            // Inner loop, process a row of sixteen pixels
            move.w #16,pixCnt           // Max of 16 pixels per tile row
            bra nextPix8
        writePix8:
            cmp.b (src)+,color1         // Test pixel color
            beq nextPix8
            bset pixCnt, output
        nextPix8:
            dbra pixCnt, writePix8      // Last pixel of tile row?
            // End of inner loop
            adda.l stride,src           // Move to next row
            move.w output, (dst)+
            clr.w output
        depth8:
            dbra rows, encRow8          // Have we processed all rows?
            bra done

            /***********************************************************
             * If monitor depth is four or two, transcode tile
             ***********************************************************/
        test4or2bit:
            cmp.b #1,depth              // Is the screen depth 1?
            beq test1bit
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                move.w fbStride,offset
                #ifdef VNC_FB_BITS_PER_PIX
                    subi.w #BYTES_PER_TILE_ROW,offset
                #else
                    sub.w depth,offset
                    sub.w depth,offset
                #endif
                asl.w #3,offset // Multiply by eight
                movea.w offset,stride
            #else
                movea.w #((VNC_BYTES_PER_LINE - BYTES_PER_TILE_ROW)*8),stride
            #endif
            clr.l offset
            // Outer loop, process all rows
            bra depth4
        encRow4:
            // Inner loop, process a row of sixteen pixels
            move.w #16,pixCnt           // Max of 16 pixels per tile row
            bra nextPix4
        writePix4:
            bfextu (src) {offset:depth},pixVal // Read a pixel
            add.l depth,offset
            cmp.b pixVal,color1         // Test pixel color
            beq nextPix4
            bset pixCnt, output
        nextPix4:
            dbra pixCnt, writePix4      // Last pixel of tile row?
            // End of inner loop
            add.l stride,offset         // ...otherwise move to next row
            move.w output, (dst)+
            clr.w output
        depth4:
            dbra rows, encRow4          // Have we processed all rows?
            bra done

            /***********************************************************
             * If monitor depth is one, simply copy 2 bytes at a time
             ***********************************************************/
        test1bit:
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                movea.w fbStride,stride
                suba.w #2,stride
            #else
                movea.w #(VNC_BYTES_PER_LINE - 2),stride
            #endif
            // Outer loop, process all rows
            bra depth1
        copy1:
            move.w (src)+,(dst)+        // Copy two bytes
            adda.l stride,src           // Move to next row
        depth1:
            dbra rows, copy1

        done:
            // Copy number of bytes written to dst to return value
            suba.l   start,dst
            move.l   dst,d0

        exit:
            movem.l (a7)+,d3-d6/a2-a4         // Restore registers
            unlk    a6
            rts

        notImplemented:
            move.b  16(a6),d0
            bra exit

            /***********************************************************
             * Write tile type and palette
             ***********************************************************/

        writePalette:
            cmpa.w #0,palette            // Do we have a palette?
            beq defaultPalette
            move.b  (palette),color1     // Make a copy of the 1st color
            move.b #2,(dst)+             // Packed tile type
            move.w (palette)+,(dst)+     // Copy 2 bytes of palette
            rts
        defaultPalette:
            cmp.b #2,lastPaletteSize
            beq reusePalette
            move.b #2,(dst)+            // Packed tile type
            move.w #0x0001,(dst)+
            rts
        reusePalette:
            move.b #127,(dst)+          // Packed tile type with reused palette
            rts

            #undef src
            #undef dst
            #undef start
            #undef stride
            #undef palette
            #undef rows
            #undef offset
            #undef depth
            #undef color1
            #undef pixCnt
            #undef pixVal
            #undef output
        }

        /**
         * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write
         * "rows" as a four-color tile and return the number of bytes written.
         */
        static asm unsigned short encodeTileFourColor(const unsigned char *src, unsigned char *dst, unsigned short size, unsigned char rows, unsigned char *palette) {
            machine 68020
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : Copy of dest ptr
             *   A3                      : Linestride of screen
             *   A4                      : Unused
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Rows to process
             *   D1                      : Depth of screen
             *   D2                      : Pixel value
             *   D3                      : Pixel count
             *   D4                      : Output accumulator
             *   D5                      : Offset
             *   D6,D7                   : Color translation table
             */

            #define src     a0
            #define dst     a1
            #define start   a2
            #define stride  a3
            #define palette a4

            #define rows    d0
            #define depth   d1
            #define pixVal  d2
            #define pixCnt  d3
            #define output  d4
            #define offset  d5
            #define cTable1 d6
            #define cTable2 d7

            link    a6,#0000            // Link for debugger
            movem.l d3-d7/a2-a4,-(a7)   // Save registers

            movea.l 8(a6),src           // Load the src ptr into A0
            movea.l 12(a6),dst          // Load the dst ptr into A1
            clr.w   rows                // Load rows into D2
            move.b  18(a6),rows
            movea.l 20(a6),palette      // Load the palette ptr in A4

            movea.l dst,start
            clr.l   output

            #ifdef VNC_FB_BITS_PER_PIX
                move.b #VNC_FB_BITS_PER_PIX,depth
            #else
                move.l fbDepth,depth
            #endif

            /***********************************************************
             * If monitor depth is eight or four, transcode tile
             ***********************************************************/
        test8bit:
            cmp.b #8,depth              // Is the screen depth 8?
            beq encode2bit
            cmp.b #4,depth              // Is the screen depth 4?
            bne test2bit
        encode2bit:
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                move.w fbStride,offset
                #ifdef VNC_FB_BITS_PER_PIX
                    subi.w #BYTES_PER_TILE_ROW,offset
                #else
                    sub.w depth,offset
                    sub.w depth,offset
                #endif
                asl.w #3,offset // Multiply by eight
                movea.w offset,stride
            #else
                movea.w #((VNC_BYTES_PER_LINE - BYTES_PER_TILE_ROW)*8),stride
            #endif
            clr.l offset
            // Outer loop, process all rows
            bra depth8
        encRow8:

            // Inner loop, process 16 pixels
            move.w #16,pixCnt           // 16 pixels in row
            bra nextPix8
        writePix8:
            bfextu (src) {offset:depth},pixVal // Read a pixel
            add.l depth,offset
            // Lookup the pixel value in the translation table
            bra colorLookup
        packPix8:
            lsl.l #2,output
            add.l pixVal,output
        nextPix8:
            dbra pixCnt, writePix8      // Last pixel?
            // End of inner loop
            move.l output, (dst)+
            clr.l output

            add.l stride,offset         // Move to next row
        depth8:
            dbra rows, encRow8          // Have we processed all rows?
            bra done

        // Find pixVal in the translation table and return index in pixVal
        colorLookup:
            cmp.b cTable1, pixVal
            bne testColor2
            moveq #0,pixVal
            bra packPix8
        testColor2:
            cmp.b cTable2, pixVal
            bne testColor3
            moveq #1,pixVal
            bra packPix8
        testColor3:
            swap cTable1
            swap cTable2
            cmp.b cTable1, pixVal
            bne testColor4
            moveq #2,pixVal
            bra unswapTable
        testColor4:
            cmp.b cTable2, pixVal
            //bne writeSolidTile // Error color not found in table!
            moveq #3,pixVal
        unswapTable:
            swap cTable1
            swap cTable2
            bra packPix8

            /***********************************************************
             * If monitor depth is two, simply copy 4 bytes at a time
             ***********************************************************/
        test2bit:
            cmp.b #2,depth              // Is the screen depth 2?
            bne test1bit
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                movea.w fbStride,stride
                suba.w #4,stride
            #else
                movea.w #(VNC_BYTES_PER_LINE - 4),stride
            #endif
            // Outer loop, process all rows
            bra depth2
        copy2:
            move.l (src)+,(dst)+        // Copy four bytes
            adda.l stride,src           // Move to next row
        depth2:
            dbra rows, copy2
            bra done

            /***********************************************************
             * If monitor depth is one, not implemented
             ***********************************************************/
        test1bit:
            bra notImplemented

        writeSolidTile:
            movea.l start ,dst           // Rewrite tile as a solid tile
            move.b #TileSolid,(dst)+     // Solid tile
            move.b pixVal,(dst)+

        done:
            // Copy number of bytes written to dst to return value
            suba.l   start,dst
            move.l   dst,d0

        exit:
            movem.l (a7)+,d3-d7/a2-a4         // Restore registers
            unlk    a6
            rts

        notImplemented:
            move.b  16(a6),d0
            bra exit

            /***********************************************************
             * Write tile type and palette
             ***********************************************************/

        writePalette:
            cmpa.w #0,palette            // Do we have a palette?
            beq defaultPalette
            // Load up the palette in the color translation table
            clr.l cTable1
            clr.l cTable2
            move.b 2(palette), cTable1
            move.b 3(palette), cTable2
            swap cTable1
            swap cTable2
            move.b 0(palette), cTable1
            move.b 1(palette), cTable2
            // Write out the tile header with palette
            move.b #4,(dst)+             // Packed tile type
            move.l (palette)+,(dst)+     // Copy 4 bytes of palette
            rts
        defaultPalette:
            cmp.b #4,lastPaletteSize
            beq reusePalette
            move.b #4,(dst)+            // Packed tile type
            move.l #0x00010203,(dst)+
            rts
        reusePalette:
            move.b #127,(dst)+          // Packed tile type with reused palette
            rts

            #undef src
            #undef dst
            #undef start
            #undef stride
            #undef palette
            #undef rows
            #undef depth
            #undef color1
            #undef pixCnt
            #undef output
        }

        /**
         * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write
         * "rows" as a sixteen-color tile and return the number of bytes written.
         */
        static asm unsigned short encodeTileSixteenColor(const unsigned char *src, unsigned char *dst, unsigned short size, unsigned char rows, unsigned char *palette) {
            machine 68020
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : Copy of dest ptr
             *   A3                      : Linestride of screen
             *   A4                      : Palette
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Rows to process
             *   D1                      : Depth of screen
             *   D2                      : First color in palette
             *   D3                      : Pixel count
             *   D4,D5,D6,D7             : Unused
             */

            #define src     a0
            #define dst     a1
            #define start   a2
            #define stride  a3
            #define palette a4

            #define rows    d0
            #define depth   d1
            #define color1  d2

            link    a6,#0000            // Link for debugger
            movem.l a2-a4,-(a7)         // Save registers

            movea.l 8(a6),src           // Load the src ptr into A0
            movea.l 12(a6),dst          // Load the dst ptr into A1
            clr.w   rows                // Load rows into D2
            move.b  18(a6),rows
            movea.l 20(a6),palette      // Load the palette ptr in A4

            movea.l  dst,start

            #ifdef VNC_FB_BITS_PER_PIX
                move.b #VNC_FB_BITS_PER_PIX,depth
            #else
                move.l fbDepth,depth
            #endif

            /***********************************************************
             * If monitor depth is eight, not supported
             ***********************************************************/
        test8bit:
            cmp.b #8,depth              // Is the screen depth 8?
            bne test4bit
            bra notImplemented

            /***********************************************************
             * If monitor depth is four, simply copy 8 bytes at a time
             ***********************************************************/
        test4bit:
            cmp.b #4,depth              // Is the screen depth 4?
            bne test2bit
            bsr writePalette
            #ifndef VNC_BYTES_PER_LINE
                movea.w fbStride,stride
                suba.w #8,stride
            #else
                movea.w #(VNC_BYTES_PER_LINE - 8),stride
            #endif
            // Outer loop, process all rows
            bra depth2
        copy2:
            move.l (src)+,(dst)+        // Copy four bytes
            move.l (src)+,(dst)+        // Copy four bytes
            adda.l stride,src           // Move to next row
        depth2:
            dbra rows, copy2
            bra done

            /***********************************************************
             * If monitor depth is two, not implemented
             ***********************************************************/
        test2bit:
            cmp.b #2,depth              // Is the screen depth 2?
            bne test1bit
            bra notImplemented

            /***********************************************************
             * If monitor depth is one, not implemented
             ***********************************************************/
        test1bit:
            bra notImplemented

        done:
            // Copy number of bytes written to dst to return value
            suba.l   start,dst
            move.l   dst,d0

        exit:
            movem.l (a7)+,a2-a4         // Restore registers
            unlk    a6
            rts

        notImplemented:
            move.b  16(a6),d0
            bra exit

            /***********************************************************
             * Write tile type and palette
             ***********************************************************/

        writePalette:
            cmpa.w #0,palette            // Do we have a palette?
            beq defaultPalette
            move.b  (palette),color1     // Make a copy of the 1st color
            move.b #16,(dst)+            // Packed tile type
            move.l (palette)+,(dst)+     // Copy 16 bytes of palette
            move.l (palette)+,(dst)+
            move.l (palette)+,(dst)+
            move.l (palette)+,(dst)+
            rts
        defaultPalette:
            cmp.b #16,lastPaletteSize
            beq reusePalette
            move.b #16,(dst)+            // Packed tile type
            move.l #0x00010203,(dst)+
            move.l #0x04050607,(dst)+
            move.l #0x08090A0B,(dst)+
            move.l #0x0C0D0E0F,(dst)+
            rts
        reusePalette:
            move.b #127,(dst)+          // Packed tile type with reused palette
            rts

            #undef src
            #undef dst
            #undef start
            #undef stride
            #undef palette
            #undef rows
            #undef depth
            #undef color1
        }

        /**
         * With "src" pointing to the first byte of a 16x16 tile on the screen, this function will write
         * "rows" as a raw tile and return the number of bytes written.
         */
        static asm unsigned short encodeTileRaw(const unsigned char *src, unsigned char *dst, unsigned short size, unsigned char rows) {
            machine 68020
            /*
             * Register Assignments:
             *   A0                      : Source ptr
             *   A1                      : Destination ptr
             *   A2                      : Copy of dest ptr
             *   A3                      : Linestride of screen
             *   A4                      : Unused
             *   A5                      : Application globals
             *   A6                      : Link for debugger
             *   A7                      : Stack ptr
             *   D0                      : Rows to process
             *   D1                      : Depth of screen
             *   D2,D3,D4,D5             : Copy buffer
             *   D6,D7                   : Unused
             */

            #define src     a0
            #define dst     a1
            #define start   a2
            #define stride  a3
            #define rows    d0
            #define depth   d1
            #define tmp2    d2
            #define tmp3    d3
            #define tmp4    d4
            #define tmp5    d5

            link    a6,#0000            // Link for debugger
            movem.l d3-d5/a2-a3,-(a7)   // Save registers

            movea.l 8(a6),src           // Load the src ptr into A0
            movea.l 12(a6),dst          // Load the dst ptr into A1
            clr.w   rows                // Load rows into D2
            move.b  18(a6),rows

            movea.l  dst,start

            #ifdef VNC_FB_BITS_PER_PIX
                move.b #VNC_FB_BITS_PER_PIX,depth
            #else
                move.l fbDepth,depth
            #endif

            /***********************************************************
             * If monitor depth is eight, copy the raw time
             ***********************************************************/
        test8bit:
            cmp.b #8,depth              // Is the screen depth 8?
            bne notImplemented
            move.b #0,(dst)+            // Raw tile type
            #ifndef VNC_BYTES_PER_LINE
                movea.w fbStride,stride
                suba.w #16,stride
            #else
                movea.w #(VNC_BYTES_PER_LINE - 16),stride
            #endif
            bra depth8
        copy16:
            movem.l (src)+,d2-d5        // Copy 16 bytes
            movem.l d2-d5,(dst)
            adda.l #16,dst
            adda.l stride,src           // Move to next row
        depth8:
            dbra rows, copy16

        done:
            // Copy number of bytes written to dst to return value
            suba.l   start,dst
            move.l   dst,d0

        exit:
            movem.l (a7)+,d3-d5/a2-a3   // Restore registers
            unlk    a6
            rts

        notImplemented:
            move.b  16(a6),d0
            bra exit

            #undef src
            #undef dst
            #undef start
            #undef stride
            #undef rows
            #undef depth
            #undef tmp2
            #undef tmp3
            #undef tmp4
            #undef tmp5
        }

        static unsigned short encodeTileSolid(unsigned char *dst, unsigned char color) {
            *dst++ = TileSolid;
            *dst++ = color;
            return 2;
        }

        static unsigned char typeColor(unsigned char type) {
            #ifdef VNC_FB_BITS_PER_PIX
                const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
            #endif
            switch(fbDepth) {
                case 8:
                    switch(type) {
                        case 0: return 0;
                        case 1: return 16;
                        case 2: return 32;
                        case 4: return 48;
                        case 16: return 64;
                        case 128: return 80;
                        default: return 96;
                    }
                case 4:
                    switch(type) {
                        case 0: return 0;
                        case 1: return 1;
                        case 2: return 4;
                        case 4: return 6;
                        case 16: return 8;
                        case 128: return 11;
                        default: return 14;
                    }
                case 2:
                    switch(type) {
                        case 0: return 0;
                        case 1: return 2;
                        case 2: return 1;
                        case 4: return 3;
                        case 16: return 0;
                        case 128: return 0;
                        default: return 0;
                    }
                case 1:
                    switch(type) {
                        case 0: return 0;
                        case 1: return 0;
                        case 2: return 1;
                        case 4: return 0;
                        case 16: return 0;
                        case 128: return 1;
                        default: printf("Invalid type: %d", type); return 1;
                    }
                default:
                    return 0;
            }
        }

        static unsigned short encodeTile(const unsigned char *src, unsigned char *dst, char rows) {
            int len = 1024;
            #ifdef USE_RLE_COLOR_TILES
                #ifdef VNC_FB_BITS_PER_PIX
                    const unsigned long fbDepth = VNC_FB_BITS_PER_PIX;
                #endif
                #ifdef USE_RLE_EARLY_ABORT
                    // When USE_RLE_EARLY_ABORT is defined, set the maximum number of bytes to
                    // write for an RLE tile to the size of the equivalent paletted or raw tile
                    const unsigned char *end = dst +
                    #if defined(VNC_FB_256_COLORS)
                        259;
                    #elif defined(VNC_FB_16_COLORS)
                        147;
                    #elif defined(VNC_FB_4_COLORS)
                        71;
                    #elif defined(VNC_FB_2_COLORS)
                        37;
                    #else
                        3 + 32 * fbDepth + ((fbDepth != 8) ? (1 << fbDepth) : 0);
                    #endif
                #else
                    const unsigned char *end = 0;
                #endif
                #ifndef RLE_GATHERS_PALETTE
                    len = encodeTilePlainRLE(src, dst, end, rows);
                #else
                    unsigned char palette[17];
                    len = encodeTilePlainRLE(src, dst, end, rows, palette);
                    const unsigned char tileColors =
                        (palette[0] != palette[4]) ? 5 : // Tile has greater than 4 colors
                        (palette[0] != palette[3]) ? 4 : // Tile has four colors
                        (palette[0] != palette[2]) ? 3 : // Tile has three colors
                        (palette[0] != palette[1]) ? 2 : // Tile has two colors
                                                     1;  // Tile is solid
                    switch(tileColors) {
                        case 5:
                            #if defined(USE_16_COLOR_TILES)
                                if(fbDepth == 4) break; // Use a native tile instead
                                if((len > 145) && (len != (end - dst))) {
                                    const int nColors = countRLEColors(dst, palette, len);
                                    if(nColors <= 16 && len > 129 + nColors) {
                                        len = encodeTileSixteenColor(src, dst, len, rows, palette);
                                    }
                                }
                            #endif
                            break;
                        case 3:
                        case 4:
                            #if defined(USE_4_COLOR_TILES)
                                if(fbDepth == 2) break; // Use a native tile instead
                                if(len > 68) {
                                    len = encodeTileFourColor(src, dst, len, rows, palette);
                                }
                            #endif
                            break;
                        case 2:
                            #if defined(USE_2_COLOR_TILES)
                                if(fbDepth == 1) break; // Use a native tile instead
                                if(len > 35) {
                                    len = encodeTileTwoColor(src, dst, len, rows, palette);
                                }
                            #endif
                            break;
                        case 1:
                            if(len > 2) {
                                len = encodeTileSolid(dst, palette[0]);
                            }
                            break;
                    }
                #endif // RLE_GATHERS_PALETTE
            #endif // USE_RLE_COLOR_TILES
            #if defined(USE_NATIVE_COLOR_TILES)
                if((fbDepth == 4) && (len > 145)) {
                    len = encodeTileSixteenColor(src, dst, len, rows, 0);
                }
                if((fbDepth == 2) && (len > 69)) {
                    len = encodeTileFourColor(src, dst, len, rows, 0);
                }
                if((fbDepth == 1) && (len > 35)) {
                    len = encodeTileTwoColor(src, dst, len, rows, 0);
                }
            #endif
            #if defined(USE_NATIVE_COLOR_TILES) || defined(USE_RAW_COLOR_TILES)
                if((fbDepth == 8) && (len > 257)) {
                    len = encodeTileRaw(src, dst, len, rows);
                }
            #endif
            #ifdef DEBUG_TILE_TYPES
                unsigned char type = dst[0];
                if(type == 127) {
                    type = lastPaletteSize;
                }
                if(dst == fbUpdateBuffer) {
                    switch((tile_y / 16) % 8) {
                        case 0:  type = 0; break;
                        case 1:  type = 1; break;
                        case 2:  type = 2; break;
                        case 3:  type = 4; break;
                        case 4:  type = 16; break;
                        case 5:  type = 128; break;
                        default: type = 0; break;
                    }
                }
                len = encodeTileSolid(dst, typeColor(type));
            #endif
            if(*dst != TileReuse) {
                lastPaletteSize = *dst;
            }
            return len;
        }

        #define min(A,B) ((A) < (B) ? (A) : (B))

        asm Boolean getChunkMonochrome(int x, int y, int w, int h, wdsEntry *wdsPtr);

        Boolean VNCEncoder::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
            #ifdef USE_FAST_MONO_ENCODER
                if(fbDepth == 1) return getChunkMonochrome(x,y,w,h,wds);
            #endif

            unsigned char *dst = fbUpdateBuffer, *src, rows, tiles = UPDATE_MAX_TILES;

            goto beginLoop;

            while(tiles-- && (dst - fbUpdateBuffer <= (UPDATE_BUFFER_SIZE - TILE_MAX_SIZE))) {
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
