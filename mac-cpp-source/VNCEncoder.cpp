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

#include "GestaltUtils.h"

#include "VNCTypes.h"
#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "VNCEncodeRaw.h"
#include "VNCEncodeHextile.h"
#include "VNCEncodeTRLE.h"
#include "VNCEncodeZRLE.h"
#include "VNCEncodeCursor.h"
#include "DebugLog.h"

#define USE_FAST_MONO_ENCODER            0 // Force use of monochrome encoder when depth = 1
#define USE_TILE_OVERLAY                 0 // For debugging

unsigned char selectedEncoder = -1, lastSelectedEncoder = -1;
unsigned char *fbUpdateBuffer = 0;
unsigned long  fbUpdateBufferSize;
int tile_x, tile_y;

OSErr VNCEncoder::setup() {
}

OSErr VNCEncoder::destroy() {
    return freeMemory();
}

OSErr VNCEncoder::freeMemory() {
    DisposePtr((Ptr)fbUpdateBuffer);
    fbUpdateBuffer = NULL;
    UnloadSeg(VNCEncodeTRLE::begin);

    if (vncFlags.zLibLoaded) {
        compressDestroy();
        UnloadSeg(compressDestroy);
    }

    return MemError();
}

void VNCEncoder::clear() {
    #if !defined(VNC_FB_MONOCHROME)
        if (selectedEncoder == mZRLEEncoding) {
            compressReset();
        }
    #endif
    vncFlags.clientTakesRaw     = false;
    vncFlags.clientTakesHextile = false;
    vncFlags.clientTakesTRLE    = false;
    vncFlags.clientTakesZRLE    = false;
    vncFlags.clientTakesCursor  = false;
    selectedEncoder = -1;
}

int VNCEncoder::begin() {
    // Select the most appropriate encoder

    #if defined(VNC_FB_MONOCHROME)
        if (vncFlags.clientTakesTRLE && (!fbPixFormat.trueColor)) {
            selectedEncoder = mTRLEEncoding;
        }
    #else
        if (vncConfig.allowTRLE && vncFlags.clientTakesTRLE) {
            selectedEncoder = mTRLEEncoding;
        } else if (vncConfig.allowHextile && vncFlags.clientTakesHextile) {
            selectedEncoder = mHextileEncoding;
        } else if (vncConfig.allowZRLE && vncFlags.clientTakesZRLE) {
            selectedEncoder = mZRLEEncoding;
        } else if (vncConfig.allowRaw && vncFlags.clientTakesRaw && (!fbPixFormat.trueColor) && (fbDepth == 8)) {
            selectedEncoder = mRawEncoding;
        }
    #endif
        else {
            dprintf("No suitable encoding found!\n");
            selectedEncoder = lastSelectedEncoder = -1;
            return false;
        }

    tile_x = 0;
    tile_y = 0;

    // Decide whether to defer to the main thread for initialization.
    // This will need to happen whenever memory needs to be allocated,
    // before the first call to any encoder routines which might cause
    // the "Color Support" segment to be loaded, or whenever the encoder
    // changes.

    if(lastSelectedEncoder != selectedEncoder) {
        lastSelectedEncoder = selectedEncoder;
        dprintf("Will use %s for updates\n", getEncoderName(selectedEncoder));
        return EncoderDefer;
    }

    if (fbUpdateBuffer == NULL) {
        return EncoderDefer;
    }

    return encoderSetup();
}

int VNCEncoder::encoderSetup() {
    switch(selectedEncoder) {
        case mTRLEEncoding: VNCEncodeTRLE::begin(); break;
        #if !defined(VNC_FB_MONOCHROME)
            case mRawEncoding:     VNCEncodeRaw::begin(); break;
            case mHextileEncoding: VNCEncodeHextile::begin(); break;
            //case mZLibEncoding:  VNCEncodeZLib::begin(); break;
            case mZRLEEncoding:
                VNCEncodeTRLE::begin();
                if (!vncFlags.zLibLoaded) {
                    return EncoderDefer;
                }
                break;
        #endif
    }
    return EncoderReady;
}

unsigned long VNCEncoder::getEncoding() {
    return selectedEncoder;
}

char *VNCEncoder::getEncoderName(unsigned long encoding) {
        switch(encoding) {
            case  0:   return "Raw";
            case  5:   return "Hextile";
            case  6:   return "Zlib";
            case  8:   return "ZHextile";
            case 15:   return "TRLE";
            case 16:   return "ZRLE";
            case -239: return "Cursor";
            case -240: return "XCursor";
            default:   return "";
        }
}

void VNCEncoder::clientEncoding(unsigned long encoding, Boolean hasMore) {
    switch(encoding) {
        case mRawEncoding:     vncFlags.clientTakesRaw     = true; break;
        case mHextileEncoding: vncFlags.clientTakesHextile = true; break;
        //case mZLibEncoding:    vncFlags.clientTakesZLib    = true; break;
        case mTRLEEncoding:    vncFlags.clientTakesTRLE    = true; break;
        case mZRLEEncoding:    vncFlags.clientTakesZRLE    = true; break;
        case mCursorEncoding:  vncFlags.clientTakesCursor  = true; break;
    };
}

OSErr VNCEncoder::fbSyncTasks() {
    // Make sure the update buffer is allocated and the right size
    Size size;
    switch(selectedEncoder) {
        case mTRLEEncoding:        size = VNCEncodeTRLE::minBufferSize(); break;
        #if !defined(VNC_FB_MONOCHROME)
            case mRawEncoding:     size = VNCEncodeRaw::minBufferSize(); break;
            case mHextileEncoding: size = VNCEncodeHextile::minBufferSize(); break;
            case mZRLEEncoding:    size = VNCEncodeZRLE::minBufferSize(); break;
        #endif
    }
    size = max(size, max(VNCPalette::minBufferSize(), VNCEncodeCursor::minBufferSize()));

    if ((fbUpdateBuffer != NULL) && (GetPtrSize((Ptr)fbUpdateBuffer) != size)) {
        dprintf("Buffer changed size, freeing existing buffers\n");
        // If the update buffer needs to change sizes,
        // reallocate everything to make room
        freeMemory();
    }

    if (fbUpdateBuffer == NULL) {
        fbUpdateBuffer = (unsigned char*) NewPtr(size);
        fbUpdateBufferSize = size;
        dprintf("Reserved %ld bytes for framebuffer updates\n", fbUpdateBufferSize);
        if (MemError() != noErr) {
            dprintf("Failed to fbUpdateBuffer\n");
            return MemError();
        }
    }

    // Initialize the encoders and associated modules

    #if !defined(VNC_FB_MONOCHROME)
        if((selectedEncoder == mZRLEEncoding) && !vncFlags.zLibLoaded) {
            compressSetup();
        }
    #endif

    encoderSetup();

    return noErr;
}

static unsigned long encodeTile(EncoderPB &epb);
static unsigned long encodeTile(EncoderPB &epb) {
    if (selectedEncoder == mHextileEncoding) {
        return VNCEncodeHextile::encodeTile(epb);
    } else {
        return VNCEncodeTRLE::encodeTile(epb);
    }
}

static unsigned long encodeSolidTile(EncoderPB &epb);
static unsigned long encodeSolidTile(EncoderPB &epb) {
    if (selectedEncoder == mHextileEncoding) {
        return VNCEncodeHextile::encodeSolidTile(epb);
    } else {
        return VNCEncodeTRLE::encodeSolidTile(epb);
    }
}

#if USE_TILE_OVERLAY
    static unsigned long annotatedEncodeTile(const EncoderPB &epb, unsigned int x, unsigned int y);
    static unsigned long annotatedEncodeTile(const EncoderPB &epb, unsigned int x, unsigned int y) {
        static GWorldPtr myGWorld = 0;
        Rect srcRect, dstRect;
        if (myGWorld == 0) {
            SetRect(&srcRect, 0, 0, 64, 64);
            OSErr err = NewGWorld(&myGWorld, 0, &srcRect, 0, 0, keepLocal);
            if (err != noErr) {
                dprintf("failed to allocate gworld\n");
                myGWorld = 0;
            }
        }
        if(myGWorld) {
            Str255 label;
            CGrafPtr savedPort;
            GDHandle savedGDevice;
            SetRect(&srcRect, x, y, x + epb.cols, y + epb.rows);
            SetRect(&dstRect, 0, 0, epb.cols, epb.rows);
            CopyBits(&qd.screenBits, &((GrafPtr)myGWorld)->portBits, &srcRect, &dstRect, srcCopy, nil);
            GetGWorld (&savedPort, &savedGDevice);
            SetGWorld(myGWorld, nil);
            MoveTo(epb.cols/2,epb.rows/2);

            NumToString(epb.dst[0], label);
            DrawString(label);
            SetGWorld(savedPort, savedGDevice);

            PixMapHandle hndl = GetGWorldPixMap(myGWorld);
            Ptr pixBaseAddr = GetPixBaseAddr(hndl);
            const unsigned long savedStride = fbStride;
            fbStride = (*hndl)->rowBytes & 0x7FFF;

            EncoderPB epb2 = epb;
            epb2.src = (unsigned char*)pixBaseAddr;
            const unsigned long len = encodeTile(epb2);
            fbStride = savedStride;
            return len;
        } else {
            return encodeTile(epb);
        }
    }
#endif

/* The Hextile and TRLE encoders are able to use a single rectangle for
 * an entire screen's worth of data because the data can be streamed in
 * chunks as small as a single tile. The ZRLE protocol, however, preceeds
 * each ZRLE rectangle with a length word for the ZLib data, necessitating
 * that all tiles be compressed prior to sending the rectangle header. For
 * a rectangle comprising the entire screen, this would require a large
 * buffer.  When using ZRLE, we divide each framebuffer update into multiple
 * subrectangles, each compressed individually, to reduce the buffer use.
 */

const unsigned int ZRLESubrectSize = 192;

unsigned int VNCEncoder::numOfSubrects() {
    // Multiple rectangles for ZRLE, one for Hextile and TRLE.
    return (selectedEncoder != mZRLEEncoding) ? 1 :
           (((fbUpdateRect.w + ZRLESubrectSize - 1) / ZRLESubrectSize) *
           ((fbUpdateRect.h + ZRLESubrectSize - 1) / ZRLESubrectSize));
}

Boolean VNCEncoder::isNewSubrect() {
    // This tells the server code to emit a new subrectangle header,
    // and the compression code to release the data. In the case,
    // of TRLE or Hextile, only emit a subrect at the very start.
    if (selectedEncoder == mZRLEEncoding) {
        return ((tile_x % ZRLESubrectSize) == 0) && ((tile_y % ZRLESubrectSize) == 0);
    } else {
        return (tile_x == 0) && (tile_y == 0);
    }
}

void VNCEncoder::getSubrect(VNCRect *rect) {
    // Called by the server when writing the subrectangle header.
    if (selectedEncoder == mZRLEEncoding) {
        const unsigned int tileMask = ZRLESubrectSize - 1;
        const unsigned int sub_x = tile_x / ZRLESubrectSize * ZRLESubrectSize;
        const unsigned int sub_y = tile_y / ZRLESubrectSize * ZRLESubrectSize;
        rect->x = fbUpdateRect.x + sub_x;
        rect->y = fbUpdateRect.y + sub_y;
        rect->w = min(sub_x + ZRLESubrectSize, fbUpdateRect.w) - sub_x;
        rect->h = min(sub_y + ZRLESubrectSize, fbUpdateRect.h) - sub_y;
    } else {
        *rect = fbUpdateRect;
    }
}

// This advances to the next tile in ZRLE. Because it completes each sub rectangle
// before proceeding to the next, the logic is a bit convoluted.

static Boolean advanceToNextTile(unsigned short tileSize, unsigned short subRectSize) {
    do {
        tile_x += tileSize;
        // Have we reached the rightmost edge of the subrect?
        if ((tile_x % subRectSize) == 0) {
            tile_y += tileSize;
            // Have we reached the bottom edge of the subrect?
            if ((tile_y % subRectSize) == 0) {
                if (tile_x >= fbUpdateRect.w) {
                    if (tile_y >= fbUpdateRect.h) return false;
                    // Step down and left to next row of subrect
                    tile_x = 0;
                } else {
                    // Move right and up to the next subrect on the right
                    tile_y -= subRectSize;
                }
            } else {
                // Step down and left to next row of tiles in the subrect
                tile_x -= subRectSize;
            }
        }
        // If both x and y are outside of the rectangle, we are done
        if ((tile_x >= fbUpdateRect.w) && (tile_y >= fbUpdateRect.h)) {
            return false;
        }
        // ...otherwise keep trying until we find a tile inside the rectangle.
    } while ((tile_x >= fbUpdateRect.w) || (tile_y >= fbUpdateRect.h));
    return true;
}

/* End of ZRLE subrectangle code */

// This advances to the next tile in TRLE or Hextile. Because there are no sub
// rectangles, the logic is quite straightforward.

static Boolean advanceToNextTile(const unsigned int tileSize) {
    tile_x += tileSize;
    if (tile_x >= fbUpdateRect.w) {
        // Prepare for the next row
        tile_y += tileSize;
        if (tile_y >= fbUpdateRect.h)
            return false;
        tile_x = 0;
        return true;
    }
    return true;
}

Boolean VNCEncoder::getUncompressedChunk(EncoderPB &epb) {
    const unsigned long minBytesAvail = 50;
    const unsigned int tileSize = (selectedEncoder == mZRLEEncoding) ? 64 : 16;
    const unsigned char *start = epb.dst;
    #if SANITY_CHECK
        unsigned char tileCount = 0;
    #endif

    do {
        const unsigned int x = fbUpdateRect.x + tile_x;
        const unsigned int y = fbUpdateRect.y + tile_y;
        epb.src = VNCFrameBuffer::getPixelAddr(x, y);
        epb.cols = min(tileSize, fbUpdateRect.w - tile_x);
        epb.rows = min(tileSize, fbUpdateRect.h - tile_y);
        #if USE_TILE_OVERLAY
            encodeTile(epb);
            const unsigned long len = annotatedEncodeTile(epb, x, y);
        #else
            const unsigned long len = encodeTile(epb);
        #endif
        if (len == 0) {
            // There is not enough room left in the buffer to encode the tile;
            // if there are other tiles in the buffer, flush them out and try
            // again...
            if (epb.dst == start) {
                // ...otherwise, emit a solid tile instead!
                dprintf("Insufficient buffer space to render tile\n");
                const unsigned long len = encodeSolidTile(epb);
                epb.bytesAvail -= len;
                epb.dst += len;
            }
            #if SANITY_CHECK
                dprintf("Encoded %d tiles in row, not enough space for last tile in buffer.\n", tileCount);
            #endif
            break;
        }
        epb.bytesAvail -= len;
        epb.dst += len;
        #if SANITY_CHECK
            tileCount++;
        #endif

        if (selectedEncoder == mZRLEEncoding) {
            advanceToNextTile(tileSize, ZRLESubrectSize);
            // For ZRLE, do one tile at a time, as the in-place compression
            // code will compress this tile to make room for the next tile.
            break;
        } else if (!advanceToNextTile(tileSize)) {
            break;
        }
        // Only advance to the next tile if we have a minimum amount of free
        // space left in the buffer.
    } while (epb.bytesAvail >= minBytesAvail);

    epb.bytesWritten = epb.dst - start;

    return (tile_y < fbUpdateRect.h) || (tile_x < fbUpdateRect.w);
}

Boolean VNCEncoder::getChunk(wdsEntry *wds) {
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    if ((!hasColorQD) || ((fbDepth == 1) && (selectedEncoder == mTRLEEncoding) && USE_FAST_MONO_ENCODER)) {
        return getChunkMonochrome(fbUpdateRect.x, fbUpdateRect.y, fbUpdateRect.w, fbUpdateRect.h, wds);
    }
    #if !defined(VNC_FB_MONOCHROME)
        EncoderPB epb;
        epb.dst = fbUpdateBuffer;
        epb.bytesAvail = fbUpdateBufferSize;

        Boolean gotMore;
        switch(selectedEncoder) {
            case mHextileEncoding:
            case mTRLEEncoding:
                gotMore = getUncompressedChunk(epb);
                break;
            case mRawEncoding:
                gotMore = VNCEncodeRaw::getChunk(epb);
                break;
            case mZRLEEncoding:
                #if USE_IN_PLACE_COMPRESSION
                    gotMore = getCompressedChunk(epb);
                #else
                    return getCompressedChunk(wds);
                #endif
                break;
        }
        wds->length = epb.bytesWritten;
        wds->ptr = (Ptr) fbUpdateBuffer;
        return gotMore;
    #endif
}