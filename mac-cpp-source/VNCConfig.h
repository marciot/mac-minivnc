/************************************************************

    VNCConfig.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 2/9/22

       LAST REVISION: 2/9/22

       (c) 2022 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

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
