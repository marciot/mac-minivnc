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

/**
 * Specify a compression level for the color TTRL encoder,
 * from 0 to 4
 */

#define COMPRESSION_LEVEL 4

/**
 * To build for a specific resolution, uncomment one of
 * the following. Otherwise, a generic binary will be built
 */

//#define VNC_FB_RES_512_342
//#define VNC_FB_RES_512_384
//#define VNC_FB_RES_640_480
//#define VNC_FB_RES_1024_768

/**
 * To build for a specific color depth, uncomment one of
 * the following. Otherwise, a generic binary will be built
 */

//#define VNC_FB_MONOCHROME
//#define VNC_FB_4_COLORS
//#define VNC_FB_16_COLORS
//#define VNC_FB_256_COLORS

// Derived values, don't change anything below this line

#if defined(VNC_FB_RES_512_342)
  // Plus, SE, Classic, SE/30
  #define VNC_FB_WIDTH        512
  #define VNC_FB_HEIGHT       342
  #define VNC_FB_MONOCHROME
#elif defined(VNC_FB_RES_512_384)
  // Classic II, all others
  #define VNC_FB_WIDTH        512
  #define VNC_FB_HEIGHT       384

#elif defined(VNC_FB_RES_640_480)
  // Mac II, LC, etc
  #define VNC_FB_WIDTH        640
  #define VNC_FB_HEIGHT       480

#elif defined(VNC_FB_RES_1024_768)
  #define VNC_FB_WIDTH        1024
  #define VNC_FB_HEIGHT       768
#endif

#if defined(VNC_FB_MONOCHROME)
  #define VNC_FB_BITS_PER_PIX 1

#elif defined(VNC_FB_4_COLORS)
  #define VNC_FB_BITS_PER_PIX 2

#elif defined(VNC_FB_16_COLORS)
  #define VNC_FB_BITS_PER_PIX 4

#elif defined(VNC_FB_256_COLORS)
  #define VNC_FB_BITS_PER_PIX 8
#endif

#define VNC_FB_PIX_PER_BYTE (8 / VNC_FB_BITS_PER_PIX)
#define VNC_FB_PALETTE_SIZE (1 << VNC_FB_BITS_PER_PIX)
#define BYTES_PER_TILE_ROW  (2 * VNC_FB_BITS_PER_PIX)

#ifdef VNC_FB_WIDTH
    #define VNC_BYTES_PER_LINE (VNC_FB_WIDTH / VNC_FB_PIX_PER_BYTE)
#endif
