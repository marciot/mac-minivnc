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

#pragma once

// Reference: https://libvncserver.sourceforge.net/doc/html/rfbtightproto_8h_source.html

enum {
    mTightVNCExt            = -4, // 0xFC
    mTightVNCExtFileListReq = 0xFC000102,
    mTightVNCExtFUpStartReq = 0xFC000106,
};

typedef struct {
    unsigned long hi;
    unsigned long lo;
} uint64;

typedef struct {
    unsigned long code;
    unsigned char vendor[4];
    unsigned char signature[8];
} TightVNCCapabilites;

typedef struct {
    unsigned long code;
} TightVNCCapReply;

struct TightVNCServerAuthCaps {
    unsigned long numberOfTunnelTypes;
    unsigned long numberOfAuthTypes;
    TightVNCCapabilites authTypes[2];
};

struct TightVNCServerInitCaps {
    unsigned short numberOfServerMesg;
    unsigned short numberOfClientMesg;
    unsigned short numberOfEncodings;
    unsigned short padding;
    TightVNCCapabilites serverMsg[40];
};

struct TightVNCFileUploadData {
    // File Upload Start Request
    unsigned long  message;
    unsigned long  fNameSize;
    const char    *filename; // char filename[fNameSize]
    unsigned char  uploadFlags;
    uint64         initialOffset;

    // File Upload Data Request
    unsigned char  compressionLevel;
    unsigned long  compressedSize;
    unsigned long  uncompressedSize;
    /* Followed by File[compressedSize],
       but if (realSize = compressedSize = 0) followed by uint32_t modTime  */

    // File Upload End Request
    unsigned short fileFlags;
    uint64         modificationTime;
    short          fRefNum;
};

#pragma options align=packed

struct TightVNCFileListEntry {
    uint64         fileSize;
    uint64         lastModified;
    unsigned short flags; // dir = 1; exe = 2
    unsigned long  dirNameSize;
    /* Followed by char Dirname[dirNameSize] */
};

struct TightVNCFileListReply {
    long           message;
    unsigned char  compressionLevel;
    unsigned long  compressedSize;
    unsigned long  uncompressedSize;
    // Followed by compressed data;
    //unsigned long  file count;
};

#pragma options align=reset
