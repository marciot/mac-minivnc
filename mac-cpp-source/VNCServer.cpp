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

#include <stdio.h>
#include <string.h>

#include <Devices.h>

#include "VNCServer.h"
#include "VNCKeyboard.h"
#include "VNCTypes.h"
#include "VNCFrameBuffer.h"
#include "VNCScreenHash.h"
#include "VNCEncoder.h"
#include "VNCEncodeCursor.h"
#include "ChainedTCPHelper.h"
#include "msgbuf.h"

#define VNC_DEBUG
//#define POLL_CONNECTION_STATUS

#define kNumRDS      5       /* Larger numbers increase read performance */
#define kBufSize     16384   /* Size for TCP stream buffer and receive buffer */
#define kReadTimeout 10

static asm void PreCompletion(TCPiopb *pb);

Boolean tcpSuccess(TCPiopb *pb);

pascal void tcpStreamCreated(TCPiopb *pb);
pascal void tcpStreamClosed(TCPiopb *pb);
pascal void tcpSendProtocolVersion(TCPiopb *pb);
pascal void tcpReceiveClientProtocolVersion(TCPiopb *pb);
pascal void tcpGetClientProtocolVersion(TCPiopb *pb);
pascal void tcpSendAuthTypes(TCPiopb *pb);
pascal void tcpGetAuthType(TCPiopb *pb);
pascal void tcpSendAuthChallenge(TCPiopb *pb);
pascal void tcpGetAuthChallengeResponse(TCPiopb *pb);
pascal void tcpSendAuthResult(TCPiopb *pb);
pascal void tcpWaitForClientInit(TCPiopb *pb);
pascal void tcpSendServerInit(TCPiopb *pb);
pascal void vncPeekMessage(TCPiopb *pb);
pascal void vncFinishMessage(TCPiopb *pb);
pascal void vncSendColorMapEntries(TCPiopb *pb);

void processMessageFragment(const char *src, size_t len);

size_t getSetEncodingFragmentSize(size_t bytesRead);
void processSetEncodingsFragment(size_t bytesRead, char *&dst);

void vncSetPixelFormat(const VNCSetPixFormat &);
void vncEncoding(unsigned long, Boolean);
void vncKeyEvent(const VNCKeyEvent &);
void vncPointerEvent(const VNCPointerEvent &);
void vncClientCutText(const VNCClientCutText &);
void vncFBUpdateRequest(const VNCFBUpdateReq &);
void vncSendFBUpdate(Boolean incremental);

pascal void vncGotDirtyRect(int x, int y, int w, int h);
pascal void vncSendFBUpdateColorMap(TCPiopb *pb);
pascal void vncPrepareForFBUpdate(TCPiopb *pb);
pascal void vncSendFBUpdateHeader(TCPiopb *pb);
pascal void vncFBUpdateEncodeCursor(TCPiopb *pb);
pascal void vncStartFBUpdate(TCPiopb *pb);
pascal void vncSendFBUpdateRow(TCPiopb *pb);
pascal void vncFinishFBUpdate(TCPiopb *pb);
pascal void vncStatusAvailable(TCPiopb *pb);
pascal void vncDeferredDataReady();

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
Ptr                recvBuffer;
OSErr              vncError;
char              *vncServerVersion = "RFB 003.007\n";
VNCClientMessages  vncClientMessage;
VNCServerMessages  vncServerMessage;
Point              vncLastMousePosition;
Boolean            runFBSyncedTasks = false;

wdsEntry           myWDS[3];
rdsEntry           myRDS[kNumRDS + 1];

VNCRect            fbUpdateRect;
unsigned long      fbUpdateStartTicks;

enum {
    VNC_STOPPED,
    VNC_STARTING,
    VNC_WAITING,
    VNC_CONNECTED,
    VNC_RUNNING,
    VNC_STOPPING,
    VNC_ERROR
} vncState = VNC_STOPPED;

VNCFlags vncFlags = {
    false, // fbColorMapNeedsUpdate
    false, // fbUpdateInProgress
    false, // fbUpdatePending
    false, // clientTakesRaw
    false, // clientTakesHexTile
    false, // clientTakesTRLE
    false, // clientTakesZRLE
    false, // clientTakesCursor
    false, // forceVNCAuth
};

OSErr vncServerStart() {
    VNCKeyboard::Setup();

    vncError = VNCFrameBuffer::setup();
    if (vncError != noErr) return vncError;

    vncError = VNCEncoder::setup();
    if (vncError != noErr) return vncError;

    vncError = VNCScreenHash::setup();
    if (vncError != noErr) return vncError;

    dprintf("Opening network driver\n");
    vncError = tcp.begin(&epb_recv.pb);
    if (vncError != noErr) return vncError;

    epb_recv.ourA5 = SetCurrentA5();
    epb_recv.pb.ioCompletion = PreCompletion;

    dprintf("Creating network stream\n");
    recvBuffer = NewPtr(kBufSize);
    vncError = MemError();
    if(vncError != noErr) return vncError;

    tcp.then(&epb_recv.pb, tcpStreamCreated);
    tcp.createStream(&epb_recv.pb, recvBuffer, kBufSize);

    // Set the forceVNCAuth to the default
    vncFlags.forceVNCAuth = vncConfig.forceVNCAuth;

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

Boolean vncServerActive() {
    return vncState == VNC_RUNNING;
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
        if(err == connectionClosing) {
            if(vncConfig.autoRestart) {
                tcp.then(&epb_recv.pb, tcpStreamCreated);
                tcp.abort(&epb_recv.pb, stream);
                return false;
            }
        }
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
        // wait for a connection
        vncState = VNC_WAITING;
        stream = tcp.getStream(pb);
        dprintf("-Waiting for connection");
        dprintf(" on port %d [ResEdit]\n", vncConfig.tcpPort);
        tcp.then(pb, tcpSendProtocolVersion);
        tcp.waitForConnection(pb, stream, kTimeOut, vncConfig.tcpPort);
    }
}

pascal void tcpStreamClosed(TCPiopb *pb) {
    vncState = VNC_STOPPED;
}

pascal void tcpSendProtocolVersion(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        const unsigned char *ip = (unsigned char *)&pb->csParam.open.remoteHost;
        dprintf("Got connection from %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

        vncState = VNC_CONNECTED;
        stream = tcp.getStream(pb);

        // send the VNC protocol version
        #ifdef VNC_DEBUG
            dprintf("Server VNC Version: %11s\n", vncServerVersion);
        #endif

        memcpy(vncServerMessage.protocol.version, vncServerVersion, 12);
        myWDS[0].ptr = (Ptr) &vncServerMessage;
        myWDS[0].length = 12;
        myWDS[1].ptr = 0;
        myWDS[1].length = 0;
        tcp.then(pb, tcpGetClientProtocolVersion);
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void tcpGetClientProtocolVersion(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        // request the client VNC protocol version
        tcp.then(pb, tcpSendAuthTypes);
        tcp.receive(pb, stream, vncServerMessage.protocol.version, 12);
    }
}

pascal void tcpSendAuthTypes(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        vncServerMessage.protocol.version[11] = 0;
        #ifdef VNC_DEBUG
            dprintf("Client VNC Version: %11s\n", vncServerMessage.protocol.version);
        #endif

        const unsigned char serverDefaultAuthType = vncFlags.forceVNCAuth ? mVNCAuthentication : mNoAuthentication;

        if(vncServerMessage.protocol.version[10] == '7' || vncServerMessage.protocol.version[10] == '8') {
            // RFB 3.7: Send a list of authetication types to the client and let the client decide
            vncServerMessage.authTypeList.numberOfAuthTypes = 1;
            vncServerMessage.authTypeList.authTypes[0] = serverDefaultAuthType;

            #ifdef VNC_DEBUG
                dprintf("Supported authentication types count: %d\n", vncServerMessage.authTypeList.numberOfAuthTypes);
                dprintf("  Authentication type: %d\n", vncServerMessage.authTypeList.authTypes[0]);
            #endif

            myWDS[0].ptr = (Ptr) &vncServerMessage;
            myWDS[0].length = sizeof(VNCServerAuthTypeList);
            tcp.then(pb, tcpGetAuthType);
        } else {
            // RFB 3.3: Server decides the authentication type
            vncServerMessage.authType.type = serverDefaultAuthType;
            vncClientMessage.message       = serverDefaultAuthType;

            myWDS[0].ptr = (Ptr) &vncServerMessage;
            myWDS[0].length = sizeof(unsigned long);
            tcp.then(pb, tcpSendAuthChallenge);
        }
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void tcpGetAuthType(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        tcp.then(pb, tcpSendAuthChallenge);
        tcp.receive(pb, stream, (Ptr) &vncClientMessage, sizeof(unsigned char));
    }
}

pascal void tcpSendAuthChallenge(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        #ifdef VNC_DEBUG
            dprintf("Selected authentication type: %s [ResEdit]\n", vncClientMessage.message == mVNCAuthentication ? "vncAuth" : "noAuth");
        #endif

        switch(vncClientMessage.message) {
            case mNoAuthentication:
                tcpWaitForClientInit(pb);
                break;
            case mVNCAuthentication:
                #ifdef VNC_DEBUG
                    dprintf("Sending authentication challenge\n");
                #endif
                memcpy(vncServerMessage.authChallenge.challenge,"PASSWORDPASSWORD", 16);
                myWDS[0].ptr = (Ptr) &vncServerMessage;
                myWDS[0].length = sizeof(VNCServerAuthChallenge);
                tcp.then(pb, tcpGetAuthChallengeResponse);
                tcp.send(pb, stream, myWDS, kTimeOut, true);
                break;
            default:
                 dprintf("Invalid authentication type!\n");
                 vncState = VNC_ERROR;
                 break;
        }
    }
}

pascal void tcpGetAuthChallengeResponse(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        tcp.then(pb, tcpSendAuthResult);
        tcp.receive(pb, stream, (Ptr) &vncServerMessage, 16);
    }
}

pascal void tcpSendAuthResult(TCPiopb *pb) {
    // The macOS Screen Sharing client in High Sierra always breaks the connection
    // here and immediately tries to reconnect.
    OSErr err = tcp.getResult(pb);
    if(err == connectionClosing) {
        dprintf("Connection terminated by client, likely macOS Screen Sharing client?\n");
        tcp.then(&epb_recv.pb, tcpStreamCreated);
        tcp.abort(&epb_recv.pb, stream);
        return;
    }
    if (tcpSuccess(pb)) {
        #ifdef VNC_DEBUG
            dprintf("Got challenge response\nSending authentication reply\n");
        #endif

        vncServerMessage.authResult.result = mAuthOK;
        myWDS[0].ptr = (Ptr) &vncServerMessage;
        myWDS[0].length = sizeof(VNCServerAuthResult);
        tcp.then(pb, tcpWaitForClientInit);
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void tcpWaitForClientInit(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        // get client init message
        tcp.then(pb, tcpSendServerInit);
        tcp.receive(pb, stream, (Ptr) &vncClientMessage, 1);
    }
}

pascal void tcpSendServerInit(TCPiopb *pb) {
    // The macOS Screen Sharing client in High Sierra always breaks the connection
    // here and immediately tries to reconnect.
    OSErr err = tcp.getResult(pb);
    if(err == connectionClosing) {
        dprintf("Connection terminated by client, likely macOS Screen Sharing client? Will try turning on authentication.\n");
        tcp.then(&epb_recv.pb, tcpStreamCreated);
        tcp.abort(&epb_recv.pb, stream);
        vncFlags.forceVNCAuth = true;
        return;
    }
    if (tcpSuccess(pb)) {
        #ifdef VNC_DEBUG
            dprintf("Client Init: %d\n", vncClientMessage.message);
        #endif

        dprintf("-Connection established!\n");
        #ifdef VNC_FB_WIDTH
            vncServerMessage.init.fbWidth = VNC_FB_WIDTH;
            vncServerMessage.init.fbHeight = VNC_FB_HEIGHT;
        #else
            vncServerMessage.init.fbWidth = fbWidth;
            vncServerMessage.init.fbHeight = fbHeight;
        #endif
        vncServerMessage.init.format.bigEndian = 1;

        #if 0
            vncServerMessage.init.format.trueColor = 1;
            vncServerMessage.init.format.bitsPerPixel = 32;
            vncServerMessage.init.format.depth = 32;
            vncServerMessage.init.format.redMax = 255;    // 2 bits
            vncServerMessage.init.format.greenMax = 255;  // 3 bits
            vncServerMessage.init.format.blueMax = 255;   // 2 bits
            vncServerMessage.init.format.redShift = 16;
            vncServerMessage.init.format.greenShift = 8;
            vncServerMessage.init.format.blueShift = 0;

        #else
            vncServerMessage.init.format.trueColor = 0;
            vncServerMessage.init.format.bitsPerPixel = 8;
            #ifdef VNC_FB_BITS_PER_PIX
                vncServerMessage.init.format.depth = VNC_FB_BITS_PER_PIX;
            #else
                vncServerMessage.init.format.depth = fbDepth;
            #endif
            vncServerMessage.init.format.redMax = 3;    // 2 bits
            vncServerMessage.init.format.greenMax = 7;  // 3 bits
            vncServerMessage.init.format.blueMax = 3;   // 2 bits
            vncServerMessage.init.format.redShift = 5;
            vncServerMessage.init.format.greenShift = 2;
            vncServerMessage.init.format.blueShift = 0;
        #endif

        pendingPixFormat.bitsPerPixel = 0;
        memcpy(&fbPixFormat, &vncServerMessage.init.format, sizeof(VNCPixelFormat));
        cPixelBytes = fbPixFormat.bitsPerPixel / 8;
        vncFlags.fbColorMapNeedsUpdate = true;
        vncFlags.fbUpdateInProgress = false;
        vncFlags.fbUpdatePending = false;

        fbUpdateRect.x = 0;
        fbUpdateRect.y = 0;
        fbUpdateRect.w = 0;
        fbUpdateRect.h = 0;

        VNCEncoder::clear();
        VNCEncodeCursor::clear();

        dprintf("Session name: %s [ResEdit]\n", vncConfig.sessionName);
        vncServerMessage.init.nameLength = strlen(vncConfig.sessionName);
        strncpy(vncServerMessage.init.name, vncConfig.sessionName, vncServerMessage.init.nameLength);

        myWDS[0].ptr = (Ptr) &vncServerMessage.init;
        myWDS[0].length = sizeof(vncServerMessage.init);

        // Set the forceVNCAuth to the default
        vncFlags.forceVNCAuth = vncConfig.forceVNCAuth;

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
                dprintf("Invalid message: %d\n", vncClientMessage.message);
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
        vncEncoding(vncClientMessage.setEncodingOne.encoding, vncClientMessage.setEncodingOne.numberOfEncodings > 1);
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

void vncEncoding(unsigned long encoding, Boolean hasMore) {
    #ifdef VNC_DEBUG
        dprintf("Got encoding %08lx %-8s hasMore:%d\n", encoding, VNCEncoder::getEncoderName(encoding), hasMore);
    #endif
    VNCEncoder::clientEncoding(encoding, hasMore);
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

void vncSetPixelFormat(const VNCSetPixFormat &pixFormat) {
    const VNCPixelFormat &format = pixFormat.format;
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char fbDepth = VNC_FB_BITS_PER_PIX;
    #endif
    if (format.trueColor) {
        dprintf("Client requests TrueColor; bitsPerPixel %d; depth %d; big-endian: %d, max %d/%d/%d; shift %d/%d/%d\n",
            format.bitsPerPixel, format.depth, format.bigEndian,
            format.redMax, format.greenMax, format.blueMax,
            format.redShift, format.greenShift, format.blueShift
        );
        memcpy(&pendingPixFormat, &pixFormat.format, sizeof(VNCPixelFormat));
    } else if (format.depth != fbDepth) {
        dprintf("Client requested an incompatible color depth of %d\n", format.depth);
        vncState = VNC_ERROR;
    }
}

void vncKeyEvent(const VNCKeyEvent &keyEvent) {
    if (vncConfig.allowControl) {
        VNCKeyboard::PressKey(keyEvent.key, keyEvent.down);
    }
}

void vncPointerEvent(const VNCPointerEvent &pointerEvent) {
    if (vncConfig.allowControl) {
        //dprintf("Got pointerEvent %d,%d,%d\n", pointerEvent.x, pointerEvent.y, (short) pointerEvent.btnMask);

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

        vncLastMousePosition = newMousePosition;

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
    //dprintf("Got frame request, incremental: %d, Rect: %d,%d,%d,%d\n", fbUpdateReq.incremental, fbUpdateReq.rect.x, fbUpdateReq.rect.y, fbUpdateReq.rect.w, fbUpdateReq.rect.h);
    if(!vncConfig.allowStreaming) return;
    if(vncFlags.fbUpdateInProgress) {
        vncFlags.fbUpdatePending = true;
    } else {
        fbUpdateRect = fbUpdateReq.rect;
        vncSendFBUpdate(vncConfig.allowIncremental && fbUpdateReq.incremental);
    }
}

// Callback for the VBL task
pascal void vncGotDirtyRect(int x, int y, int w, int h) {
    if(vncFlags.fbUpdateInProgress) {
        dprintf("Got dirty rect while busy\n");
        return;
    }
    //dprintf("Got dirty rect");
    if (vncState == VNC_RUNNING) {
        //dprintf("%d,%d,%d,%d\n",x,y,w,h);
        fbUpdateRect.x = x;
        fbUpdateRect.y = y;
        fbUpdateRect.w = w;
        fbUpdateRect.h = h;
        vncPrepareForFBUpdate(&epb_send.pb);
    }
}

void vncSendFBUpdate(Boolean incremental) {
    if(incremental && !(vncFlags.fbColorMapNeedsUpdate || pendingPixFormat.bitsPerPixel)) {
        // Ask the VBL task to determine what needs to be updated
        //dprintf("Requesting dirty rect\n");
        OSErr err = VNCScreenHash::requestDirtyRect(
            fbUpdateRect.x,
            fbUpdateRect.y,
            fbUpdateRect.w,
            fbUpdateRect.h,
            vncGotDirtyRect
        );
        if((err != noErr) && (err != requestAlreadyScheduled)) {
            dprintf("Failed to request update (OSErr:%d)\n", err);
            vncError = err;
        }
    } else {
        vncPrepareForFBUpdate(&epb_send.pb);
    }
}

pascal void vncPrepareForFBUpdate(TCPiopb *pb) {
    fbUpdateStartTicks = TickCount();
    vncFlags.fbUpdateInProgress = true;
    vncFlags.fbUpdatePending = false;

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

    // If a new color palette is available, let the main
    // thread handle it before continuing with the update.
    Boolean needDefer = vncFlags.fbColorMapNeedsUpdate || pendingPixFormat.bitsPerPixel;

    switch (VNCEncoder::begin()) {
        case EncoderOk:
            if (!needDefer) {
                vncSendFBUpdateColorMap(pb);
                break;
            }
            // Intentional fall-thru
        case EncoderDefer:
            runFBSyncedTasks = 1;
            // The idle task will complete initialization tasks and then
            // call vncDeferredDataReady() to resume sending the frame
            break;
        default:
            dprintf("Failed to start encoder\n");
            vncState = VNC_ERROR;
            break;
    }
}

pascal void vncFBSyncTasksDone() {
    vncSendFBUpdateColorMap(&epb_send.pb);
}

pascal void vncSendFBUpdateColorMap(TCPiopb *pb) {
    vncFlags.fbUpdateInProgress = true;
    vncFlags.fbUpdatePending = false;

    if(fbPixFormat.trueColor || !vncFlags.fbColorMapNeedsUpdate) {
        vncFlags.fbColorMapNeedsUpdate = false;
        vncSendFBUpdateHeader(pb);
        return;
    }

    #ifdef VNC_DEBUG
        dprintf("Sending color palette\n");
    #endif
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
    tcp.send(pb, stream, myWDS, kTimeOut,true);
    vncFlags.fbColorMapNeedsUpdate = false;
}

pascal void vncSendFBUpdateHeader(TCPiopb *pb) {
    // Send the header
    myWDS[0].ptr = (Ptr) &vncServerMessage;
    myWDS[0].length = sizeof(VNCFBUpdate);
    myWDS[1].ptr = 0;
    myWDS[1].length = 0;

    vncServerMessage.fbUpdate.message = mFBUpdate;
    vncServerMessage.fbUpdate.padding = 0;
    if(vncFlags.clientTakesCursor && VNCEncodeCursor::needsUpdate()) {
        // If we have a cursor update pending, we send two rects, a
        // pseudo-encoding for the cursor, followed by the screen update
        vncServerMessage.fbUpdate.numRects = 2;
        tcp.then(pb, vncFBUpdateEncodeCursor);
    } else {
        vncServerMessage.fbUpdate.numRects = 1;
        tcp.then(pb, vncStartFBUpdate);
    }
    tcp.send(pb, stream, myWDS, kTimeOut, false);
}

pascal void vncFBUpdateEncodeCursor(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        dprintf("Sending cursor update\n");
        // Add the termination
        myWDS[1].ptr = 0;
        myWDS[1].length = 0;

        // Get cursor data from the encoder
        VNCEncodeCursor::getChunk(myWDS);
        tcp.then(pb, vncStartFBUpdate);
        tcp.send(pb, stream, myWDS, kTimeOut, false);
    }
}

pascal void vncStartFBUpdate(TCPiopb *pb) {
    if (tcpSuccess(pb)) {
        vncServerMessage.fbUpdateRect.rect = fbUpdateRect;
        vncServerMessage.fbUpdateRect.encodingType = VNCEncoder::getEncoding();

        myWDS[0].ptr = (Ptr) &vncServerMessage;
        myWDS[0].length = sizeof(VNCFBUpdateRect);
        myWDS[1].ptr = 0;
        myWDS[1].length = 0;

        tcp.then(pb, vncSendFBUpdateRow);
        tcp.send(pb, stream, myWDS, kTimeOut, false);
    }
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
            tcp.then(pb, vncFinishFBUpdate);
        }
        tcp.send(pb, stream, myWDS, kTimeOut, true);
    }
}

pascal void vncFinishFBUpdate(TCPiopb *pb) {
    float elapsedTime = TickCount() - fbUpdateStartTicks;
    #if LOG_COMPRESSION_STATS
        dprintf("Update done in %.1f s\n", elapsedTime / 60);
    #endif
    vncFlags.fbUpdateInProgress = false;
    if(vncFlags.fbUpdatePending) {
        vncSendFBUpdate(true);
    }

#ifdef POLL_CONNECTION_STATUS
    else {
        tcp.then(pb, vncStatusAvailable);
        tcp.status(pb, stream);
    }
#endif
}

#ifdef POLL_CONNECTION_STATUS
    pascal void vncStatusAvailable(TCPiopb *pb) {
        if (tcpSuccess(pb)) {
            const unsigned char *ip = (unsigned char *)&pb->csParam.status.remoteHost;
            dprintf("Remote IP address: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
            dprintf("Max seg size: %ld\n", pb->csParam.status.sendMaxSegSize);
        }
    }
#endif

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