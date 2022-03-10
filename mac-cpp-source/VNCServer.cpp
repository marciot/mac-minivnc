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

#include <stdio.h>
#include <string.h>

#include <Devices.h>

#include "VNCServer.h"
#include "VNCKeyboard.h"
#include "VNCTypes.h"
#include "VNCFrameBuffer.h"
#include "VNCScreenHash.h"
#include "VNCEncodeTRLE.h"
#include "ChainedTCPHelper.h"

//#define VNC_DEBUG

#ifndef USE_STDOUT
    #define printf ShowStatus
    int ShowStatus(const char* format, ...);
#endif

enum {
    VNC_STOPPED,
    VNC_STARTING,
    VNC_WAITING,
    VNC_CONNECTED,
    VNC_RUNNING,
    VNC_STOPPING,
    VNC_ERROR
} vncState = VNC_STOPPED;

Boolean allowControl = true, sendGraphics = true, allowIncremental = true, fbColorMapNeedsUpdate = true;
Ptr recvBuffer;

#define kNumRDS      5       /* Larger numbers increase read performance */
#define kBufSize     16384   /* Size for TCP stream buffer and receive buffer */
#define kReadTimeout 10

static asm void PreCompletion(TCPiopb *pb);

Boolean tcpSuccess(TCPiopb *pb);

pascal void tcpStreamCreated(TCPiopb *pb);
pascal void tcpStreamClosed(TCPiopb *pb);
pascal void tcpSendProtocolVersion(TCPiopb *pb);
pascal void tcpReceiveClientProtocolVersion(TCPiopb *pb);
pascal void tcpRequestClientProtocolVersion(TCPiopb *pb);
pascal void tcpSendSecurityHandshake(TCPiopb *pb);
pascal void tcpRequestClientInit(TCPiopb *pb);
pascal void tcpSendServerInit(TCPiopb *pb);
pascal void vncPeekMessage(TCPiopb *pb);
pascal void vncFinishMessage(TCPiopb *pb);
pascal void vncSendColorMapEntries(TCPiopb *pb);

void processMessageFragment(const char *src, size_t len);

size_t getSetEncodingFragmentSize(size_t bytesRead);
void processSetEncodingsFragment(size_t bytesRead, char *&dst);

void vncSetPixelFormat(const VNCSetPixFormat &);
void vncEncoding(unsigned long);
void vncKeyEvent(const VNCKeyEvent &);
void vncPointerEvent(const VNCPointerEvent &);
void vncClientCutText(const VNCClientCutText &);
void vncFBUpdateRequest(const VNCFBUpdateReq &);
void vncSendFBUpdate(Boolean incremental);

pascal void vncGotDirtyRect(int x, int y, int w, int h);
pascal void vncSendFBUpdateColorMap(TCPiopb *pb);
pascal void vncSendFBUpdateHeader(TCPiopb *pb);
pascal void vncSendFBUpdateRow(TCPiopb *pb);
pascal void vncSendFBUpdateFinished(TCPiopb *pb);

/* From github.com/jeeb/mpc-be/blob/master/include/qt/LowMem.h
 *  EXTERN_API(void) LMSetMouseTemp(Point value)        TWOWORDINLINE(0x21DF, 0x0828)  // movel %sp@+,0x00000828
 *  EXTERN_API(void) LMSetRawMouseLocation(Point value) TWOWORDINLINE(0x21DF, 0x082C)  // movel %sp@+,0x0000082c
 *  EXTERN_API(void) LMSetMouseLocation(Point value)    TWOWORDINLINE(0x21DF, 0x0830)  // movel %sp@+,0x00000830
 *  EXTERN_API(void) LMSetMouseButtonState(UInt8 value) TWOWORDINLINE(0x11DF, 0x0172)  // moveb %sp@+,0x00000172
 *
 * From github.com/jeeb/mpc-be/blob/master/include/qt/QuickDraw.h
 *  EXTERN_API(void) LMSetCursorNew(Boolean value)      TWOWORDINLINE(0x11DF, 0x08CE)  // moveb %sp@+,0x000008ce
 */

void LMSetMBTicks(unsigned long val);
void LMSetMouseTemp(Point pt);
void LMSetRawMouseLocation(Point pt);
void LMSetMouseLocation(Point pt);
void LMSetCursorNew(Boolean val);
void LMSetMouseButtonState(unsigned char val);
Boolean LMGetCrsrCouple();

void LMSetMBTicks(unsigned long val)          {*((unsigned long*) 0x016e) = val;}
void LMSetMouseTemp(Point pt)                 {*((unsigned long*) 0x0828) = *(long*)&pt;}
void LMSetRawMouseLocation(Point pt)          {*((unsigned long*) 0x082c) = *(long*)&pt;}
void LMSetMouseLocation(Point pt)             {*((unsigned long*) 0x0830) = *(long*)&pt;}
void LMSetCursorNew(Boolean val)              {*((Boolean*)       0x08ce) = val;}
void LMSetMouseButtonState(unsigned char val) {*((unsigned char*) 0x0172) = val;}
Boolean LMGetCrsrCouple()                     {return * (Boolean*) 0x8cf;}

ExtendedTCPiopb    epb_recv;
ExtendedTCPiopb    epb_send;
ChainedTCPHelper   tcp;
StreamPtr          stream;
OSErr              vncError;
char              *vncServerVersion = "RFB 003.003\n";
char               vncClientVersion[13];
long               vncSecurityHandshake = 1;
char               vncClientInit;
VNCServerInit      vncServerInit;
VNCClientMessages  vncClientMessage;
VNCServerMessages  vncServerMessage;

wdsEntry           myWDS[3];
rdsEntry           myRDS[kNumRDS + 1];

VNCRect            fbUpdateRect;
Boolean            fbUpdateInProgress, fbUpdatePending;

OSErr vncServerStart() {
    VNCKeyboard::Setup();

    vncError = VNCFrameBuffer::setup();
    if (vncError != noErr) return vncError;

    vncError = VNCEncoder::setup();
    if (vncError != noErr) return vncError;

    vncError = VNCScreenHash::setup();
    if (vncError != noErr) return vncError;

    fbUpdateInProgress = false;
    fbUpdatePending = false;
    fbColorMapNeedsUpdate = true;

    fbUpdateRect.x = 0;
    fbUpdateRect.y = 0;
    fbUpdateRect.w = 0;
    fbUpdateRect.h = 0;

    printf("Opening network driver\n");
    vncError = tcp.begin(&epb_recv.pb);
    if (vncError != noErr) return vncError;

    epb_recv.ourA5 = SetCurrentA5();
    epb_recv.pb.ioCompletion = PreCompletion;

    printf("Creating network stream\n");
    recvBuffer = NewPtr(kBufSize);
    vncError = MemError();
    if(vncError != noErr) return vncError;

    tcp.then(&epb_recv.pb, tcpStreamCreated);
    tcp.createStream(&epb_recv.pb, recvBuffer, kBufSize);

    return noErr;
}

OSErr vncServerStop() {
    if (vncState != VNC_STOPPED) {
        vncState = VNC_STOPPING;
        tcp.then(&epb_recv.pb, tcpStreamClosed);
        tcp.release(&epb_recv.pb, stream);
        while(vncState != VNC_STOPPED) {
            SystemTask();
        }
    }

    VNCScreenHash::destroy();
    VNCEncoder::destroy();
    VNCFrameBuffer::destroy();

    return noErr;
}

Boolean vncServerStopped() {
    return vncState == VNC_STOPPED;
}

OSErr vncServerError() {
    if(vncState == VNC_ERROR) {
        return vncError;
    } else {
        return noErr;
    }
}

Boolean tcpSuccess(TCPiopb *pb) {
    OSErr err = tcp.getResult(pb);
    if(err != noErr) {
         vncState = VNC_ERROR;
         if(vncError == noErr) {
            vncError = err;
         }
        return false;
    }
    return true;
}

pascal void tcpStreamCreated(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        vncState = VNC_WAITING;
        stream = tcp.getStream(pb);
        // wait for a connection
        printf("Waiting for connection\n");
        tcp.then(pb, tcpSendProtocolVersion);
        tcp.waitForConnection(pb, stream, kTimeOut, 5900);
    }
}

pascal void tcpStreamClosed(TCPiopb *pb) {
    vncState = VNC_STOPPED;
}

pascal void tcpSendProtocolVersion(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        vncState = VNC_CONNECTED;
        stream = tcp.getStream(pb);

        // send the VNC protocol version
        #ifdef VNC_DEBUG
            printf("Sending Protocol Version!\n");
        #endif
        myWDS[0].ptr = (Ptr) vncServerVersion;
        myWDS[0].length = strlen(vncServerVersion);
        myWDS[1].ptr = 0;
        myWDS[1].length = 0;
        tcp.then(pb, tcpRequestClientProtocolVersion);
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void tcpRequestClientProtocolVersion(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        // request the client VNC protocol version
        tcp.then(pb, tcpSendSecurityHandshake);
        tcp.receive(pb, stream, vncClientVersion, 12);
    }
}

pascal void tcpSendSecurityHandshake(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        #ifdef VNC_DEBUG
            printf("Client VNC Version: %s\n", vncClientVersion);
        #endif

        // send the VNC security handshake
        printf("Sending VNC Security Handshake\n");
        myWDS[0].ptr = (Ptr) &vncSecurityHandshake;
        myWDS[0].length = sizeof(long);
        tcp.then(pb, tcpRequestClientInit);
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void tcpRequestClientInit(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        // get client init message
        tcp.then(pb, tcpSendServerInit);
        tcp.receive(pb, stream, &vncClientInit, 1);
    }
}

pascal void tcpSendServerInit(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        #ifdef VNC_DEBUG
            printf("Client Init: %d\n", vncClientInit);
        #endif
        // send the VNC security handshake
        printf("Connection established!\n");
        #ifdef VNC_FB_WIDTH
            vncServerInit.fbWidth = VNC_FB_WIDTH;
            vncServerInit.fbHeight = VNC_FB_HEIGHT;
        #else
            vncServerInit.fbWidth = fbWidth;
            vncServerInit.fbHeight = fbHeight;
        #endif
        vncServerInit.format.bigEndian = 1;
        vncServerInit.format.bitsPerPixel = 8;
        #ifdef VNC_FB_BITS_PER_PIX
            vncServerInit.format.depth = VNC_FB_BITS_PER_PIX;
        #else
            vncServerInit.format.depth = fbDepth;
        #endif
        vncServerInit.format.trueColor = 0;
        vncServerInit.nameLength = 10;
        memcpy(vncServerInit.name, "Macintosh ", 10);

        myWDS[0].ptr = (Ptr) &vncServerInit;
        myWDS[0].length = sizeof(vncServerInit);

        // Prepare a copy of our parameter block for sending frames
        BlockMove(&epb_recv, &epb_send, sizeof(ExtendedTCPiopb));

        vncState = VNC_RUNNING;
        tcp.then(pb, vncPeekMessage);
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

void processMessageFragment(const char *src, size_t len) {
    static char *dst = (char *)&vncClientMessage;

    /**
     * Process unfragmented mPointerEvent and mFBUpdateRequest messages,
     * the common case
     *
     * Whole messages will be processed without copying. Upon exit, either
     * len = 0 and all data has been consumed, or len != 0 and src will
     * point to messages for further processing.
     */

    if (  (dst == (char *)&vncClientMessage) &&        // If there are no prior fragments...
         ( ((unsigned long)src & 0x01) == 0) ) { // ... and word aligned for 68000
        while(true) {
            if((*src == mPointerEvent) && (len >= sizeof(VNCPointerEvent))) {
                vncPointerEvent(*(VNCPointerEvent*)src);
                src += sizeof(VNCPointerEvent);
                len -= sizeof(VNCPointerEvent);
            }

            else if((*src == mFBUpdateRequest) && (len >= sizeof(VNCFBUpdateReq))) {
                vncFBUpdateRequest(*(VNCFBUpdateReq*)src);
                src += sizeof(VNCFBUpdateReq);
                len -= sizeof(VNCFBUpdateReq);
            }

            else
                break; // Exit loop for any other message
        }
    }

    /**
     * Deal with fragmented messages, a less common case
     *
     * While processing fragmented messages, src is a read pointer into the
     * RDS while dst is a write pointer into vncClientMessage. Bytes are copied
     * from src to dst until a full message detected, at which point it
     * is processed and dst is reset to the top of vncClientMessage.
     *
     * Certain messages in the VNC protocol are variable size, so a message
     * may be copied in bits until its total size is known.
     */

    size_t bytesRead, msgSize;

    while(len) {
        // If we've read no bytes yet, get the message type
        if(dst == (char *)&vncClientMessage) {
            vncClientMessage.message = *src++;
            len--;
            dst++;
        }

        // How many bytes have been read up to this point?
        bytesRead = dst - (char *)&vncClientMessage;

        // Figure out the message length
        switch(vncClientMessage.message) {
            case mSetPixelFormat:  msgSize = sizeof(VNCSetPixFormat); break;
            case mFBUpdateRequest: msgSize = sizeof(VNCFBUpdateReq); break;
            case mKeyEvent:        msgSize = sizeof(VNCKeyEvent); break;
            case mPointerEvent:    msgSize = sizeof(VNCPointerEvent); break;
            case mClientCutText:   msgSize = sizeof(VNCClientCutText); break;
            case mSetEncodings:    msgSize = getSetEncodingFragmentSize(bytesRead); break;
            default:
                printf("Invalid message: %d\n", vncClientMessage.message);
                vncServerStop();
                break;
        }

        // Copy message bytes
        if(bytesRead < msgSize) {
            size_t bytesToCopy = msgSize - bytesRead;

            // Copy the message bytes
            if(bytesToCopy > len) bytesToCopy = len;

            BlockMove(src, dst, bytesToCopy);
            src += bytesToCopy;
            len -= bytesToCopy;
            dst += bytesToCopy;
        }

        // How many bytes have been read up to this point?
        bytesRead = dst - (char *)&vncClientMessage;

        if(bytesRead == msgSize) {
            // Dispatch the message
            switch(vncClientMessage.message) {
                // Fixed sized messages
                case mSetPixelFormat:  vncSetPixelFormat(  vncClientMessage.pixFormat); break;
                case mFBUpdateRequest: vncFBUpdateRequest( vncClientMessage.fbUpdateReq); break;
                case mKeyEvent:        vncKeyEvent(        vncClientMessage.keyEvent); break;
                case mPointerEvent:    vncPointerEvent(    vncClientMessage.pointerEvent); break;
                case mClientCutText:   vncClientCutText(   vncClientMessage.cutText); break;
                // Variable sized messages
                case mSetEncodings:
                    processSetEncodingsFragment(bytesRead, dst);
                    continue;
            }
            // Prepare to receive next message
            dst = (char *)&vncClientMessage;
        }
    }
}

size_t getSetEncodingFragmentSize(size_t bytesRead) {
    return ((bytesRead >= sizeof(VNCSetEncoding)) && vncClientMessage.setEncoding.numberOfEncodings) ?
        sizeof(VNCSetEncodingOne) :
        sizeof(VNCSetEncoding);
}

void processSetEncodingsFragment(size_t bytesRead, char *&dst) {
    if(bytesRead == sizeof(VNCSetEncodingOne)) {
        vncEncoding(vncClientMessage.setEncodingOne.encoding);
        vncClientMessage.setEncodingOne.numberOfEncodings--;
    }

    if(vncClientMessage.setEncoding.numberOfEncodings) {
        // Preare to read the next encoding
        dst = (char *) &vncClientMessage.setEncodingOne.encoding;
    } else {
        // Prepare to receive next message
        dst = (char *) &vncClientMessage;
    }
}

pascal void vncPeekMessage(TCPiopb *pb) {
    if (tcpSuccess(pb) && vncState == VNC_RUNNING) {
        // read the first byte of a message
        tcp.then(pb, vncFinishMessage);
        tcp.receiveNoCopy(pb, stream, myRDS, kNumRDS);
    }
}

pascal void vncFinishMessage(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        for(int i = 0; i < kNumRDS; i++) {
            if(myRDS[i].length == 0) break;
            processMessageFragment(myRDS[i].ptr, myRDS[i].length);
        }

        // read subsequent messages
        tcp.then(pb, vncPeekMessage);
        tcp.receiveReturnBuffers(pb);
    }
}

void vncEncoding(unsigned long) {
    #ifdef VNC_DEBUG
        printf("Got encoding %ld\n", vncClientMessage.setEncodingOne.encoding);
    #endif
}

void vncSetPixelFormat(const VNCSetPixFormat &pixFormat) {
    const VNCPixelFormat &format = pixFormat.format;
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    if (format.trueColor || format.depth != fbDepth) {
        printf("Invalid pixel format requested\n");
        vncState = VNC_ERROR;
    }
}

void vncKeyEvent(const VNCKeyEvent &keyEvent) {
    if (allowControl) {
        VNCKeyboard::PressKey(keyEvent.key, keyEvent.down);
    }
}

void vncPointerEvent(const VNCPointerEvent &pointerEvent) {
    if (allowControl) {
        //printf("Got pointerEvent %d,%d,%d\n", pointerEvent.x, pointerEvent.y, (short) pointerEvent.btnMask);

        //static UInt32 lastEventTicks = 0;

        // From chromiumvncserver.340a5.src, VNCResponderThread.cp

        /*if(msg.btnMask != lastBMask) {
            while((TickCount() - lastEventTicks) < 2)
                ;
            lastEventTicks = TickCount();
        }*/

        Point newMousePosition;
        newMousePosition.h = pointerEvent.x;
        newMousePosition.v = pointerEvent.y;
        LMSetMouseTemp(newMousePosition);
        LMSetRawMouseLocation(newMousePosition);
        LMSetCursorNew(LMGetCrsrCouple());

        // On the Mac Plus, it is necessary to prevent the VBL task from
        // over-writing the button state by keeping MBTicks ahead of Ticks

        #define DELAY_VBL_TASK(seconds) LMSetMBTicks(LMGetTicks() + 60 * seconds);

        if(pointerEvent.btnMask) {
            DELAY_VBL_TASK(10);
        }

        // Use low memory globals to handle clicks
        static unsigned char lastBMask = 0;

        if(lastBMask != pointerEvent.btnMask) {
            lastBMask = pointerEvent.btnMask;

            if(pointerEvent.btnMask) {
                DELAY_VBL_TASK(10);
                LMSetMouseButtonState(0x00);
                PostEvent(mouseDown, 0);
            } else {
                DELAY_VBL_TASK(0);
                LMSetMouseButtonState(0x80);
                PostEvent(mouseUp, 0);
            }
        }
    }
}

void vncClientCutText(const VNCClientCutText &) {
}

void vncFBUpdateRequest(const VNCFBUpdateReq &fbUpdateReq) {
    if(!sendGraphics) return;
    if(fbUpdateInProgress) {
        fbUpdatePending = true;
    } else {
        fbUpdateRect = fbUpdateReq.rect;
        vncSendFBUpdate(allowIncremental && fbUpdateReq.incremental);
    }
}

// Callback for the VBL task
pascal void vncGotDirtyRect(int x, int y, int w, int h) {
    if(fbUpdateInProgress) {
        printf("Got dirty rect while busy\n");
        return;
    }
    //printf("Got dirty rect");
    if (vncState == VNC_RUNNING) {
        //printf("%d,%d,%d,%d\n",x,y,w,h);
        fbUpdateRect.x = x;
        fbUpdateRect.y = y;
        fbUpdateRect.w = w;
        fbUpdateRect.h = h;
        vncSendFBUpdateColorMap(&epb_send.pb);
    }
}

void vncSendFBUpdate(Boolean incremental) {
    if(incremental) {
        // Ask the VBL task to determine what needs to be updated
        OSErr err = VNCScreenHash::requestDirtyRect(
            fbUpdateRect.x,
            fbUpdateRect.y,
            fbUpdateRect.w,
            fbUpdateRect.h,
            vncGotDirtyRect
        );
        if(err != noErr) {
            printf("Failed to request update %d", err);
            vncError = err;
        }
    } else {
        vncSendFBUpdateColorMap(&epb_send.pb);
    }
}

pascal void vncSendFBUpdateColorMap(TCPiopb *pb) {
    fbUpdateInProgress = true;
    fbUpdatePending = false;
    if(fbColorMapNeedsUpdate) {
        fbColorMapNeedsUpdate = false;

        // Send the header
        #ifdef VNC_FB_BITS_PER_PIX
            const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
        #endif
        const unsigned int paletteSize = 1 << fbDepth;
        vncServerMessage.fbColorMap.message = mSetCMapEntries;
        vncServerMessage.fbColorMap.padding = 0;
        vncServerMessage.fbColorMap.firstColor = 0;
        vncServerMessage.fbColorMap.numColors = paletteSize;

        myWDS[0].ptr = (Ptr) &vncServerMessage;
        myWDS[0].length = sizeof(VNCSetColorMapHeader);
        myWDS[1].ptr = (Ptr) VNCFrameBuffer::getPalette();
        myWDS[1].length = paletteSize * sizeof(VNCColor);
        myWDS[2].ptr = 0;
        myWDS[2].length = 0;
        tcp.then(pb, vncSendFBUpdateHeader);
        tcp.send(pb, stream, myWDS, kTimeOut);
    } else {
        vncSendFBUpdateHeader(pb);
    }
}

pascal void vncSendFBUpdateHeader(TCPiopb *pb) {
    fbUpdateInProgress = true;
    fbUpdatePending = false;

    // Make sure x falls on a byte boundary
    unsigned char dx = fbUpdateRect.x & 7;
    fbUpdateRect.x -= dx;
    fbUpdateRect.w += dx;

    // Make sure width is a multiple of 16
    fbUpdateRect.w = (fbUpdateRect.w + 15) & ~15;

    #ifdef VNC_FB_WIDTH
        const unsigned int fbWidth = VNC_FB_WIDTH;
    #endif
    if((fbUpdateRect.x + fbUpdateRect.w) > fbWidth) {
        fbUpdateRect.x = fbWidth - fbUpdateRect.w;
    }

    // Prepare to stream the data
    VNCEncoder::begin();

    // Send the header
    vncServerMessage.fbUpdate.message = mFBUpdate;
    vncServerMessage.fbUpdate.padding = 0;
    vncServerMessage.fbUpdate.numRects = 1;
    vncServerMessage.fbUpdate.rect = fbUpdateRect;
    vncServerMessage.fbUpdate.encodingType = mTRLEEncoding;
    myWDS[0].ptr = (Ptr) &vncServerMessage;
    myWDS[0].length = sizeof(VNCFBUpdate);
    myWDS[1].ptr = 0;
    myWDS[1].length = 0;
    tcp.then(pb, vncSendFBUpdateRow);
    tcp.send(pb, stream, myWDS, kTimeOut);
}

pascal void vncSendFBUpdateRow(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        // Add the termination
        myWDS[1].ptr = 0;
        myWDS[1].length = 0;

        // Get a row from the encoder
        const Boolean gotMore = VNCEncoder::getChunk(fbUpdateRect.x, fbUpdateRect.y, fbUpdateRect.w, fbUpdateRect.h, myWDS);
        if(gotMore) {
            tcp.then(pb, vncSendFBUpdateRow);
        } else {
            fbUpdateRect.w = fbUpdateRect.h = 0;
            tcp.then(pb, vncSendFBUpdateFinished);
        }
        tcp.send(pb, stream, myWDS, kTimeOut);
    }
}

pascal void vncSendFBUpdateFinished(TCPiopb *pb) {
    //printf("Update finished");
    fbUpdateInProgress = false;
    if(fbUpdatePending) {
        vncSendFBUpdate(true);
    }
}

// PreCompletion routine as described in "Asyncronous Routines on the Macintosh", Develop magazine, March 1993

static asm void PreCompletion(TCPiopb *pb) {
    link    a6,#0                // Link for the debugger
    movem.l a5,-(sp)             // Preserve the A5 register
    move.l  a0,-(sp)             // Pass PB pointer as the parameter
    move.l  -8(a0),a5            // Set A5 to passed value (ourA5).
    move.l  -4(a0),a0            // A0 = real completion routine address
    jsr     (a0)                 // Transfer control to ourCompletion
    movem.l (sp)+,a5             // Restore A5 register
    unlk    a6                   // Unlink.
    rts                          // Return
    dc.b    0x8D,"PreCompletion"
    dc.w    0x0000
}