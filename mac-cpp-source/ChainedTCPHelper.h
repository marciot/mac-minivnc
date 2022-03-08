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

#include "compat.h"
#include "MacTCP.h"

const long kTimeOut = 120;      /* Timeout for TCP commands */

struct ExtendedTCPiopb;

typedef pascal void (*TCPCompletionPtr)(TCPiopb *epb);

struct ExtendedTCPiopb {
    unsigned long ourA5;            // Application A5
    TCPCompletionPtr ourCompletion; // Address of completion routine written in a high-level language
    TCPiopb pb;                     // Parameter block used to make the call
};

class ChainedTCPHelper {
    public:
        static inline OSErr getResult(TCPiopb *pBlock) {return pBlock->ioResult;}
        static inline void clearError(TCPiopb *pBlock) {pBlock->ioResult = noErr;}
        static inline StreamPtr getStream(TCPiopb *pBlock) {return pBlock->tcpStream;};

        static OSErr begin(TCPiopb *pBlock);

        static void createStream(TCPiopb *pBlock, Ptr recvPtr, unsigned short recvLen);

        static void openConnection(TCPiopb *pBlock, StreamPtr streamPtr, ip_addr remoteHost, tcp_port remotePort, Byte timeout = kTimeOut);
        static void waitForConnection(TCPiopb *pBlock, StreamPtr streamPtr, Byte timeout, tcp_port localPort, ip_addr remoteHost = 0, tcp_port remotePort = 0);
        static void getRemoteHost(TCPiopb *pBlock, ip_addr *remoteHost, tcp_port *remotePort);
        static void getLocalHost(TCPiopb *pBlock, ip_addr *localHost, tcp_port *localPort);

        static void close(TCPiopb *pBlock, StreamPtr streamPtr, Byte timeout = kTimeOut);
        static void abort(TCPiopb *pBlock, StreamPtr streamPtr);
        static void release(TCPiopb *pBlock, StreamPtr streamPtr);

        static void send(TCPiopb *pBlock, StreamPtr streamPtr, wdsEntry data[], Byte timeout = kTimeOut, Boolean push = false, Boolean urgent = false);
        static void receive(TCPiopb *pBlock, StreamPtr streamPtr, Ptr buffer, unsigned short length, Byte timeout = kTimeOut);
        static void getReceiveInfo(TCPiopb *pBlock, unsigned short *rcvLen, Boolean *urgent = 0,Boolean *mark = 0);

        static void receiveNoCopy(TCPiopb *pBlock, StreamPtr streamPtr, rdsEntry data[], unsigned short numRds, Byte timeout = 0);
        static void receiveReturnBuffers(TCPiopb *pBlock);

        static void then(TCPiopb *pBlock, TCPCompletionPtr proc);
};