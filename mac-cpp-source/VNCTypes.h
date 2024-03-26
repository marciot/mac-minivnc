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

#include "TightVNCTypes.h"

enum {
    mConnectionFailed  = 0,
    mNoAuthentication  = 1,
    mVNCAuthentication = 2,
    mTightAuth         = 16
};

enum {
    mAuthOK            = 0,
    mAuthFailed        = 1,
    mAuthTooMany       = 2
};

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

/*
 * References:
 *    https://www.iana.org/assignments/rfb/rfb.xhtml
 *    https://en.wikipedia.org/wiki/RFB_protocol
 *    https://vncdotool.readthedocs.io/en/0.8.0/rfbproto.html#encodings
 *    https://github.com/rfbproto/rfbproto/blob/master/rfbproto.rst
 */
enum {
    mRawEncoding      = 0,
    mCopyRectEncoding = 1,
    mRREEncoding      = 2,
    mCoRREEncoding    = 4,
    mHextileEncoding  = 5,
    mZLibEncoding     = 6,
    mTightEncoding    = 7,
    mZHextileEncoding = 8,
    mUltraEncoding    = 9,
    mTRLEEncoding     = 15,
    mZRLEEncoding     = 16,
    mH264Encoding     = 50,
    mVMWareMinEnc     = 0x574d5600,
    mVMWareMaxEnc     = 0x574d56ff,
    mTightQtyMaxEnc   = -23,
    mTightQtyMinEnc   = -32,
    mTightPNGEncoding = -140,
    mNewFBSizEncoding = -223,
    mLastRectEncoding = -224,
    mMousePosEncoding = -232,
    mCursorEncoding   = -239,
    mXCursorEncoding  = -240,
    mTightCmpMaxEnc   = -247,
    mTightCmpMinEnc   = -256,
    mQMUPtrChangeEnc  = -257,
    mQMUExtKeyEnc     = -258,
    mQMUAudioEnc      = -259,
    mGIIEncoding      = -305,
    mDeskNameEncoding = -307,
    mExtDesktopSizEnc = -308,
    mFenceEncoding    = -312,
    mContUpdtEncoding = -313,
    mCrsrWithAlphaEnc = -314,
    mJPEGQtyMaxEnc    = -412,
    mJPEGQtyMinEnc    = -512,
    mJPEGSubMaxEnc    = -763,
    mJPEGSubMinEnc    = -768
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
    unsigned char  redShift;
    unsigned char  greenShift;
    unsigned char  blueShift;
    unsigned char  padding[3];
};

struct VNCServerProto {
    char version[12];
};

struct VNCServerInit {
    unsigned short fbWidth;
    unsigned short fbHeight;
    VNCPixelFormat format;
    unsigned long  nameLength;
    char name[10];
};

struct VNCServerAuthTypeList {
    unsigned char numberOfAuthTypes;
    unsigned char authTypes[1];
};

struct VNCServerAuthType {
    unsigned long type;
    unsigned char challenge[16];
};

struct VNCServerAuthChallenge {
    unsigned char challenge[16];
};

struct VNCServerAuthResult {
    unsigned long result;
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
};

struct VNCFBUpdateRect {
    VNCRect rect;
    long encodingType;
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
    char                    message;
    VNCSetPixFormat         pixFormat;
    VNCSetEncoding          setEncoding;
    VNCFBUpdateReq          fbUpdateReq;
    VNCKeyEvent             keyEvent;
    VNCPointerEvent         pointerEvent;
    VNCClientCutText        cutText;

    // TightVNC Messages
    long                    tightVncExtMsg;
    TightVNCCapReply        tightCapReq;
    TightVNCFileUploadData  tightFileUploadData;
};

union VNCServerMessages {
    VNCServerProto          protocol;
    VNCServerAuthTypeList   authTypeList;
    VNCServerAuthType       authType;
    VNCServerAuthChallenge  authChallenge;
    VNCServerAuthResult     authResult;
    VNCServerInit           init;
    VNCFBUpdate             fbUpdate;
    VNCFBUpdateRect         fbUpdateRect;
    VNCSetColorMapHeader    fbColorMap;
};