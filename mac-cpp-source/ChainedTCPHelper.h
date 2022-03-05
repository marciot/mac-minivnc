/************************************************************

    ChainedTCPHelper.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 12/14/21

       LAST REVISION: 12/14/21

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

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