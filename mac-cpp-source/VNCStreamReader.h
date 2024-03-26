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

#include "VNCServer.h"

#define kNumRDS      5       /* Larger numbers increase read performance */

class StreamReader {
    private:
        char const    *src;
        rdsEntry      *rdsPtr;
        unsigned short rdsLeft;
        unsigned short position;
    public:
        inline char const *getDataBlock(size_t *length) {
            *length = rdsLeft;
            return src;
        }

        inline unsigned short getPosition() {
            return position;
        }

        inline size_t avail() {
            return rdsLeft;
        }

        inline Boolean finished() {
            return rdsLeft == 0;
        }

        inline size_t skip(size_t len) {
            return copyTo(NULL, len);
        }

        void setPosition(unsigned short pos);
        void const * getAlignedBlock(size_t *length);
        size_t copyTo(void *dst, size_t len);
};

extern StreamReader inStream;

enum DispatchMsgResult {
    msgTooShort    = 0,  // false
    nextMessage    = 1,  // true
    returnToCaller = -1, // true
    badContext     = -2
};

enum CallContext {
    asInterrupt,
    asMainLoop
};

struct MessageData {
    CallContext              context;

    const VNCClientMessages *msgPtr;
    size_t                   msgAvail;
    size_t                   msgSize;
};

#define  offsetof(st, m) ((Size)((char *)&((st *)0)->m - (char *)0))
#define     endof(st, m) (offsetof(st,m) + sizeof(((st *)0)->m))

#define READ_ALL(arg) \
    pb->msgSize = sizeof(pb->msgPtr->arg); \
    if (pb->msgSize > pb->msgAvail) return msgTooShort;
#define READ_STR(arg) \
    pb->msgSize += pb->msgPtr->arg; \
    if (pb->msgSize > pb->msgAvail) return msgTooShort;
#define READ_TO(arg) \
    pb->msgSize = endof(VNCClientMessages, arg); \
    if (pb->msgSize > pb->msgAvail) return msgTooShort;
#define MUST_COPY() \
    if (pb->msgPtr != (const VNCClientMessages *)&vncClientMessage) return msgTooShort;
#define MAIN_LOOP_ONLY() \
    if (pb->context == asInterrupt) return badContext;
#define DISPATCH_MESSAGE(func, arg) \
    READ_ALL(arg); \
    func(pb->msgPtr->arg); \
    break;
#define DISPATCH_MSGWSTR(func, arg, lenarg) \
    READ_ALL(arg); \
    READ_STR(arg.lenarg); \
    func(pb->msgPtr->arg, ((const char *)pb->msgPtr) + sizeof(pb->msgPtr->arg)); \
    break;