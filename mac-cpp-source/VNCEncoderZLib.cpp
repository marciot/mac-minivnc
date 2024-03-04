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
#include "VNCEncoder.h"
#include "DebugLog.h"

#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_TIME
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_MALLOC
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#define MINIZ_LITTLE_ENDIAN 0
#define MINIZ_HAS_64BIT_REGISTERS 0
#define MINIZ_HAS_64BIT_INTEGERS 0

#include "miniz.h"

// tdefl_compressor contains all the state needed by the low-level compressor so it's a pretty big struct (~300k).
// This example makes it a global vs. putting it on the stack, of course in real-world usage you'll probably malloc() or new it.
static tdefl_compressor *g_deflator = 0;

#if !USE_IN_PLACE_COMPRESSION
    // COMP_OUT_BUF_SIZE is the size of the output buffer used during compression.
    // COMP_OUT_BUF_SIZE must be >= 1
    #define COMP_OUT_BUF_SIZE (50l*1024)
    static unsigned char *s_outbuf = 0;
#endif

OSErr VNCEncoder::compressSetup() {
    // Make sure the compression buffer is allocated
    if (g_deflator == NULL) {
        g_deflator = (tdefl_compressor*)NewPtr(sizeof(tdefl_compressor));
        dprintf("Reserved %ld bytes for ZLib compressor object\n", sizeof(tdefl_compressor));
        if (MemError() != noErr) {
            dprintf("Failed to allocate compressor\n");
            return MemError();
        }
        compressReset();
        vncFlags.zLibLoaded = true;
    }

    // Make sure the compression objects are allocated
    #if !USE_IN_PLACE_COMPRESSION
        if (s_outbuf == NULL) {
            dprintf("Reserved %ld bytes for ZLib output buffer\n", COMP_OUT_BUF_SIZE);
            s_outbuf = (unsigned char*)NewPtr(COMP_OUT_BUF_SIZE);
            if (MemError() != noErr) {
                dprintf("Failed to allocate output buffer\n");
                return MemError();
            }
        }
    #endif

    // Make sure the "ANSI Libraries" segment is loaded
    strlen("");

    return noErr;
}

void VNCEncoder::compressDestroy() {
    DisposePtr((Ptr)g_deflator);
    #if !USE_IN_PLACE_COMPRESSION
        DisposePtr((Ptr)s_outbuf);
    #endif
    g_deflator = NULL;
    #if !USE_IN_PLACE_COMPRESSION
        s_outbuf = NULL;
    #endif
    vncFlags.zLibLoaded = false;
}

void VNCEncoder::compressReset() {
    if (g_deflator) {
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
        dprintf("Initializing ZLib at compression level %d [ResEdit]\n", level);
        tdefl_status status = tdefl_init(g_deflator, NULL, NULL, comp_flags);
        if (status != TDEFL_STATUS_OKAY) {
            dprintf("tdefl_init() failed!\n");
            return;
        }
    }
}


#if USE_IN_PLACE_COMPRESSION
    Boolean VNCEncoder::getCompressedChunk(EncoderPB &epb) {
        // Write the uncompressed data slightly ahead of the
        // compressed data, to leave room for zLib to work:

        const unsigned long zLibScratchSpace = 1024;

        // Compress the data
        unsigned char *max = epb.dst + epb.bytesAvail;
        unsigned char *next_out = epb.dst + sizeof(unsigned long); // Leave space for the zLib length
        unsigned char *next_in;

        size_t avail_out;
        size_t avail_in = 0;
        size_t total_in = 0;
        size_t total_out = 0;
        Boolean gotMoreAfterwards = true;
        Boolean gotMore = true;

        // Compression.
        for (;;) {
            // ZLib compressed all previous tile data, so get more to compress
            if (gotMore && (avail_in == 0)) {
                next_in = next_out + zLibScratchSpace;
                if (next_in >= max) {
                    dprintf("Temp buffer insufficient for uncompressed data. Aborting!\n");
                    break;
                }

                EncoderPB epb2 = epb;
                epb2.dst = next_in;
                epb2.bytesAvail = max - next_in;

                gotMoreAfterwards  = VNCEncoder::getUncompressedChunk(epb2);
                gotMore = gotMoreAfterwards && !VNCEncoder::isNewSubrect();

                avail_in  = epb2.bytesWritten;
                avail_out = next_in - next_out;
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

            if ((status == TDEFL_STATUS_OKAY) && (avail_out != 0) && !gotMore ) {
                // Compression completed successfully.
                #if LOG_COMPRESSION_STATS
                    dprintf("Deflated %ld bytes to %ld (%ld%%)\n", total_in, total_out, (total_out * 100) / total_in);
                #endif
                break;
            }
            else if (status != TDEFL_STATUS_OKAY) {
                // Compression somehow failed.
                dprintf("tdefl_compress() failed with status %d!\n", status);
                break;
            }

            /* Because we are compressing in-place, adjust avail_out to be the
             * gap between where the last compressed data was written, and
             * the start of the uncompressed data.
             */
            avail_out = next_in - next_out;

            if (avail_out == 0) {
                // Try to move the remaining uncompressed data to the end of
                // the buffer to make room for further compression
                unsigned char* copy_to = max - avail_in;
                if (copy_to <= next_in) {
                    dprintf("Output buffer insufficient for compressed stream. Aborting!\n");
                    break;
                }
                if (avail_in) {
                    dprintf("Moved %ld bytes of data to free up %ld bytes of space in compression buffer!\n", avail_in, copy_to - next_out);
                    BlockMove((Ptr)next_in, (Ptr)copy_to, avail_in);
                }
                next_in = copy_to;
                avail_out = next_in - next_out;
            }
        }

        // Write the length byte
        *((unsigned long*)epb.dst) = total_out;

        // Send the data
        if(total_out > 0xFFFF) {
            dprintf("too much data to send!\n");
        } else {
            epb.bytesWritten = total_out + sizeof(unsigned long);
        }
        return gotMoreAfterwards;
    }
#else
    Boolean VNCEncoder::getCompressedChunk(wdsEntry *wds) {
        // Compress the data
        unsigned char *next_in;
        unsigned char *next_out = s_outbuf;

        next_out += sizeof(unsigned long); // Leave space for the zLib length

        size_t avail_out = COMP_OUT_BUF_SIZE;
        size_t avail_in = 0;
        size_t total_in = 0;
        size_t total_out = 0;
        Boolean gotMoreAfterwards = true;
        Boolean gotMore = true;

        // Compression.
        for (;;) {
            if (!avail_in && gotMore) {
                EncoderPB epb;
                epb.dst = fbUpdateBuffer;
                epb.bytesAvail = fbUpdateBufferSize;
                gotMoreAfterwards = getUncompressedChunk(epb);
                gotMore = gotMoreAfterwards && !VNCEncoder::isNewSubrect();
                avail_in = epb.bytesWritten;
                next_in = fbUpdateBuffer;
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
                    dprintf("Deflated %ld bytes to %ld (%ld%%).\n", total_in, total_out, 100L - (total_out * 100) / total_in);
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
        return gotMoreAfterwards;
    }
#endif
