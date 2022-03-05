/************************************************************

    VNCTypes.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 12/14/21

       LAST REVISION: 12/14/21

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#pragma once

enum {
    mSetPixelFormat   = 0,
    mSetEncodings     = 2,
    mFBUpdateRequest  = 3,
    mKeyEvent         = 4,
    mPointerEvent     = 5,
    mClientCutText    = 6
};

enum {
    mFBUpdate         = 0,
    mSetCMapEntries   = 1,
    mBell             = 2,
    mServerCutText    = 3
};

enum {
    mRawEncoding      = 0,
    mTRLEEncoding     = 15
};

struct VNCRect {
    unsigned short x;
    unsigned short y;
    unsigned short w;
    unsigned short h;
};

struct VNCPixelFormat {
    unsigned char  bitsPerPixel;
    unsigned char  depth;
    unsigned char  bigEndian;
    unsigned char  trueColor;
    unsigned short redMax;
    unsigned short greenMax;
    unsigned short blueMax;
    unsigned char redShift;
    unsigned char greenShift;
    unsigned char blueShift;
    char padding[3];
};

struct VNCServerInit {
    unsigned short fbWidth;
    unsigned short fbHeight;
    VNCPixelFormat format;
    unsigned long  nameLength;
    char name[10];
};

struct VNCSetPixFormat {
    unsigned char message;
    unsigned char padding[3];
    VNCPixelFormat format;
};

struct VNCSetEncoding {
    unsigned char message;
    unsigned char padding;
    unsigned short numberOfEncodings;
};

struct VNCSetEncodingOne {
    unsigned char message;
    unsigned char padding;
    unsigned short numberOfEncodings;
    unsigned long encoding;
};

struct VNCColor {
    unsigned short red;
    unsigned short green;
    unsigned short blue;
};

struct VNCSetColorMapHeader {
    unsigned char  message;
    unsigned char  padding;
    unsigned short firstColor;
    unsigned short numColors;
};

struct VNCFBUpdateReq{
    unsigned char  message;
    unsigned char  incremental;
    VNCRect        rect;
};

struct VNCFBUpdate {
    unsigned char  message;
    unsigned char  padding;
    unsigned short numRects;
    VNCRect        rect;
    long           encodingType;
};

struct VNCKeyEvent {
    unsigned char  message;
    unsigned char down;
    unsigned short padding;
    unsigned long key;
};

struct VNCPointerEvent {
    unsigned char message;
    unsigned char btnMask;
    unsigned short x;
    unsigned short y;
};

struct VNCClientCutText {
    unsigned char message;
    unsigned char padding[3];
    unsigned long length;
};

union VNCClientMessages {
    char               message;
    VNCSetPixFormat    pixFormat;
    VNCSetEncoding     setEncoding;
    VNCSetEncodingOne  setEncodingOne;
    VNCFBUpdateReq     fbUpdateReq;
    VNCKeyEvent        keyEvent;
    VNCPointerEvent    pointerEvent;
    VNCClientCutText   cutText;
};

union VNCServerMessages {
    VNCFBUpdate           fbUpdate;
    VNCSetColorMapHeader  fbColorMap;
};