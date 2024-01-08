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

#include "VNCEncoder.h"

#include "VNCTypes.h"
#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCEncodeRaw.h"
#include "VNCEncodeHextile.h"
#include "VNCEncodeTRLE.h"
#include "VNCEncodeZRLE.h"
#include "VNCEncodeCursor.h"
#include "msgbuf.h"

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_TIME
//#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC

#include "miniz.h"

// tdefl_compressor contains all the state needed by the low-level compressor so it's a pretty big struct (~300k).
// This example makes it a global vs. putting it on the stack, of course in real-world usage you'll probably malloc() or new it.
static tdefl_compressor *g_deflator = 0;

// COMP_OUT_BUF_SIZE is the size of the output buffer used during compression.
// COMP_OUT_BUF_SIZE must be >= 1
#define COMP_OUT_BUF_SIZE (50l*1024)
static unsigned char *s_outbuf = 0;

extern VNCRect fbUpdateRect;

unsigned char selectedEncoder = -1, lastSelectedEncoder = -1;

OSErr VNCEncoder::setup() {
    #if defined(VNC_FB_MONOCHROME)
        const Size bSize = VNCEncodeTRLE::minBufferSize();
    #else
        const Size bSize = max(
            VNCEncodeRaw::minBufferSize(),
            max(
                VNCEncodeHextile::minBufferSize(),
                max(
                    VNCEncodeTRLE::minBufferSize(),
                    max(
                        VNCEncodeZRLE::minBufferSize(),
                        VNCEncodeCursor::minBufferSize()
                    )
                )
            )
        );
    #endif

    fbUpdateBuffer = (unsigned char*) NewPtr(bSize);
    if (MemError() != noErr)
        return MemError();
    return noErr;
}

OSErr VNCEncoder::destroy() {
    DisposPtr((Ptr)fbUpdateBuffer);
    fbUpdateBuffer = NULL;
    return MemError();
}

void VNCEncoder::clear() {
    vncFlags.clientTakesRaw     = false;
    vncFlags.clientTakesHextile = false;
    vncFlags.clientTakesTRLE    = false;
    vncFlags.clientTakesZRLE    = false;
    vncFlags.clientTakesCursor  = false;
    selectedEncoder = -1;
    compressReset();
}

int VNCEncoder::begin() {
    // Select the most appropriate encoder

    #if defined(VNC_FB_MONOCHROME)
        selectedEncoder = mTRLEEncoding;
    #else
        if (vncConfig.allowZRLE && vncFlags.clientTakesZRLE) {
            selectedEncoder = mZRLEEncoding;
        } else if (vncConfig.allowTRLE && vncFlags.clientTakesTRLE && !fbPixFormat.trueColor) {
            selectedEncoder = mTRLEEncoding;
        }
        else if (vncConfig.allowHextile && vncFlags.clientTakesHextile) {
            selectedEncoder = mHextileEncoding;
        }
        else if (vncConfig.allowRaw && vncFlags.clientTakesRaw && !fbPixFormat.trueColor) {
            dprintf("No suitable encoding found!\n");
            lastSelectedEncoder = -1;
            return false;
        }
    #endif

    if(lastSelectedEncoder != selectedEncoder) {
        lastSelectedEncoder = selectedEncoder;
        dprintf("Will use %s for updates\n", getEncoderName(selectedEncoder));
    }

    int status;
    #if defined(VNC_FB_MONOCHROME)
        status = VNCEncodeTRLE::begin();
    #else
        switch(selectedEncoder) {
            case mTRLEEncoding:    status = VNCEncodeTRLE::begin(); break;
            case mRawEncoding:     status = VNCEncodeRaw::begin(); break;
            case mHextileEncoding: status = VNCEncodeHextile::begin(); break;
            //case mZLibEncoding:    status = VNCEncodeZLib::begin(); break;
            case mZRLEEncoding:    status = VNCEncodeZRLE::begin(); break;
            default:               status = VNCEncodeRaw::begin(); break;
        }
    #endif

    if(status == EncoderNeedsCompression) {
        if((s_outbuf == NULL) || (g_deflator == NULL)) {
            status = EncoderDefer;
        } else {
            status = EncoderOk;
        }
    }
    return status;
}

Boolean VNCEncoder::getChunk(int x, int y, int w, int h, wdsEntry *wds) {
    #if defined(VNC_FB_MONOCHROME)
        return VNCEncodeTRLE::getChunk(x, y, w, h, wds);
    #else
        switch(selectedEncoder) {
            case mTRLEEncoding:    return VNCEncodeTRLE::getChunk(x, y, w, h, wds);
            case mRawEncoding:     return VNCEncodeRaw::getChunk(x, y, w, h, wds);
            case mHextileEncoding: return VNCEncodeHextile::getChunk(x, y, w, h, wds);
            //case mZLibEncoding:
            case mZRLEEncoding:    return getCompressedChunk(wds);
            default:               return VNCEncodeRaw::getChunk(x, y, w, h, wds);
        }
    #endif
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

void VNCEncoder::compressReset() {
    if(g_deflator) {
        const int level = vncConfig.zLibLevel;

        // The number of dictionary probes to use at each compression level (0-10). 0=implies fastest/minimal possible probing.
        const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32,  16, 32, 128, 256,  512, 768, 1500 };

        // create tdefl() compatible flags (we have to compose the low-level flags ourselves, or use tdefl_create_comp_flags_from_zip_params() but that means MINIZ_NO_ZLIB_APIS can't be defined).
        mz_uint comp_flags = TDEFL_WRITE_ZLIB_HEADER |
                            s_tdefl_num_probes[MZ_MIN(10, level)] |
                            ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0);
        if (level == 0) {
            comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;
        }

        // Initialize the low-level compressor.
        dprintf("Allocated %ld bytes for ZLib at compression level %d [ResEdit]\n", sizeof(tdefl_compressor), level);
        tdefl_status status = tdefl_init(g_deflator, NULL, NULL, comp_flags);
        if (status != TDEFL_STATUS_OKAY) {
            dprintf("tdefl_init() failed!\n");
            return;
        }
    }
}

void VNCEncoder::fbSyncTasks() {
    if(selectedEncoder == mZRLEEncoding) {
        if(s_outbuf == NULL) {
            dprintf("Allocated %ld bytes for ZLib output buffer\n", COMP_OUT_BUF_SIZE);
            s_outbuf = (unsigned char*)NewPtr(COMP_OUT_BUF_SIZE);
            if (MemError() != noErr) {
                dprintf("Failed to allocate output buffer\n");
                return;
            }
        }

        if(g_deflator == NULL) {
            g_deflator = (tdefl_compressor*)NewPtr(sizeof(tdefl_compressor));
            if (MemError() != noErr) {
                dprintf("Failed to allocate compressor\n");
                return;
            }
            compressReset();
        }
    }
}

Boolean VNCEncoder::getCompressedChunk(wdsEntry *wds) {
    unsigned char *next_in;
    unsigned char *next_out = s_outbuf;

    next_out += sizeof(unsigned long); // Leave space for the zLib length

    size_t avail_out = COMP_OUT_BUF_SIZE;
    size_t avail_in = 0;
    size_t total_in = 0;
    size_t total_out = 0;
    Boolean gotMore = true;

    // Compression.
    for (;;) {
        if (!avail_in && gotMore) {
            switch(selectedEncoder) {
                case mZLibEncoding:
                    //gotMore = VNCEncodeRaw::getChunk(fbUpdateRect.x, fbUpdateRect.y, fbUpdateRect.w, fbUpdateRect.h, next_in, avail_in);
                    break;
                case mZRLEEncoding:
                    gotMore = VNCEncodeZRLE::getChunk(fbUpdateRect.x, fbUpdateRect.y, fbUpdateRect.w, fbUpdateRect.h, next_in, avail_in);
                    break;
            }
        }

        // Compress as much of the input as possible (or all of it) to the output buffer.

        size_t in_bytes  = avail_in;
        size_t out_bytes = avail_out;
        tdefl_status status = tdefl_compress(g_deflator, next_in, &in_bytes, next_out, &out_bytes, gotMore ? TDEFL_NO_FLUSH : TDEFL_SYNC_FLUSH);

        next_in  += in_bytes;
        avail_in -= in_bytes;
        total_in += in_bytes;

        next_out  += out_bytes;
        avail_out -= out_bytes;
        total_out += out_bytes;

        if (avail_out == 0) {
            dprintf("Output buffer full!\n");
            break;
        }

        if ((status == TDEFL_STATUS_OKAY) && !gotMore) {
            // Compression completed successfully.
            #if LOG_COMPRESSION_STATS
                dprintf("Deflated %ld bytes to %ld. ", total_in, total_out, 100 - (total_out * 100) / total_in);
            #endif
            break;
        }
        else if (status != TDEFL_STATUS_OKAY) {
            // Compression somehow failed.
            dprintf("tdefl_compress() failed with status %d!\n", status);
            break;
        }
    }

    // Write the length byte
    unsigned long *header = (unsigned long*)s_outbuf;
    *header = total_out;

    // Send the data
    if(total_out > 0xFFFF) {
        dprintf("too much data to send!\n");
    } else {
        wds->length = total_out + sizeof(unsigned long);
        wds->ptr = (Ptr) s_outbuf;
    }
    return false;
}