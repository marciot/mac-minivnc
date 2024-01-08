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

#include <stdio.h>
#include <string.h>

#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCScreenToRLE.h"

#if !defined(VNC_FB_MONOCHROME)
#if __MC68K__
    /**
     * With "src" pointing to the first byte of a tile on the screen, this function will write RLE
     * coded data to "dst" and return the number of bytes written. Only the first "rows" of
     * the tile will be considered, cols must be a multiple of 16
     *
     * This function will use the correct depth and linestride for the device.
     */
    asm unsigned short writeScreenTileAsRLE(const unsigned char *src, unsigned char *dst, const unsigned char* end, char rows, char cols) {
        machine 68020
        /*
         * Register Assignments:
         *   A0                      : Source ptr
         *   A1                      : Destination ptr
         *   A2                      : Maximum value to write
         *   A3                      : Linestride of screen
         *   A4                      : Carry forward of data
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
        #define dstArg    12(a6)
        #define endArg    16(a6)
        #define rowArg    20(a6)
        #define colArg    22(a6)

        #define src       a0
        #define dst       a1
        #define end       a2
        #define stride    a3
        #define carry     a4

        #define tmp       d0
        #define depth     d1
        #define cLnP_pair d2
        #define nPix      d2  //  colsLeft : nPix
        #define colsLeft  d2
        #define lpxPair   d3  //  loop : rowsLeft
        #define loop      d3
        #define rowsLeft  d3
        #define curr      d4
        #define mask      d5
        #define test      d6
        #define rlePair   d7  //  rleVal : rleCnt
        #define rleVal    d7
        #define rleCnt    d7

        link    a6,#0000            // Link for debugger
        movem.l d3-d7/a2-a4,-(a7)   // Save registers

        movea.l  srcArg,src         // Load the src ptr into A0
        movea.l  dstArg,dst         // Load the dst ptr into A1
        movea.l  endArg,end         // Load the end ptr into A2

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

    // Initialize next, loop counters and nPix

        cmpi.b #1,depth
        bne testDepth8

        move.w #32,nPix
        bra prepareLoopCounters

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
        move.w #16,nPix

    prepareLoopCounters:
        // Mask for rightmost color in block
        moveq.l     #1,mask  // mask = (1 << depth) - 1
        lsl.l    depth,mask
        subq.l      #1,mask

        // Load first block into current
        move.l (src), curr

        // Move leftmost pixel in current into rleVal

        move.l    curr,rleVal
        rol.l    depth,rleVal
        and.l     mask,rleVal
        swap   rlePair // Move the value into the high-word
        move.w     #-1,rleCnt

        // Load leftmost pixel in current into carry
        move.l    mask,tmp
        ror.l    depth,tmp
        and.l     curr,tmp
        movea.l    tmp,carry

        // rowsLeft = rows - 1
        clr.w          rowsLeft
        move.b  rowArg,rowsLeft
        subi.w      #1,rowsLeft

        // colsLeft = cols
        swap cLnP_pair           // Move colsLeft to low-word
        clr.w          colsLeft
        move.b  colArg,colsLeft
        swap cLnP_pair           // Move nPix to low-word

    /////////////////////////////////
    // Outer loop, process all blocks
    readNextBlock:
        move.w nPix,tmp

        swap cLnP_pair             // Move colsLeft to low-word
        cmp.w colsLeft,tmp        // colsLeft >= npix
        bgt depthOne

        // if(colsLeft >= npix)
        // Fetch a block of pixels from the screen
        move.l (src)+, curr

        sub.w tmp,colsLeft
        beq addStride

        // If we did not take a stride, adjust rowsLeft to
        // compensate for the DBRA subtracting one.
        addi.w #1,rowsLeft
        bra compareAllPixelsInBlock

    addStride:
        clr.w          colsLeft
        move.b  colArg,colsLeft

        // At the end of each tile row, jump by stride

        adda.w   stride, src
        move.w    depth, tmp
        mulu.w colsLeft, tmp
        lsr.l        #3, tmp
        suba.l      tmp, src

        bra compareAllPixelsInBlock

    depthOne:
        // If depth == 1, we generally have to load two rows into
        //  current, except as a special case when rows is odd
        move.w   (src),curr
        swap curr
        adda.w  stride,src
        tst.w rowsLeft
        beq onlyHalfBlock

        // Read in the second row
        move.w   (src),curr
        adda.w  stride,src
        subi.w #1,rowsLeft

        bra compareAllPixelsInBlock

    onlyHalfBlock:
        // When rows is odd, our last read is only half a block
        // and we pad with zeros
        clr.w curr

    compareAllPixelsInBlock:
        swap cLnP_pair       // Move nPix to low-word

        // Make a pattern for testing all the
        // pixels for equality
        // This also moves the first pixel to the
        // low-order bits for comparison to rleVal

        move.l  curr,test
        not          test
        or.l    mask,test
        not          test
        ror.l  depth,test
        move.l carry,tmp
        or.l     tmp,test

        // Now check whether all pixels in block are equal
        eor.l   curr,test
        bne allPixelsNotEqual
        // All pixels are equal in block
        add.w   nPix,rleCnt
        bra nextBlock

    allPixelsNotEqual:
        // Compute the carry into the next block
        move.l   curr,tmp
        and.l    mask,tmp
        ror.l   depth,tmp
        movea.l   tmp,carry

        // Prepare loop counter
        swap         lpxPair // move loop into low-word
        move.w  nPix,loop    // n = npix - 1;
        subi.w    #1,loop

        //*********** Inner loop ***********/
    innerLoop:
        // Check the left-most pixel for match
        rol.l depth,test
        move.l test,tmp
        and.l mask,tmp
        bne writeRLEPair

        // Pixel match, just increment the RLE count
        addi.w #1, rleCnt
        bra skipRLEPair

    writeRLEPair:
        // Write the RLE pair...
        // tmp will contain the XOR value required to update the RLE value
        swap rlePair                // Move the value to the low-word
        move.b rleVal,(dst)+        // ...write the RLE value
        eor.w tmp,rleVal            // ...update the RLE value
        swap rlePair                // Move the count to the low-word
    writeRLE255:
        cmpi.w #255,rleCnt          // While rleCnt >= 255
        blt writeRLEFinalCount
        move.b #255,(dst)+          // ...write a 255
        subi.w #255,rleCnt
        bra writeRLE255
    writeRLEFinalCount:
        move.b rleCnt,(dst)+        // ...write the count
        clr.w rleCnt                // ...reset the count (to 0, which means one)

    skipRLEPair:
        eor.l tmp,test              // Exit early if remaining pixels are a run...
        dbeq loop, innerLoop
        //*********** Inner loop ***********/
        add.w loop, rleCnt          // ...and update run count with remaining pixels
        swap lpxPair // Restore pixels to low-word

    nextBlock:
        cmpa.l dst,end               // If dst == end, then abort early
        dbeq rowsLeft, readNextBlock // Have we processed all blocks?
    /////////////////////////////////////

        // If depth is 1 and the rows are odd, we may have to
        // adjustment for the 16 zero pixels of padding

        cmpi.b #1,depth        // Special handling for depth == 1
        bne writeLastRLEPair

        btst      #0,rowArg    // ...is rows odd?
        beq writeLastRLEPair

        // If the last RLE block is has a count of 17 or greater,
        // subtract out 16 for the padding

        cmpi.w  #16,rleCnt
        blt adjustRLECount
        bra done // ...otherwise, discard last RLE pair

    adjustRLECount:
        subi.w  #16,rleCnt

    writeLastRLEPair:
        // Write the remaining RLE pair
        swap rlePair                // Move the count to the low-word
        move.b rleVal,(dst)+        // Write an RLE value
        swap rlePair                // Move the count to the low-word
    writeLastRLE255:
        cmpi.w #255,rleCnt          // While rleCnt >= 255
        blt writeLastRLEFinalCount
        move.b #255,(dst)+          // ...write a 255
        subi.w #255,rleCnt
        bra writeLastRLE255
    writeLastRLEFinalCount:
        move.b rleCnt,(dst)+        // ...write the count

    done:
        // Copy number of bytes written to dst to return value
        suba.l   dstArg, dst
        move.l   dst, d0

    exit:
        movem.l (a7)+, d3-d7/a2-a4  // Restore registers
        unlk    a6
        rts

        #undef srcArg
        #undef dstArg
        #undef endArg
        #undef rowArg
        #undef colArg

        #undef src
        #undef dst
        #undef end
        #undef stride
        #undef next

        #undef tmp
        #undef depth
        #undef nPix
        #undef lpxPair
        #undef loop
        #undef pixels
        #undef curr
        #undef mask
        #undef test
        #undef rlePair
        #undef rleVal
        #undef rleCnt
    }
#else
    unsigned short writeScreenTileAsRLE(const unsigned char *src, unsigned char *dst, const unsigned char* end, char rows, char cols) {
        #ifdef VNC_FB_BITS_PER_PIX
            const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
        #endif
        #ifdef VNC_BYTES_PER_LINE
            const unsigned long fbStride = VNC_BYTES_PER_LINE;
        #endif

        const unsigned char npix = 32 / fbDepth;
        const unsigned char rsft = 32 - fbDepth;
        const unsigned char *start = dst;
        const unsigned long *ptr = (unsigned long*) src;
        const unsigned short *sptr = (unsigned short*) src;
        const unsigned long rmask = (1 << fbDepth) - 1; // Mask for rightmost color in block
        const unsigned long lmask = ((unsigned long)-1) << rsft; // Mask for leftmost color in block
        unsigned long curr = *ptr, test, tmp;
        #define ROL(A) (A << fbDepth) | (A >> rsft);
        #define ROR(A) (A >> fbDepth) | (A << rsft);
        unsigned long carry = curr & lmask;
        unsigned short rleCnt = -1, rleVal = curr >> rsft;
        char colsLeft = cols;
        char rowsLeft = rows - 1;
        do {
            if(colsLeft >= npix) {
                colsLeft -= npix;
                curr = *ptr++;
                // At the end of each tile row, jump by stride
                if(colsLeft == 0) {
                    colsLeft = cols;
                    ptr += (fbStride - (cols * fbDepth / 8)) / sizeof(unsigned long);
                } else {
                    // If we did not take a stride, adjust rowsLeft to
                    // compensate for the DBRA subtracting one.
                    rowsLeft++;
                }
            }
            else {
                // Special case: We have to pack
                // two rows in a single block
                curr = ((unsigned long)*sptr) << 16;
                sptr += fbStride / sizeof(unsigned short);
                // When rows is odd, we need to buffer with zeros
                if(rowsLeft) {
                    curr |= ((unsigned long)*sptr);
                    sptr += fbStride / sizeof(unsigned short);
                    rowsLeft--;
                }
            }

            // Generate the test pattern
            test = curr;
            test = ~test; // Clear right-most pixel...
            test |= rmask;
            test = ~test;
            test = ROR(test); // Add the carry
            test |= carry;

            // Now check whether all pixels are equal
            test ^= curr;
            if(test == 0) {
                // All pixels in block are equal
                rleCnt += npix;
            } else {
                // Compute the carry to the next block
                carry = curr & rmask;
                carry = ROR(carry);
                // Not pixels all are equal, test each individually
                char n = npix - 1;
                do {
                    test = ROL(test);
                    tmp = test & rmask;
                    if(tmp == 0) {
                        // Pixel match, just increment the RLE count
                        rleCnt++;
                    } else {
                        test   ^= tmp;
                        *dst++ = rleVal;
                        rleVal ^= tmp;
                        while(rleCnt >= 255) {
                            rleCnt -= 255;
                            *dst++ = 255;
                        }
                        *dst++ = rleCnt;
                        rleCnt = 0;
                    }
                } while(test && (--n != -1));
                rleCnt += n;
            }
        } while ((--rowsLeft != -1) && (dst != end));
        // Adjustment for when we end up with 16 zero pixels as padding.
        if((fbDepth == 1) && ((rows & 1) == 1)) {
            if(rleCnt >= 16) {
                rleCnt -= 16;
            } else {
                return dst - start;
            }
        }

        // Write the remaining RLE pair
        *dst++ = rleVal;
        while(rleCnt >= 255) {
            rleCnt -= 255;
            *dst++ = 255;
        }
        *dst++ = rleCnt;
        return dst - start;
    }
#endif
#endif