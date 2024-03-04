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

#if !USE_ASM_CODE

#define ROL(A,B) ((A << B) | (A >> (sizeof(A)*8 - B)))
#define ROR(A,B) ((A >> B) | (A << (sizeof(A)*8 - B)))
#define src16 ((unsigned short*)src)
#define dst16 ((unsigned short*)dst)
#define src32 ((unsigned long*)src)

/**
 * With "src" pointing to the first byte of a tile on the screen, this function will copy the data
 * "dst" and returns the number of bytes written. Although tile rows are written end-to-end, no
 * change is done to the color values, i.e. they are stored exactly as in the "native" framebuffer.
 * The tile size in pixels is "rows" and "cols", but this function requires that each row be multiple
 * of 2 bytes -- i.e. a tile must be at least 16 pixels across in 1-bit mode, but may be as few as
 * two pixels across in 8-bit mode.
 *
 * If colorInfo is provided, this function will also tally the colors. It operates four bytes at a time,
 * so when counting colors, this function may append up to two bytes of padding to the dst data buffer,
 * however these padding bytes will not included in the return value indicating the amount of data written.
 *
 * a.k.a "Reference::writeScreenTileAsNative"
 */
unsigned short screenToNative(const unsigned char *src, unsigned char *dst, short rows, short cols, ColorInfo *colorInfo) {
    unsigned char *start = dst;

    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    #ifdef VNC_BYTES_PER_LINE
        const unsigned long fbStride = VNC_BYTES_PER_LINE;
    #endif

    unsigned long tally[8] = {0};

    /********************************************************************
     * Stage 1: Copy pixel data from screen tile into consecutive bytes *
     ********************************************************************/
    {
        const unsigned char wordsInRow = cols * fbDepth / sizeof(unsigned short) / 8;
        char rowsLeft = rows - 1;
        char wordsLeftInRow = wordsInRow - 1;
        const unsigned short stride = fbStride - wordsInRow * sizeof(unsigned short);
        do {
            *dst16++ = *src16++;
            if (--wordsLeftInRow == -1) {
                src += stride;
                wordsLeftInRow = wordsInRow - 1;
                if (--rowsLeft == -1)
                    break;
            }
        } while(true);
    }

    /********************************************************************
     * Stage 2: Tally colors of all pixels                              *
     ********************************************************************/
    if(colorInfo) {
        src = start;
        if((dst - src) % sizeof(unsigned long)) {
            // Add padding
            *dst16 = *(dst16-1);
            dst16++;
        }
        unsigned long colors, lastColors = ~*src32;
        const unsigned char oneLessNumPix = sizeof(colors) * 8 / fbDepth - 1;
        const unsigned long rmask = (1 << fbDepth) - 1; // Mask for rightmost color in block
        do {
            unsigned long colors = *src32++;
            if(colors != lastColors) {
                lastColors = colors;
                // XOR the word with itself to see whether all colors are equal
                char n = (colors == ROL(colors,fbDepth)) ? 0 : oneLessNumPix;
                do {
                    const unsigned char color = colors & rmask;
                    // Tally the color
                    const unsigned char bucket = color >> 5;
                    const unsigned long bit = 1L << (color & 0x1F);
                    tally[bucket] |= bit;
                    colors >>= fbDepth;
                } while (--n != -1);
            }
        } while (src != dst);

    /********************************************************************
     * Stage 3: Write out color table and color mapping table           *
     ********************************************************************/
        unsigned char nColors = 0;
        unsigned char color = 0;
        for(unsigned char i = 0; i < 8; i++) {
            unsigned long colors = tally[i];
            if(colors) {
                for(unsigned char bit = 0; bit < 32; bit++) {
                    if(colors & 1) {
                        colorInfo->colorMap[color] = nColors;
                        colorInfo->colorPal[nColors] = color;
                        nColors = (nColors + 1) & 0x7F;
                    }
                    colors >>= 1;
                    color++;
                }
            } else {
                color += 32;
            }
        }
        colorInfo->nColors = nColors;
    }
    return rows * cols * fbDepth / 8;
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
 *
 * a.k.a. Reference::tallyColors
 */
unsigned short nativeToColors(const unsigned char *src, unsigned char *end, ColorInfo *colorInfo) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    #ifdef VNC_BYTES_PER_LINE
        const unsigned long fbStride = VNC_BYTES_PER_LINE;
    #endif

    unsigned long tally[8] = {0};

    /********************************************************************
     * Stage 1: Tally the colors using a bit-field                      *
     ********************************************************************/

    const unsigned char oneLessNumPix = 32 / fbDepth - 1;
    const unsigned short rmask = (1 << fbDepth) - 1; // Mask for rightmost color in block
    #define end16 ((unsigned short*)end)
    if((end - src) % sizeof(unsigned long)) {
        // Add padding
        *end16 = *(end16-1);
        end16++;
    }
    do {
        unsigned long colors = *src32++;
        char n = oneLessNumPix;
        do {
            // Rotate the left-most color from the most
            // significant bits to the least significant
            colors = ROL(colors, fbDepth);
            const unsigned short color = colors & rmask;
            // Tally the color
            const unsigned char bucket = color & 7;
            const unsigned long bit = 1L << (color >> 3);
            tally[bucket] |= bit;
        } while (--n != -1);
    } while (src != end);

    /********************************************************************
     * Stage 2: Write out color table and color mapping table           *
     ********************************************************************/

    unsigned char nColors = 0;
    for(unsigned short color = 0; color < 256; color++) {
        const unsigned long bit = 1L << (color >> 3);
        if( tally[color & 7] & bit ) {
            colorInfo->colorMap[color] = nColors & 0x7F;
            colorInfo->colorPal[nColors & 0x7F] = color;
            nColors++;
        }
    }

    colorInfo->nColors = nColors;

    return nColors;
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
 * a.k.a. Reference::RLE_FromNativeNew_1
 */
unsigned short nativeToRle(const unsigned char *src, unsigned char *end, unsigned char *dst, const unsigned char *stop, unsigned char depth, ColorInfo *cInfo) {
    #define ROL(A,B) ((A << B) | (A >> (sizeof(A)*8 - B)))
    #define ROR(A,B) ((A >> B) | (A << (sizeof(A)*8 - B)))
    #define src32 ((unsigned long*)src)

    const unsigned char *start = dst;
    const unsigned char npix = 32 / depth;
    const unsigned char rsft = 32 - depth;
    const unsigned long lmask = ((unsigned long)-1) << rsft; // Mask for leftmost color in block
    unsigned long curr = *src32, test, tmp;
    unsigned long carry = curr & lmask;
    unsigned short rleCnt = -1, rleVal = curr >> rsft, rleSiz = cInfo->colorSize;
    unsigned short runsOfOne = 0;

    const short nativeColors = (1 << fbDepth);
    const Boolean mapColors  = (cInfo->nColors < nativeColors);

    const Boolean gotPadding = (end - src) % sizeof(unsigned long);
    if(gotPadding == 2) {
        *end++ = 0;
        *end++ = 0;
    } else if(gotPadding) {
        printf("Unexpected padding value in RLE from native! %d\n", gotPadding);
        return 0;
    }

    if(cInfo->packRuns && (cInfo->colorSize != 1)) {
        dprintf("Invalid color size!!\n");
    }

    if(dst > stop) {
        dprintf("Nothing to do!\n");
    }

    Boolean done = false;
    for(;;) {
        curr = *src32++;
        if(src > end) {
            goto writeLastRle;
        }
        // Generate the test pattern
        test = curr;
        test >>= depth;
        test |= carry;
        // Now check whether all pixels are equal
        test ^= curr;
        if(test == 0) {
            // All pixels in block are equal
            rleCnt += npix;
            continue;
        }
        // Compute the carry to the next block
        carry = curr & rmask;
        carry = ROR(carry, depth);
        // Not pixels all are equal, test each individually
        char n = npix - 1;
        do {
            test = ROL(test, depth);
            tmp = test & rmask;
            if(tmp != 0) {
                // Pixel not a match, emit the RLE count
                goto writeRle;
            rleWriteReturn:
                rleVal ^= tmp;
                rleCnt = 0;
                test &= ~rmask;
            } else {
                // Pixel match, just increment the RLE count
                rleCnt++;
            }
        } while(test && (--n != -1));
        rleCnt += n;
    }

writeLastRle:
    done = true; // Could also make stop = dst
    // Adjustment for when we end up with 16 zero pixels as padding.
    if(gotPadding) {
        if(rleCnt < 15) {
            goto skipLastRLEPair;
        }
        rleCnt -= 16;
        if(rleCnt == 0) {
            runsOfOne--;
        }
    }
writeRle:
    /*** Write RLE Pair ***/
    const unsigned char color = mapColors ? cInfo->colorMap[rleVal] : rleVal;
    if((rleCnt == 0) && cInfo->packRuns) {
        *dst++ = color;
        runsOfOne++;
    } else {
        *dst = (cInfo->packRuns ? 0x80 : 0) | color;
        dst += rleSiz;
        while(rleCnt >= 255) {
            rleCnt -= 255;
            *dst++ = 255;
        }
        *dst++ = rleCnt;
    }
    /***********************/
    if(!done && (dst <= stop))
        goto rleWriteReturn;

skipLastRLEPair:
    cInfo->runsOfOne  = runsOfOne;
    return dst - start;
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
 * a.k.a. Reference::packTile_1
 */
unsigned short nativeToPacked(const unsigned char *src, unsigned char *dst, const unsigned char* end, const char inDepth, const char outDepth, ColorInfo *colorInfo) {
    const unsigned char *start = src;

    #define src32 ((unsigned long*)src)
    #define dst32 ((unsigned long*)dst)

    unsigned long inBits, outBits = 0;
    unsigned char inLeft = 0, outLeft = sizeof(unsigned long) * 8;

    const unsigned long inMask = (1 << inDepth) - 1; // Mask for rightmost color in block

    for (;;) {
        if (inLeft == 0) {
            if (src >= end)
                break;
            inBits = *src32++;
            inLeft = sizeof(unsigned long) * 8;
            if (outLeft == 0) {
                *dst32++ = outBits;
                outBits = 0;
                outLeft = sizeof(unsigned long) * 8;
            }
        }
        inLeft  -= inDepth;
        outLeft -= outDepth;
        const unsigned char color = (inBits >> inLeft) & inMask;
        const unsigned char mapped = colorInfo->colorMap[color];
        outBits |= (unsigned long)mapped << outLeft;
    }
    // Write the remaining bits
    *dst32++ = outBits;

    return (end - start) * outDepth / inDepth;
}
#endif // !USE_ASM_CODE
