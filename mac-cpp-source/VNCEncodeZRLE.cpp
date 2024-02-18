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
#include "VNCEncodeTRLE.h"
#include "VNCEncodeZRLE.h"
#include "QDOffscreen.h"
#include "msgbuf.h"

#define USE_TILE_OVERLAY                 0 // For debugging

#if !defined(VNC_FB_MONOCHROME)
    #define TILE_MAX_SIZE      4096
    #define UPDATE_BUFFER_SIZE 16385

    extern int tile_x, tile_y;

    Size VNCEncodeZRLE::minBufferSize() {
        return UPDATE_BUFFER_SIZE;
    }

    int VNCEncodeZRLE::begin() {
        tile_x = 0;
        tile_y = 0;
        VNCEncodeTRLE::begin();
        return EncoderNeedsCompression;
    }

    #if USE_TILE_OVERLAY
        unsigned long annotatedEncodeTileTRLE(const unsigned char *src, int x, int y, int rows, int cols, unsigned char *dst, unsigned long bytesAvail, Boolean allowPaletteReuse);
        unsigned long annotatedEncodeTileTRLE(const unsigned char *src, int x, int y, int rows, int cols, unsigned char *dst, unsigned long bytesAvail, Boolean allowPaletteReuse {
            static GWorldPtr myGWorld = 0;
            Rect srcRect, dstRect;
            if (myGWorld == 0) {
                SetRect(&srcRect, 0, 0, 64, 64);
                OSErr err = NewGWorld(&myGWorld, 0, &srcRect, 0, 0, keepLocal);
                if (err != noErr) {
                    printf("failed to allocate gworld\n");
                    myGWorld = 0;
                }
            }
            if(myGWorld) {
                char label[5];
                CGrafPtr savedPort;
                GDHandle savedGDevice;
                SetRect(&srcRect, x, y, x + cols, y + rows);
                SetRect(&dstRect, 0, 0, cols, rows);
                CopyBits(&qd.screenBits, &((GrafPtr)myGWorld)->portBits, &srcRect, &dstRect, srcCopy, nil);
                GetGWorld (&savedPort, &savedGDevice);
                SetGWorld(myGWorld, nil);
                MoveTo(cols/2,rows/2);
                sprintf(label, "%x", dst[0]);
                DrawText(label,0,strlen(label));
                SetGWorld(savedPort, savedGDevice);

                PixMapHandle hndl = GetGWorldPixMap(myGWorld);
                Ptr pixBaseAddr = GetPixBaseAddr(hndl);
                unsigned long savedStride = fbStride;
                fbStride = (*hndl)->rowBytes & 0x7FFF;
                unsigned long len = VNCEncodeTRLE::encodeTile((unsigned char*)pixBaseAddr, rows, cols, dst, bytesAvail, allowPaletteReuse);
                fbStride = savedStride;
                return len;
            } else {
                return VNCEncodeTRLE::encodeTile(src, rows, cols, dst, bytesAvail, allowPaletteReuse);
            }
        }
    #endif

    Boolean VNCEncodeZRLE::getChunk(int x, int y, int w, int h, unsigned char *&ptr, size_t &length) {
        const int tileSize = 64;
        unsigned char *dst = fbUpdateBuffer;
        unsigned long bytesAvail = UPDATE_BUFFER_SIZE;

        while (bytesAvail > TILE_MAX_SIZE) {
            const unsigned char *src = VNCFrameBuffer::getPixelAddr(x + tile_x, y + tile_y);
            const int cols = min(tileSize, w - tile_x);
            const int rows = min(tileSize, h - tile_y);
            #if USE_TILE_OVERLAY
                encodeTile(src, rows, cols, dst, bytesAvail, false);
                const unsigned long len = annotatedEncodeTileTRLE(src, x + tile_x, y + tile_y, rows, cols, dst, bytesAvail, false);
                bytesAvail -= len;
                dst += len;
            #else
                const unsigned long len = VNCEncodeTRLE::encodeTile(src, rows, cols, dst, bytesAvail, false);
                bytesAvail -= len;
                dst += len;
            #endif

            // Advance to next tile in row
            tile_x += tileSize;
            if (tile_x < w)
                continue;

            // Prepare for the next row
            tile_y += tileSize;
            if (tile_y >= h)
                break;
            tile_x = 0;
        }

        length = dst - fbUpdateBuffer;
        ptr    = fbUpdateBuffer;

        return tile_y < h;
    }
#endif