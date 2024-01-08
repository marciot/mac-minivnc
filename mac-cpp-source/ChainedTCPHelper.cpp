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

#include "ChainedTCPHelper.h"

/* Opens the TCP driver and allocates a parameter block for
 * subsequent calls.
 */
OSErr ChainedTCPHelper::begin(TCPiopb *pBlock) {
    // Open the TCP driver

    ParamBlockRec param;
    param.ioParam.ioNamePtr = "\p.IPP";
    param.ioParam.ioPermssn = fsCurPerm;
    OSErr err = PBOpen(&param, false);
    if(err != noErr) return err;

    //pBlock->ioCompletion = 0L;
    pBlock->ioCRefNum = param.ioParam.ioRefNum;
    pBlock->ioResult = 0;

    return err;
}

/* Requests a new TCP stream in preparation for initiating a connection.
 * A buffer must be provided for storing incoming data waiting to be processed.
 * Once isReady() is true call getStream() to retreive the stream pointer.
 */
void ChainedTCPHelper::createStream(TCPiopb *pBlock, Ptr recvPtr, unsigned short recvLen) {
    pBlock->csCode = TCPCreate;
    pBlock->ioResult = 1;
    pBlock->csParam.create.rcvBuff = recvPtr;
    pBlock->csParam.create.rcvBuffLen = recvLen;
    pBlock->csParam.create.notifyProc = nil;
    PBControl((ParmBlkPtr)pBlock,true);
}

/* Attempts to initiate a connection with a host specified by host and port.
 */
void ChainedTCPHelper::openConnection(TCPiopb *pBlock, StreamPtr streamPtr, ip_addr remoteHost, tcp_port remotePort, Byte timeout) {
    pBlock->csCode = TCPActiveOpen;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.open.ulpTimeoutValue = timeout;
    pBlock->csParam.open.ulpTimeoutAction = 1;
    pBlock->csParam.open.validityFlags = 0xC0;
    pBlock->csParam.open.commandTimeoutValue = timeout;
    pBlock->csParam.open.remoteHost = remoteHost;
    pBlock->csParam.open.remotePort = remotePort;
    pBlock->csParam.open.localPort = 0;
    pBlock->csParam.open.tosFlags = 0;
    pBlock->csParam.open.precedence = 0;
    pBlock->csParam.open.dontFrag = 0;
    pBlock->csParam.open.timeToLive = 0;
    pBlock->csParam.open.security = 0;
    pBlock->csParam.open.optionCnt = 0;
    PBControl((ParmBlkPtr)pBlock,true);
}

/* Waits for a connection to be opened on a specified port. If remoteHost and remotePort are given
 * it will only accept connections from that host and port, either value can be zero.
 */
void ChainedTCPHelper::waitForConnection(TCPiopb *pBlock, StreamPtr streamPtr, Byte timeout, tcp_port localPort, ip_addr remoteHost, tcp_port remotePort) {
    pBlock->csCode = TCPPassiveOpen;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.open.ulpTimeoutValue = timeout;
    pBlock->csParam.open.ulpTimeoutAction = 1;
    pBlock->csParam.open.validityFlags = 0xC0;
    pBlock->csParam.open.commandTimeoutValue = timeout;
    pBlock->csParam.open.remoteHost = remoteHost;
    pBlock->csParam.open.remotePort = remotePort;
    pBlock->csParam.open.localPort = localPort;
    pBlock->csParam.open.tosFlags = 0;
    pBlock->csParam.open.precedence = 0;
    pBlock->csParam.open.dontFrag = 0;
    pBlock->csParam.open.timeToLive = 0;
    pBlock->csParam.open.security = 0;
    pBlock->csParam.open.optionCnt = 0;
    PBControl((ParmBlkPtr)pBlock,true);
}

/* Returns information about the most recent completed call to waitForConnection() or openConnection()
 */
void ChainedTCPHelper::getRemoteHost(TCPiopb *pBlock, ip_addr *remoteHost, tcp_port *remotePort) {
    *remoteHost = pBlock->csParam.open.remoteHost;
    *remotePort = pBlock->csParam.open.remotePort;
}

void ChainedTCPHelper::getLocalHost(TCPiopb *pBlock, ip_addr *localHost, tcp_port *localPort) {
    *localHost = pBlock->csParam.open.localHost;
    *localPort = pBlock->csParam.open.localPort;
}

/* Gracefully closes a connection with a remote host.  This is not always possible,
   and the programmer might have to resort to abort(), described next. */

void ChainedTCPHelper::close(TCPiopb *pBlock, StreamPtr streamPtr, Byte timeout) {
    pBlock->csCode = TCPClose;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.close.ulpTimeoutValue = timeout;
    pBlock->csParam.close.validityFlags = 0xC0;
    pBlock->csParam.close.ulpTimeoutAction = 1;
    PBControl((ParmBlkPtr)pBlock,true);
}

/* Should be called if a close() fails to close a connection properly.
   This call should not normally be used to terminate connections. */

void ChainedTCPHelper::abort(TCPiopb *pBlock, StreamPtr streamPtr) {
    pBlock->csCode = TCPAbort;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    PBControl((ParmBlkPtr)pBlock,true);
}

/* ReleaseStream() frees the allocated buffer space for a given connection
   stream.  This call should be made after close(). */

void ChainedTCPHelper::release(TCPiopb *pBlock, StreamPtr streamPtr) {
    pBlock->csCode = TCPRelease;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::send(TCPiopb *pBlock, StreamPtr streamPtr, wdsEntry data[], Byte timeout, Boolean push, Boolean urgent) {
    pBlock->csCode = TCPSend;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.send.ulpTimeoutValue = timeout;
    pBlock->csParam.send.ulpTimeoutAction = 1;
    pBlock->csParam.send.validityFlags = 0xC0;
    pBlock->csParam.send.pushFlag = push;
    pBlock->csParam.send.urgentFlag = urgent;
    pBlock->csParam.send.wdsPtr = (Ptr) data;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::status(TCPiopb *pBlock, StreamPtr streamPtr) {
    pBlock->csCode = TCPStatus;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::receiveNoCopy(TCPiopb *pBlock, StreamPtr streamPtr, rdsEntry data[], unsigned short numRds, Byte timeout) {
    pBlock->csCode = TCPNoCopyRcv;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.receive.commandTimeoutValue = timeout;
    pBlock->csParam.receive.rdsPtr = (Ptr) data;
    pBlock->csParam.receive.rdsLength = numRds;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::receiveReturnBuffers(TCPiopb *pBlock) {
    pBlock->csCode = TCPRcvBfrReturn;
    pBlock->ioResult = 1;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::receive(TCPiopb *pBlock, StreamPtr streamPtr, Ptr buffer, unsigned short length, Byte timeout) {
    pBlock->csCode = TCPRcv;
    pBlock->ioResult = 1;
    pBlock->tcpStream = streamPtr;
    pBlock->csParam.receive.commandTimeoutValue = timeout;
    pBlock->csParam.receive.rcvBuff = buffer;
    pBlock->csParam.receive.rcvBuffLen = length;
    PBControl((ParmBlkPtr)pBlock,true);
}

void ChainedTCPHelper::getReceiveInfo(TCPiopb *pBlock, unsigned short *rcvLen, Boolean *urgent, Boolean *mark) {
    if(rcvLen) *rcvLen = pBlock->csParam.receive.rcvBuffLen;
    if(urgent) *urgent = pBlock->csParam.receive.urgentFlag;
    if(mark)   *mark = pBlock->csParam.receive.markFlag;
}

void ChainedTCPHelper::then(TCPiopb *pBlock, TCPCompletionPtr proc) {
    ExtendedTCPiopb *epb = (ExtendedTCPiopb *)((char*)pBlock - 8);
    epb->ourCompletion = proc;
}