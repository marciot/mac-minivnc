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
#include "VNCFrameBuffer.h"
#include "VNCEncodeTight.h"


#define ROL(A,B) ((A << B) | (A >> (sizeof(A)*8 - B)))
#define ROR(A,B) ((A >> B) | (A << (sizeof(A)*8 - B)))
#define src16 ((unsigned short*)src)
#define dst16 ((unsigned short*)dst)
#define src32 ((unsigned long*)src)

extern int tile_x, tile_y;

Size VNCEncodeTight::minBufferSize() {
    return 0;
}

void VNCEncodeTight::begin() {
    tile_x = tile_y = 0;
}

#define src32 ((unsigned long*)src)

Boolean VNCEncodeTight::getChunk(wdsEntry *wds) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    #ifdef VNC_BYTES_PER_LINE
        const unsigned long fbStride = VNC_BYTES_PER_LINE;
    #endif

    const int stripeWidth = 32;
    const int longsPerRow = stripeWidth * fbDepth / 8 / sizeof(unsigned long);

    const int tile_top = tile_y;

    const unsigned char numPix = sizeof(unsigned long) * 8 / fbDepth;
    const unsigned long rmask = (1 << fbDepth) - 1; // Mask for rightmost color in block

    const unsigned char *src = VNCFrameBuffer::getPixelAddr(fbUpdateRect.x + tile_x, fbUpdateRect.y + tile_y);
    unsigned long colors = *src32;
    unsigned char lastColor = ROL(colors,fbDepth) & rmask;

    unsigned char cMap[256] = {0};
    unsigned char cPal[256] = {lastColor};
    unsigned char cIndx = 0;
    unsigned char extraColors = 0; // Count of colors - 1

    unsigned char consecutive_rows = 0;
    unsigned char row_1st_cidx = lastColor;
    unsigned char row_2nd_cidx = lastColor;

    const unsigned char* row_src = src;
    const unsigned char* row_end = src + longsPerRow * sizeof(unsigned long);

restartRow:
    unsigned short outBits = 1;
    unsigned char *dst = fbUpdateBuffer;
    Boolean firstRowPixel = true;

    src = row_src;

    for(;;) {
        colors = *src32++;

        unsigned char n = numPix - 1;
        do {
            // Extract left-most color value
            colors = ROL(colors, fbDepth);
            const unsigned char color = colors & rmask;

            // Process the color
            if (color != lastColor) {
                lastColor = color;

                // Perform color mapping
                cIndx = cMap[color];
                if ((cIndx == 0) && (color != cPal[0])) {
                    // Add new color to color table
                    cIndx = ++extraColors;
                    cMap[color] = cIndx;
                    cPal[cIndx] = color;

                    // If we have a color we have not seen before...
                    if (extraColors < 3) {
                        if (tile_y != tile_top) {
                            // ...emit previous rows as a solid or two-color tile?
                            goto emitTile;
                        } else {
                            // Re-emit the row
                            goto restartRow;
                        }
                    }
                }

                // Keep tally of consecutive solid rows
                if (consecutive_rows && (cIndx != row_1st_cidx) && (cIndx != row_2nd_cidx)) {
                    if (row_1st_cidx == row_2nd_cidx) {
                        row_1st_cidx = cIndx;
                    } else {
                        consecutive_rows = 0;
                    }
                }
            }

            if (firstRowPixel) {
                firstRowPixel = false;
                // Write out the row of pixels
                if (consecutive_rows++ == 0) {
                    row_1st_cidx = cIndx;
                    row_2nd_cidx = cIndx;
                }
            }

            /* If we have a solid tile, don't write out anything
               ...if we are a two-color tile, write out a bit per color
               ...otherwise write out a full byte per color.
             */
            if (extraColors != 0) {
                if (extraColors == 1) {
                    // Write out colors as bits
                    outBits = (outBits << 1) | cIndx;
                    if (outBits & 0x10) {
                        *dst++ = outBits;
                        outBits = 1;
                    }
                } else {
                    // Write out indices as bytes
                    *dst++ = cIndx;
                }
            }
        } while (--n != -1);

        #if SANITY_CHECK
            if (outBits != 1) {
                dprintf("Stripe width not a multiple of 8 pixels\n");
            }
        #endif

        if (src == row_end) {
            // Move to the next row
            row_src += fbStride;
            row_end += fbStride;
            src = row_src;
            if (++tile_y == fbUpdateRect.h) {
                break;
            }
            firstRowPixel = true;
        }
    } // for(;;)

emitTile:
    const size_t pixelDataLen = dst - fbUpdateBuffer;

    if (extraColors == 0) {
        *dst++ = 0x80;        // Fill compression
        *dst++ = 255;         // Red
        *dst++ = 255;         // Green
        *dst++ = 255;         // Blue
        return 4;
    } else {
        // Compute the compact data length represenation
        unsigned char lenData[4], lenSize = 0;
        for (size_t remainder = pixelDataLen; remainder; remainder >>= 7) {
            lenData[lenSize++] = remainder & 0x7F;
        }

        // Make space for the header
        const size_t headerSize = 3 + 3 * (extraColors + 1) + lenSize + pixelDataLen;
        BlockMove (fbUpdateBuffer, fbUpdateBuffer + headerSize, pixelDataLen);

        // Write out the header
        dst = fbUpdateBuffer;
        *dst++ = 0x40;        // Basic compression
        *dst++ = 1;           // PaletteFilter
        *dst++ = extraColors; // Number of colors minus 1
        for (int i = 0; i <= extraColors; i++) {
            emitColor (dst, i);
        }
        BlockMove (lenData, dst, lenSize);
    }

    // Prepare to advance to the next tile
    if (tile_y == fbUpdateRect.h) {
        tile_y = 0;
        tile_x += stripeWidth;
    }
    return tile_x >= fbUpdateRect.h;
}
