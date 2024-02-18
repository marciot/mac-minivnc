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

#include "VNCFrameBuffer.h"
#include "VNCEncodeTiles.h"

#if USE_ASM_CODE

/**
 * With "src" pointing to the first byte of a tile on the screen, this function will copy the data
 * "dst" and returns the number of bytes written. Although tile rows are written end-to-end, no
 * change is done to the color values, i.e. they are stored exactly as in the "native" framebuffer.
 * The tile size in pixels is "rows" and "cols", but this function requires that each row be multiple
 * of 2 bytes -- i.e. a tile must be at least 16 pixels across in 1-bit mode, but may be as few as
 * two pixels across in 8-bit mode.
 *
 * The colorInfo argument is ignored.
 *
 * a.k.a "Experimental::writeScreenTileAsNative_9"
 */
asm unsigned short screenToNative(const unsigned char *src, unsigned char *dst, short rows, short cols, ColorInfo *colorInfo) {
    machine 68020
    /*
     * Register Assignments:
     *   A0                      : Source ptr
     *   A1                      : Destination ptr
     *   A2                      : Linestride of screen
     *   A3                      : Loop entry point
     *   A4                      : Unused
     *   A5                      : Application globals
     *   A6                      : Link for debugger
     *   A7                      : Stack ptr
     *   D0                      : Temporary storage
     *   D1                      : Depth of screen
     *   D2                      : Words in row
     *   D3                      : Rows left in tile
     *   D4                      : Unused
     *   D5                      : Unused
     *   D6                      : Unused
     *   D7                      : Unused
     */

    #define srcArg       8(a6)
    #define dstArg      12(a6)
    #define rowArg      16(a6)
    #define colArg      18(a6)
    #define cInfoArg    20(a6)

    #define src            a0
    #define dst            a1
    #define stride         a2
    #define copyEntry      a3

    #define tmp            d0
    #define depth          d1
    #define wordsInRow     d2
    #define rowsLeft       d3

    link    a6,#0000             // Link for debugger
    movem.l d3/a2-a3,-(a7)       // Save registers

    movea.l  srcArg,src          // Load the src ptr into A0
    movea.l  dstArg,dst          // Load the dst ptr into A1

    #ifdef VNC_FB_BITS_PER_PIX
        move.l #VNC_FB_BITS_PER_PIX,depth
    #else
        move.l fbDepth,depth
    #endif
    #ifndef VNC_BYTES_PER_LINE
        movea.w fbStride,stride
    #else
        movea.w #VNC_BYTES_PER_LINE,stride
    #endif

    // wordsInRow = cols * (fbDepth / 8) / sizeof(unsigned short)
    move.w     colArg, wordsInRow
    mulu.w      depth, wordsInRow
    lsr.w          #4, wordsInRow

    move.w     rowArg, rowsLeft
    subq.w         #1, rowsLeft

    // Adjust the stride
    suba.w wordsInRow, stride
    suba.w wordsInRow, stride

    // Precompute address to jump to copy the
    // proper number of long words in a row
    move.w wordsInRow,tmp
    bclr #0,tmp
    neg.w tmp
    lea lastMove(tmp), copyEntry

    // Do we have an odd word left over?
    #define hasOddWord wordsInRow
    add.w tmp,hasOddWord

    suba.w stride, src // Prime the loop

copyTileRow:
    adda.w stride, src // Move to next row

    // Jump to address to copy the appropriate number of bytes
    jmp (copyEntry)

    // A tile can have a max of 64 bytes per row
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
    move.l (src)+,(dst)+
lastMove:
    // Do we have an odd word to copy?
    tst.b hasOddWord
    bne copyOddWord

    dbra rowsLeft, copyTileRow
    bra done

copyOddWord:
    move.w (src)+,(dst)+
    dbra rowsLeft, copyTileRow

done:
    // Copy number of bytes written to dst to return value
    suba.l   dstArg, dst
    move.l   dst, d0

exit:
    movem.l (a7)+, d3/a2-a3  // Restore registers
    unlk    a6
    rts

    #undef tmp
    #undef depth
    #undef wordsInRow
    #undef rowsLeft

    #undef stride
    #undef copyEntry
    #undef src
    #undef dst

    #undef srcArg
    #undef dstArg
    #undef rowArg
    #undef colArg
    #undef cInfoArg
}

/**
 * With "src" pointing to the first byte of a tile written by "screenToNative"
 * in 8-bit color depth, count the colors and populate the ColorInfo structure.
 *
 * WARNING: Internally this function operates in chunks that are 4 bytes long
 * and will hang if the input data is not a multiple of four. Since the VNC
 * server dirtyRect routine also operates in 32-bit values, this is presumed
 * to not cause a problem.
 *
 * a.k.a. Experimental::tallyColors256_17
 */
static asm unsigned short native256ToColors(const unsigned char *start, unsigned char *end, ColorInfo *colorInfo) {
    machine 68020

    #define srcArg       8(a6)
    #define endArg      12(a6)
    #define cInfoArg    16(a6)

    #define src           a0
    #define end           a1
    #define table         a2

    #define tmp           d0
    #define colors        d1
    #define color         d2
    #define lastColors    d3

    link    a6,#0000             // Link for debugger
    movem.l d3-d6/a2,-(a7)       // Save registers

    movea.l  srcArg,src          // Load the src ptr into A0
    movea.l  endArg,end          // Load the end ptr into A1

    /********************************************************************
     * Stage 1: Tally the colors into a 256 bit bit-field               *
     ********************************************************************/

    // Clear the table
    movea.l endArg,table
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    clr.l (table)+
    movea.l endArg,table

    clr.l color
    move.l (src), lastColors     // Make sure we don't skip the first colors
    not.l lastColors

colorTallyLoop:
    move.l (src)+, colors

    cmp.l lastColors,colors      // Do not count if we are seeing the same colors as before
    beq nextColor
    move.l colors,lastColors

// Tally the first color

    move.b colors,color
    bfset (table) {color:1} // Tally the color

// Check whether all four bytes are the same
    ror.l #8,colors
    cmp.l colors,lastColors
    beq nextColor

// We have three additional colors to tally
    move.b colors,color
    lsr.l #8,colors
    bfset (table) {color:1} // Tally the color

    move.b colors,color
    lsr.l #8,colors
    bfset (table) {color:1} // Tally the color

    move.b colors,color
    bfset (table) {color:1} // Tally the color

nextColor:
    cmpa.l src,end
    bne colorTallyLoop

    #undef src
    #undef end
    #undef tmp
    #undef colors
    #undef color
    #undef lastColors

    /********************************************************************
     * Stage 2: Write out color table and color mapping table           *
     ********************************************************************/

    #define tmp       d0
    #define nCols     d1
    #define color     d2
    #define offset    d3
    #define bits      d4
    #define baseColor d5
    #define immed32   d6

    #define cInfo     a0

    moveq.l #32,immed32

writeColorTable:
    movea.l cInfoArg,cInfo      // Load the cInfo ptr into A0
    clr.w baseColor
    clr.w nCols

wcrReadLongWord:
    move.l (table)+,bits
    beq wcrNextLongWord

// Tally up the bits
    clr.l offset

wcrFindNextBit:
    // Use bfffo to find next set color bit
    move.l immed32,tmp
    sub.l offset,tmp
    bfffo bits {offset:tmp},offset
    beq wcrNextLongWord

    move.w baseColor,color
    add.w offset,color

    addq.b #1,offset

// Add color to the colormap

    // colorInfo->colorMap[color] = nColors & 0x7F;
    move.b nCols, struct(ColorInfo.colorMap)(cInfo,color.w)
    //colorInfo->colorPal[nColors & 0x7F] = color;
    move.b color, struct(ColorInfo.colorPal)(cInfo,nCols.w)
    addq.w #1,   nCols
    andi.b #0x7F,nCols

    cmp.b immed32,offset // Have we tested all 32 color bits?
    bne wcrFindNextBit

wcrNextLongWord:
    add.b immed32, baseColor // Add 32 to baseColor
    bcc wcrReadLongWord // Stop when baseColor = 256 (which causes a carry)

// Copy values to the colorInfo struct
    move.w nCols, struct(ColorInfo.nColors)(cInfo)

    /********************************************************************/

    #undef table
    #undef tmp
    #undef nCols
    #undef color
    #undef offset
    #undef bits
    #undef baseColor
    #undef immed32
    #undef cInfo

done:

exit:
    movem.l (a7)+, d3-d6/a2    // Restore registers
    unlk    a6
    rts

    #undef srcArg
    #undef endArg
    #undef cInfoArg
}

/**
 * With "src" pointing to the first byte of a tile written by "screenToNative" in
 * 4,2 or 1-bit color depth, count the colors and populate the ColorInfo structure.
 *
 * Internally this function operates in chunks that are 4 bytes long. If the input
 * data length is divisible by two, but not by four, two additional bytes will be
 * written to "end" to pad the data. Input data which is not divisible by two or
 * four bytes is unsupported.
 *
 * a.k.a. Experimental::tallyColorsFewer17_2
 */
static asm unsigned short native16ToColors(const unsigned char *start, unsigned char *end, ColorInfo *colorInfo) {
    machine 68020

    #define srcArg       8(a6)
    #define endArg      12(a6)
    #define cInfoArg    16(a6)

    #define src           a0
    #define end           a1
    #define testEntry     a2

    #define depth         d0
    #define colors        d1
    #define color         d2
    #define lastColors    d3
    #define table         d4
    #define nPix          d5
    #define mask          d6

    link    a6,#0000             // Link for debugger
    movem.l d3-d6/a2-a5,-(a7)    // Save registers

    movea.l  srcArg,src          // Load the src ptr into A0
    movea.l  endArg,end          // Load the end ptr into A1

    #ifdef VNC_FB_BITS_PER_PIX
        move.l #VNC_FB_BITS_PER_PIX,depth
    #else
        move.l fbDepth,depth
    #endif

    /********************************************************************
     * Stage 1: Tally the colors into a 32 bit bit-field                *
     ********************************************************************/

    // Clear the table
    clr.l table

testDepth8:
    cmpi.b #8,depth
    beq unsupported

testDepth4:
    cmpi.b #4,depth
    bne testDepth2
    move.w #8,nPix
    bra prepareLoopCounters

testDepth2:
    cmpi.b #2,depth
    bne testDepth1
    move.w #16,nPix
    bra prepareLoopCounters

testDepth1:
    move.w #32,nPix

prepareLoopCounters:

    // Mask for rightmost color in block
    moveq.l     #1,mask  // mask = (1 << depth) - 1
    lsl.l    depth,mask
    subq.l      #1,mask

    // Make sure we don't skip the first colors
    clr.l color
    move.l (src), lastColors
    not.l lastColors

colorTallyLoop:
    move.l (src)+, colors

    cmp.l lastColors,colors      // Do not count if we are seeing the same colors as before
    beq nextColor
    move.l colors,lastColors

// Check whether all colors in the longword are the same
    move.l colors, color
    rol.l   depth, color
    cmp.l  colors, color
    beq oneUniqueColor

    // If depth is 4, there are 8 colors in the longword to test

    cmpi.b #4, depth
    beq testEight

    // If depth is 2, there are 16 colors in the longword to test

    cmpi.b #2, depth
    beq testSixteen

    // If depth is 1, not all pixels in word are equal,
    //  we know the only two color have been seen

    moveq.l #3,table
    bra writeColorTable

testSixteen:
    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 16th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 15th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 14th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 13th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 12th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 11th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 10th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 9th color

testEight:
    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 8th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 7th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 6th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 5th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 4th color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 3rd color

    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 2nd color

oneUniqueColor:
    move.b colors, color
    and.b    mask, color
    lsr.l   depth, colors
    bset    color, table    // Tally the 1st color

nextColor:
    cmpa.l src,end
    bne colorTallyLoop

    #undef src
    #undef end
    #undef colors
    #undef color
    #undef lastColors
    #undef nPix
    #undef mask
    #undef testEntry
    #undef depth

    /********************************************************************
     * Stage 2: Write out color table and color mapping table           *
     ********************************************************************/

    #define nCols     d0
    #define tmp       d1
    #define color     d2
    #define immed32   d3

    #define cInfo     a0

writeColorTable:
    movea.l cInfoArg,cInfo      // Load the cInfo ptr into A0
    moveq.l #32,immed32
    clr.w color
    clr.w nCols

wcrFindNextBit:
    btst color,table
    beq wcrNextColor

wcrTallyColor:
    // Add color to the colormap

    // colorInfo->colorMap[color] = nColors & 0x7F;
    move.b nCols, struct(ColorInfo.colorMap)(cInfo,color.w)
    //colorInfo->colorPal[nColors & 0x7F] = color;
    move.b color, struct(ColorInfo.colorPal)(cInfo,nCols.w)
    addq.w #1,   nCols
    andi.b #0x7F,nCols

wcrNextColor:
    addq.b #1,color     // Move to the next color
    cmp.b immed32,color // Have we tested all 32 color bits?
    bne wcrFindNextBit
    bra wcrDone

unsupported:
    clr.l nCols

wcrDone:
// Copy values to the colorInfo struct
    move.w nCols, struct(ColorInfo.nColors)(cInfo)
    bra exit

    /********************************************************************/

exit:
    movem.l (a7)+, d3-d6/a2-a5  // Restore registers
    unlk    a6
    rts

    #undef srcArg
    #undef endArg
    #undef cInfoArg
    #undef table
    #undef nCols
    #undef tmp
    #undef color
    #undef immed32
    #undef cInfo
}

/**
 * With "src" pointing to the first byte of VNC tile in 8, 4, 2 or 1-bit
 * format, count the colors in the tile and populate the ColorInfo structure:
 *
 *    nColors: Count of unique colors in data
 *    colorPal: List of unique color values
 *    colorMap: Mapping table from native color values to colorPal indices
 *
 * For 8-bit data, the input data length must be a multiple of four bytes.
 * For 4, 2 or 1-bit data, it may be a multiple of four or two bytes, but
 * an additional two padding bytes will written to "end" as padding.
 */
unsigned short nativeToColors(const unsigned char *start, unsigned char *end, ColorInfo *colorInfo) {
    return (fbDepth == 8)
        ? native256ToColors(start, end, colorInfo)
        : native16ToColors(start, end, colorInfo);
}

/**
 * With "src" pointing to the first byte of a tile written by "screenToNative" in
 * 4,2 or 1-bit color (as specified by "depth") this function will write RLE
 * encoded data "dst" and return the number of bytes written. Data is read from
 * "src" to "end", or until "dst" exceeds "stop" (this latter condition signals
 * an premature abort and will invalidate the output results).
 *
 * Internally this function operates in chunks that are 4 bytes long. If the input
 * data length is divisible by two, but not by four, two additional bytes will be
 * written to "end" to pad the data. Input data which is not divisible by two or
 * four bytes is unsupported.
 *
 * The format of the RLE data written to dst is controlled by fields of the
 * "ColorInfo" structure. If "packBits" is non-zero, data will be written in the
 * TRLE "packed" format. If "nColors" is less than the native screen depth, the
 * "colorMap" will be applied to each color value prior to it being written.
 * Lastly, although a single byte will be written for each color value, the
 * destination pointer will be incremented by "colorSize" -- this can be used to
 * reserve space for later conversion of indexed color values into true-color.
 * On exit, the count of runs of exactly one pixel will be stored in runsOfOne.
 *
 * a.k.a. Experimental::RLE_FromNativeNew_5
 */
asm unsigned short nativeToRle(const unsigned char *src, unsigned char *end, unsigned char *dst, const unsigned char *stop, unsigned char depth, ColorInfo *cInfo) {
    machine 68020
    /*
     * Register Assignments:
     *   A0                      : Source ptr
     *   A1                      : Destination ptr
     *   A2                      : Maximum value to read
     *   A3                      : Carry forward of data
     *   A4                      : Color info counters
     *   A5                      : Application globals
     *   A6                      : Link for debugger
     *   A7                      : Stack ptr
     *   D0                      : Temporary storage
     *   D1                      : Depth of screen
     *   D2                      : Number of pixels per long word
     *   D3                      : Outer and inner loop variables
     *   D4                      : Current block of pixels
     *   D5                      : Mask for extracting pixels
     *   D6                      : Pattern for testing pixels
     *   D7                      : RLE value and count
     */

    #define srcArg     8(a6)
    #define endArg    12(a6)
    #define dstArg    16(a6)
    #define stopArg   20(a6)
    #define depthArg  24(a6)
    #define cInfoArg  26(a6)

    #define src       a0
    #define end       a1
    #define dst       a2
    #define stop      a3
    #define carry     a4
    #define tmpA      a5

    #define tmp       d0

    #define depthPair d1
    #define depth     depthPair
    #define runsofOne depth      // bit 16-08 = runsofOne

    #define nPix      d2         // bit 15-0  = nPix
    #define pak_b31   nPix       // bit 31    = packRuns
    #define pad_b30   nPix       // bit 30    = gotPadding
    #define map_b29   nPix       // bit 29    = mapColors
    #define rleSiz    d7{3:2}    // bit 28-27 = rleSiz

    #define loopPair  d3
    #define loop      loopPair   // rleVal : loop

    #define curr      d4
    #define mask      d5
    #define test      d6
    #define rleCnt    d7

    link    a6,#0000             // Link for debugger
    movem.l d3-d7/a2-a5,-(a7)    // Save registers

    movea.l  srcArg,src          // Load the src ptr into A0
    movea.l  endArg,end          // Load the end ptr into A1
    movea.l  dstArg,dst          // Load the dst ptr into A2
    movea.l  stopArg,stop        // Load the stop ptr into A3

    clr.l    depth
    move.b   depthArg,depth      // Load the depth into D1

// Initialize next, loop counters and nPix

    clr.l nPix

testDepth8:
    cmpi.b #8,depth
    bne testDepth4
    move.w #4,nPix
    bra prepareLoopCounters

testDepth4:
    cmpi.b #4,depth
    bne testDepth2
    move.w #8,nPix
    bra prepareLoopCounters

testDepth2:
    cmpi.b #2,depth
    bne testDepth1
    move.w #16,nPix
    bra prepareLoopCounters

testDepth1:
    move.w #32,nPix

prepareLoopCounters:
    movea.l cInfoArg, tmpA

    // nativeColors = (1 << fbDepth)
    moveq.w   #1,tmp
    lsl.w  depth,tmp

    // mapColors  = (cInfo->nColors < nativeColors);
    // notMapColors = (nativeColors <= cInfo->nColors)
    cmp.w struct(ColorInfo.nColors)(tmpA),tmp
    bls notMapping
    bset #29,map_b29
notMapping:

    tst.b  struct(ColorInfo.packRuns)(tmpA)
    beq notPacking
    bset #31,pak_b31 // set the packRuns flag
notPacking:
    // Mask for rightmost color in block
    moveq.l     #1,mask  // mask = (1 << depth) - 1
    lsl.l    depth,mask
    subq.l      #1,mask

    // Load first block into current
    move.l (src), curr

    movea.l cInfoArg, carry
    clr.l tmp
    move.b  struct(ColorInfo.colorSize)(carry),tmp
    bfins tmp, rleSiz

    // Move leftmost pixel in current into rleVal

    #define rleVal loopPair
    move.l    curr,rleVal
    rol.l    depth,rleVal
    and.l     mask,rleVal
    swap           loopPair // Move the value into the high-word
    #undef rleVal

    move.w     #-1,rleCnt

    // Load leftmost pixel in current into carry
    move.l    mask,tmp
    ror.l    depth,tmp
    and.l     curr,tmp
    movea.l    tmp,carry

    // We need to make sure the length of the data
    // is divisible by four, adding two bytes of padding
    // if necessary

    // gotPadding = (end - src) % sizeof(unsigned long)
    move.l  end,tmp
    sub.l   src,tmp
    andi.l   #3,tmp
    beq readNextLongword

    // Add an extra zero word for padding
    clr.w (end)+
    bset #30,pad_b30 // set the gotPadding flag

/////////////////////////////////
// Outer loop, process all blocks
readNextLongword:
    // Fetch a block of pixels from the screen
    move.l (src)+, curr

    cmp.l end,src // if(src > end) goto writeLastRle;
    bhi writeRleLast

    // Make a pattern for testing all the
    // pixels for equality

    move.l  curr,test
    lsr.l  depth,test
    move.l carry,tmp
    or.l     tmp,test

    // Now check whether all pixels in block are equal
    eor.l   curr,test
    bne pixelsUnequal
    // All pixels are equal in block
    add.w   nPix,rleCnt
    bra readNextLongword

pixelsUnequal:
    // Compute the carry into the next block
    move.l   curr,tmp
    and.l    mask,tmp
    ror.l   depth,tmp
    movea.l   tmp,carry

    // Prepare loop counter
    clr.w        loop
    move.b  nPix,loop    // n = npix - 1;
    subi.b    #1,loop

    //*********** Inner loop ***********/
findNextPixel:
    // Copy left-most pixel to tmp while
    // testing whether it is zero
    rol.l depth,test
    move.l test,tmp
    and.l mask,tmp
    bne writeRle

    // Pixel match, just increment the RLE count
    addq.w #1, rleCnt
writeRleReturn:
    eor.l tmp,test              // Exit early if remaining pixels are a run...
    dbeq loop, findNextPixel
    //*********** Inner loop ***********/
    add.w loop, rleCnt          // ...and update run count with remaining pixels
    bra readNextLongword

writeRleLast:
    move.l dst, stop // done = true

    // Adjustment for when we end up with 16 zero pixels as padding.
    btst #30,pad_b30
    beq writeRle

    cmp.w #15,rleCnt            // if(rleCnt < 15) goto skipLastRLEPair;
    bcs skipLastRLEPair

    subi.w  #16,rleCnt
    bne writeRle                // if(rleCnt == 0) runsOfOne--;
    sub.l #0x0100,runsofOne

writeRle:
    swap loopPair
    #undef loop
    #define rleVal loopPair

    // Lookup the RLE value in mapping table

    move.b rleVal,(dst)        // Write the color value w/o mapping

    btst #29,map_b29
    beq noColorMapping

    movea.l cInfoArg,tmpA      // Load the cInfo ptr into A0
    move.b struct(ColorInfo.colorMap)(tmpA,rleVal.w),(dst)

noColorMapping:
    // Write the RLE pair...
    //btst #31,pak_b31
    //beq noPackSkipByRleSize
    tst.l pak_b31
    bpl noPackSkipByRleSize

    tst.w rleCnt
    beq runOfOne

runOfMany:
    // If we are packing, set the high bit to indicate a
    // run length of more than one
    ori.b #0x80,(dst)+
    bra writeRle255

runOfOne:
    adda.w #1,dst
    add.l #0x0100,runsofOne     // runsofOne++
    bra writeRleDone

noPackSkipByRleSize:
    // Skip forwards by rleSiz
    movea.l tmp,tmpA            // Save the tmp value
    bfextu rleSiz,tmp
    adda.w tmp,dst
    move.l tmpA,tmp             // Restore the tmp value

writeRle255:
    cmpi.w #255,rleCnt          // While rleCnt >= 255
    bcs writeRleFinalCount
    move.b #255,(dst)+          // ...write a 255
    subi.w #255,rleCnt
    bra writeRle255
writeRleFinalCount:
    move.b rleCnt,(dst)+        // ...write the count
    clr.w rleCnt                // ...reset the count (to 0, which means one)

writeRleDone:
    eor.w tmp,rleVal            // ...update the RLE value

    swap loopPair
    #undef rleVal
    #define loop loopPair

    cmpa.l stop,dst             // If dst <= stop, then abort early
    bls writeRleReturn          // Have we processed all blocks?

skipLastRLEPair:
/////////////////////////////////////

    #undef carry
    #undef curr
    #undef mask
    #undef test
    #undef rleCnt
    #undef loopPair
    #undef loop
    #undef end

    #undef nPix
    #undef pak_b31
    #undef pad_b30
    #undef map_b29
    #undef rleSiz

// ================== GATHER COLOR INFO ==================
writeColorInfo:

    // Gather color info, if requested
    tst.l cInfoArg
    beq done

    // Copy values to the colorInfo struct

    lsr.l   #8,runsofOne
    movea.l cInfoArg, src
    move.w  runsofOne, struct(ColorInfo.runsOfOne)(src)

// ================== GATHER COLOR INFO ==================

done:
    // Copy number of bytes written to dst to return value
    suba.l   dstArg, dst
    move.l   dst, d0

exit:
    movem.l (a7)+, d3-d7/a2-a5  // Restore registers
    unlk    a6
    rts

    #undef src
    #undef dst
    #undef stop

    #undef tmpA
    #undef tmp

    #undef depthPair
    #undef depth
    #undef runsofOne

    #undef srcArg
    #undef endArg
    #undef dstArg
    #undef stopArg
    #undef depthArg
    #undef cInfoArg
}

/**
 * With "src" pointing to the first byte of a tile written by "screenToNative" in
 * 4,2 or 1-bit color (as specified by "srcDepth") this function will downsample
 * the data to "dstDepth", replacing each source color value with the corresponding
 * values in "ColorInfo->colorMap".
 *
 * Data is read from "src" to "end", but because this function operates in chunks
 * that are 4 bytes long, it may overshoot reading by up to 3 bytes. While this may
 * cause extra bytes to be written to dst, the return value is adjusted to the
 * value expected as if this had not happened.
 *
 * a.k.a. Experimental::packTile_8
 */
asm unsigned short nativeToPacked(const unsigned char *src, unsigned char *dst, const unsigned char* end, const char srcDepth, const char dstDepth, ColorInfo *colorInfo) {
    machine 68020
    /*
     * Register Assignments:
     *   A0                      : Source ptr
     *   A1                      : Destination ptr
     *   A2                      : Limit for source ptr
     *   A3                      : Pointer to color info
     *   A4                      : Source depth
     *   A5                      : Destination depth
     *   A6                      : Link for debugger
     *   A7                      : Stack ptr
     *   D0                      : Temporary
     *   D1                      : Source offset
     *   D2                      : Output long word
     *   D3                      : Loop counter
     *   D4                      : Source depth
     *   D5                      : Destination depth or color
     */

    #define srcArg       8(a6)
    #define dstArg      12(a6)
    #define endArg      16(a6)
    #define srcDeArg    20(a6)
    #define dstDeArg    22(a6)
    #define cInfoArg    24(a6)

    #define src         a0
    #define dst         a1
    #define end         a2
    #define cInfo       a3
    #define endOffset   a4

    #define tmp         d0
    #define srcOffset   d1
    #define dstOutput   d2
    #define loop        d3
    #define srcDepth    d4
    #define dstDepth    d5
    #define color1      d5

    link    a6,#0000           // Link for debugger
    movem.l d3-d5/a2-a4,-(a7)  // Save registers

    movea.l   srcArg, src      // Load the src ptr into A0
    movea.l   dstArg, dst      // Load the dst ptr into A1
    movea.l   endArg, end      // Load the end ptr into A2
    movea.l cInfoArg, cInfo    // Load the cInfo ptr into A3

    clr.l             srcDepth
    move.b  srcDeArg, srcDepth

    clr.l             dstDepth
    move.b  dstDeArg, dstDepth

    clr.l            srcOffset

    move.l      end, tmp
    sub.l       src, tmp
    lsl.l        #3, tmp
    movea.l     tmp, endOffset

/* Destination depth is four */
test4bit:
    cmp.b        #4, dstDepth             // Is the output depth 4?
    bne test2bit

    // Outer loop, process all long words
copyLoop4:
    clr.l            dstOutput
    // Inner loop, process 7 pixels
    moveq.w      #7, loop // Max of 7 pixels per long word
wordLoop4:
    bfextu (src){srcOffset:srcDepth}, tmp // Read a pixel
    add.l  srcDepth, srcOffset
    lsl.l        #4, dstOutput            // Lookup pixel in table
    or.b struct(ColorInfo.colorMap)(cInfo,tmp.w),dstOutput
    dbra        loop, wordLoop4           // Last pixel of longword?

    // End of inner loop
    move.l dstOutput, (dst)+               // Write out the longword
    cmp.l  endOffset, srcOffset            // Have we processed all words?
    blt copyLoop4
    bra done

/* Destination depth is two */
test2bit:
    cmp.b         #2, dstDepth             // Is the output depth 2?
    bne test1bit

    // Outer loop, process all long words
copyLoop2:
    clr.l             dstOutput
    // Inner loop, process 16 pixels
    moveq.w      #15, loop // Max of 16 pixels per long word
wordLoop2:
    bfextu (src){srcOffset:srcDepth}, tmp // Read a pixel
    add.l   srcDepth, srcOffset
    lsl.l         #2, dstOutput           // Lookup pixel in table
    or.b struct(ColorInfo.colorMap)(cInfo,tmp.w),dstOutput
    dbra        loop, wordLoop2           // Last pixel of longword?

    // End of inner loop
    move.l dstOutput, (dst)+               // Write out the longword
    cmp.l  endOffset, srcOffset            // Have we processed all words?
    blt copyLoop2
    bra done

/* Destination depth is one */
test1bit:
    cmp.b #1,dstDepth              // Is the output depth 1?
    bne test8bit

    // Read the first palette color into color1
    move.b struct(ColorInfo.colorPal)(cInfo),color1

    // Outer loop, process all long words
copyLoop1:
    // Inner loop, process 32 pixels
    clr.l             dstOutput
    moveq.w      #31, loop // Max of 32 pixels per long word
wordLoop1:
    bfextu  (src){srcOffset:srcDepth}, tmp // Read a pixel
    add.l   srcDepth, srcOffset
    cmp.b     color1, tmp                  // Test pixel color
    beq               notColorMatch
    bset        loop, dstOutput
notColorMatch:
    dbra loop,wordLoop1                    // Last pixel of longword?
    // End of inner loop
    move.l dstOutput, (dst)+               // Write out the longword
    cmp.l  endOffset, srcOffset            // Have we processed all words?
    blt copyLoop1

    moveq.l       #1, dstDepth             // Restore dstDepth as it was overwritten
    bra done

/* Destination depth is eight */
test8bit:

    clr.l tmp
copyLoop8:
    move.b (src)+,tmp
    move.b struct(ColorInfo.colorMap)(cInfo,tmp.w),(dst)+
    cmp.l src,end
    bne copyLoop8

done:
    // Copy size of resampled data to d0
    move.l   endOffset, d0
    mulu.w   dstDepth,  d0
    divu.w   srcDepth,  d0
    lsr.l          #3,  d0

exit:
    movem.l (a7)+,d3-d5/a2-a4         // Restore registers
    unlk    a6
    rts

    #undef srcArg
    #undef dstArg
    #undef endArg
    #undef srcDeArg
    #undef dstDeArg
    #undef cInfoArg

    #undef src
    #undef dst
    #undef end
    #undef cInfo
    #undef endOffset

    #undef tmp
    #undef srcOffset
    #undef dstOutput
    #undef loop
    #undef srcDepth
    #undef dstDepth
    #undef color1
}
#endif // USE_ASM_CODE