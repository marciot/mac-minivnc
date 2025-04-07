/**************************************************************************** *   MiniVNC (c) 2022-2024 Marcio Teixeira                                  * *                                                                          * *   This program is free software: you can redistribute it and/or modify   * *   it under the terms of the GNU General Public License as published by   * *   the Free Software Foundation, either version 3 of the License, or      * *   (at your option) any later version.                                    * *                                                                          * *   This program is distributed in the hope that it will be useful,        * *   but WITHOUT ANY WARRANTY; without even the implied warranty of         * *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          * *   GNU General Public License for more details.                           * *                                                                          * *   To view a copy of the GNU General Public License, go to the following  * *   location: <http://www.gnu.org/licenses/>.                              * ****************************************************************************/#include <Files.h>#include "VNCServer.h"#include "VNCEncoder.h"#include "TightVNCSupport.h"#include "DebugLog.h"#if USE_TIGHT_AUTH#define IN_STREAM_COPY(arg)       inStream.copyTo(&arg, sizeof(arg));#define IN_STREAM_PEEK(arg, len) {size_t avail; arg = inStream.getDataBlock(&avail); if(avail < len) {dprintf("Insufficient data! %ld < %ld\n", avail, len);} inStream.skip(len);}#define OUT_STREAM_COPY(type, arg)      *((type*)c)++ = arg;extern "C" {    extern struct TightVNCServerAuthCaps tightAuthCaps;    extern struct TightVNCServerInitCaps tightInitCaps;}pascal void tcpGetTightVncAuthChoice(TCPiopb *pb);pascal void tcpProcessTightVncAuthChoice(TCPiopb *pb);void loadTightSupport() {    dprintf("TightVNC: Loading support code\n");    vncFlags.clientTakesTightAuth = false;}void sendTightCapabilities() {    if (vncFlags.clientTakesTightAuth) {        dprintf("TightVNC: Sending capabilities\n");        myWDS[1].ptr = (Ptr) &tightInitCaps;        myWDS[1].length = sizeof(unsigned short) * 4 + sizeof(TightVNCCapabilites) *            (tightInitCaps.numberOfServerMesg + tightInitCaps.numberOfClientMesg + tightInitCaps.numberOfEncodings);        myWDS[2].ptr = 0;        myWDS[2].length = 0;    }}pascal void tcpSendTightVNCAuthTypes(TCPiopb *pb) {    if (tcpSuccess(pb)) {        vncFlags.clientTakesTightAuth = true;        tcp.then(pb, tcpGetTightVncAuthChoice);        myWDS[0].ptr = (Ptr) &tightAuthCaps;        myWDS[0].length = sizeof(TightVNCServerAuthCaps);        tcp.send(pb, stream, myWDS, kTimeOut, true);    }}pascal void tcpGetTightVncAuthChoice(TCPiopb *pb) {    if (tcpSuccess(pb)) {        tcp.then(pb, tcpProcessTightVncAuthChoice);        tcp.receive(pb, stream, (Ptr) &vncClientMessage, sizeof(TightVNCCapReply));    }}pascal void tcpProcessTightVncAuthChoice(TCPiopb *pb) {    if (tcpSuccess(pb)) {        #ifdef VNC_DEBUG            dprintf("TightVNC: Auth choice: %ld\n", vncClientMessage.tightCapReq.code);        #endif        switch (vncClientMessage.tightCapReq.code) {            case mNoAuthentication:                tcpSendAuthResult(pb);                break;            case mVNCAuthentication:                tcpSendAuthChallenge(pb);                break;        }    }}void tightVNCSendReply(unsigned long message);void tightVNCSendReply(unsigned long message) {    vncServerMessage.tightMessage = message;    tcpSendReply((Ptr)&vncServerMessage, sizeof(vncServerMessage.tightMessage), tcpFinishMultiPartMessage);}// Convert from Mac epoch time (seconds since midnight January 1st, 1904)// to UNIX epoch time (seconds since midnight January 1st, 1970) in milisecondsstatic asm void macToUnixEpoch(uint64*) {    machine 68020    #define tdArg       8(a6)    #define tdPtr         a0    #define tdLo          d0    #define tdHi          d1    #define tmp           d2    link    a6,#0000             // Link for debugger    move.l tdArg,tdPtr    move.l struct(uint64.lo)(tdPtr),tdLo    move.l struct(uint64.hi)(tdPtr),tdHi    subi.l #2082844800,tdLo    moveq           #0,tmp    subx.l         tmp,tdHi    mulu.l #1000,tdHi:tdLo    move.l tdLo, struct(uint64.lo)(tdPtr)    move.l tdHi, struct(uint64.hi)(tdPtr)    unlk    a6    rts    #undef tdArg    #undef tdPtr    #undef tdLo    #undef tdHi    #undef tmp}/** * processRequestPath parses the path in the request and converts it * into a dir specifier (vRefNum + dirId) and a trailing name. * * On input: *    req.pathPtr  = File or directory path *    req.pathLen  = Length of string * * On output: *    req.vRefNum  = Volume reference number *    req.dirId    = Directory id *    req.isRoot   = Is it root? *    trailingName = last name on the path (can be directory or file) */void processRequestPath(TightVNCFileUploadData &req, Str63 trailingName);void processRequestPath(TightVNCFileUploadData &req, Str63 trailingName) {    // The TightVNC client sends a terminating '\0' as part    // of the path, but it is unclear whether all clients do.    // Point pathEnd to the last non-zero char regardless.    const Boolean zeroTerm = req.pathPtr[req.pathLen - 1] == '\0';    const char *pathEnd    = req.pathPtr + req.pathLen - (zeroTerm ? 1 : 0);    const char *namePtr    = req.pathPtr;    req.isRoot = (namePtr[0] == '/') && (pathEnd - namePtr == 1);    req.vRefNum                  = 0;    req.dirId                    = 2;    if (req.isRoot) {        return;    }    // Walk the path, finding ioVRefNum and ioDrDirID as we go along    OSErr myErr = noErr;    CInfoPBRec cpb;    cpb.dirInfo.ioCompletion     = 0;    cpb.dirInfo.ioNamePtr        = trailingName;    cpb.dirInfo.ioVRefNum        = 0;    cpb.dirInfo.ioFDirIndex      = 0;    cpb.dirInfo.ioDrDirID        = 2; // 0 = working dir; 2 = root dir    const char *nameEnd;    do {        // Skip the leading slash        namePtr++;        // Find next slash separator        nameEnd = namePtr;        while ((nameEnd != pathEnd) && (*nameEnd != '/')) {            nameEnd++;        }        // Copy name to trailingName as pascal str        trailingName[0] = nameEnd - namePtr;        BlockMove (namePtr, trailingName + 1, trailingName[0]);        // Are we at the root dir?        if (cpb.dirInfo.ioVRefNum == 0) {            // If yes, append colon to make a volume name            trailingName[0]++;            trailingName[trailingName[0]] = ':';            // Convert volume name into a volume reference number            HParamBlockRec hpb;            hpb.volumeParam.ioCompletion = 0;            hpb.volumeParam.ioNamePtr    = trailingName;            hpb.volumeParam.ioVolIndex   = -1;            hpb.volumeParam.ioVRefNum    = 0;            if (PBHGetVInfo(&hpb, false) != noErr) {                break;            }            req.vRefNum           = hpb.volumeParam.ioVRefNum;            cpb.dirInfo.ioVRefNum = hpb.volumeParam.ioVRefNum;        } else {            if (PBGetCatInfo(&cpb, false) != noErr) {                break;            }            if (cpb.dirInfo.ioFlAttrib & ioDirMask) {                // If we have a directory record the directory id                req.dirId = cpb.dirInfo.ioDrDirID;            }        }        namePtr = nameEnd;    } while (namePtr != pathEnd);    if (myErr != noErr) {        dprintf("processRequestPath error %d\n", myErr);    }}size_t getDirEntries(TightVNCFileUploadData &req, unsigned char *c, const unsigned char *end);pascal void tightVNCFileListContinuation(TCPiopb *pb);void tightVNCFileList(MessageData *pb);void tightVNCFileList(MessageData *pb) {    /**        Message Format:            long           message;            unsigned char  compressionLevel;            unsigned long  dirNameSize;        Followed by char Dirname[dirNameSize]     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    unsigned char  compressionLevel;    IN_STREAM_COPY(message);    IN_STREAM_COPY(compressionLevel);    IN_STREAM_COPY(req.pathLen);    IN_STREAM_PEEK(req.pathPtr, req.pathLen);    dprintf("Got file list request: %.*s\n", (unsigned short) req.pathLen, req.pathPtr);    // Get the GMT conversion factor (this function cannot be called from an interrupt)    MachineLocation loc;    ReadLocation(&loc);    req.gmtDelta = (loc.u.gmtDelta & 0x00FFFFFF) | ((loc.u.gmtDelta & (1L << 23)) ? 0xFF000000 : 0);    OSErr          myErr;    Str63          dirName;    processRequestPath(req, dirName);    // Do a first pass to pre-compute the length of the data    req.index = 0;    const size_t uncompressedLength = sizeof(unsigned long) + getDirEntries(req, 0, 0);    req.nEntries = req.index;    // Make space for the header    unsigned char *c   = fbUpdateBuffer;    unsigned char *end = fbUpdateBuffer + GetPtrSize((Ptr)fbUpdateBuffer);    OUT_STREAM_COPY(long, 0xFC000103);                  // message    OUT_STREAM_COPY(unsigned char, 0);                  // compressionLevel    OUT_STREAM_COPY(unsigned long, uncompressedLength); // compressedSize    OUT_STREAM_COPY(unsigned long, uncompressedLength); // uncompressedSize    OUT_STREAM_COPY(unsigned long, req.nEntries);       // nEntries    req.index = 0;    c += getDirEntries(req, c, end);    vncFlags.fbUpdateInProgress = true;    tcpSendReply((Ptr) fbUpdateBuffer, c - fbUpdateBuffer,        req.index == req.nEntries ? tcpFinishMultiPartMessage : tightVNCFileListContinuation    );}pascal void tightVNCFileListContinuation(TCPiopb *pb) {    if (tcpSuccess(pb)) {        TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;        unsigned char *c   = fbUpdateBuffer;        unsigned char *end = fbUpdateBuffer + GetPtrSize((Ptr)fbUpdateBuffer);        c += getDirEntries(req, c, end);        tcpSendReply((Ptr) fbUpdateBuffer, c - fbUpdateBuffer,            req.index == req.nEntries ? tcpFinishMultiPartMessage : tightVNCFileListContinuation        );    }}size_t getDirEntries(TightVNCFileUploadData &req, unsigned char *c, const unsigned char *end) {    OSErr          myErr;    HParamBlockRec myHPB;    CInfoPBRec     myCPB;    Str63          myName;    size_t         myLen = 0;    for (;;) {        if (req.isRoot) {            // List volumes            myHPB.volumeParam.ioCompletion = 0;            myHPB.volumeParam.ioVRefNum    = 0;            myHPB.volumeParam.ioNamePtr    = myName;            myHPB.volumeParam.ioVolIndex   = req.index + 1;             myErr = PBHGetVInfo(&myHPB, false);        } else {            // List files and folders            myCPB.dirInfo.ioDrDirID     = req.dirId;            myCPB.dirInfo.ioCompletion  = 0;            myCPB.dirInfo.ioNamePtr     = myName;            myCPB.dirInfo.ioFDirIndex   = req.index + 1;            myCPB.dirInfo.ioVRefNum     = req.vRefNum;            myErr = PBGetCatInfo(&myCPB, false);        }        if (myErr != noErr) {            break;        }        const size_t entryLen = sizeof(TightVNCFileListEntry) + myName[0];        if (c == NULL) {            // Do a dry-run without writing any data, only counting bytes            myLen += entryLen;            req.index++;        } else if ((end - c) < entryLen) {            // Stop processing as soon as the buffer is filled            break;        } else {            // Append the file entry to the reply if it fits            const Boolean isDir = req.isRoot || (myCPB.dirInfo.ioFlAttrib & ioDirMask);            const unsigned short flags = isDir ? 1 : 0;            uint64 modTime, fileSize;            modTime.hi = 0;            fileSize.hi = 0;            if (isDir) {                modTime.lo  = myHPB.volumeParam.ioVLsMod - req.gmtDelta;                fileSize.lo = 0;            } else {                modTime.lo  = myCPB.hFileInfo.ioFlMdDat - req.gmtDelta;                fileSize.lo = myCPB.hFileInfo.ioFlLgLen + myCPB.hFileInfo.ioFlRLgLen;            }            macToUnixEpoch(&modTime);            OUT_STREAM_COPY(uint64, fileSize);         // fileSize            OUT_STREAM_COPY(uint64, modTime);          // lastModified            OUT_STREAM_COPY(unsigned short, flags);    // flags            OUT_STREAM_COPY(unsigned long, myName[0]); // dirNameSize            BlockMove(myName + 1, c, myName[0]);            c     += myName[0];            myLen += entryLen;            req.index++;        }    }    return myLen;}void mapFileExtension(StringPtr fileName, OSType *type, OSType *creator);void mapFileExtension(StringPtr fileName, OSType *type, OSType *creator) {    struct Mapping {        OSType type;        OSType creator;    };    // Find the file extension    Str63 extension;    extension[0] = 0;    unsigned char periodAt;    for (periodAt = fileName[0]; (periodAt != 0) && (fileName[periodAt] != '.'); periodAt--);    if (periodAt) {        extension[0] = fileName[0] - periodAt;        BlockMove(fileName + periodAt + 1, extension + 1, extension[0]);    }    // Find a "fmap" resource whose name matches the extension    Handle mapHandle = GetNamedResource('fmap', extension);    if (mapHandle && (*mapHandle)) {        const Mapping mapping = **(Mapping**)mapHandle;        *type    = mapping.type;        *creator = mapping.creator;        ReleaseResource(mapHandle);    } else {        *type    = 'TEXT';        *creator = '????';    }    dprintf("Creating '%#s' as '%.4s'/'%.4s' [ResEdit]\n", fileName, type, creator);}void tightVNCFileUploadStart(MessageData *pb);void tightVNCFileUploadStart(MessageData *pb) {    /**        Message Format:            unsigned long  message;            unsigned long  fNameSize;            const char    *filename; // char filename[fNameSize]            unsigned char  uploadFlags;            uint64         initialOffset;     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    unsigned char  uploadFlags;    uint64         initialOffset;    IN_STREAM_COPY(message);    IN_STREAM_COPY(req.pathLen);    IN_STREAM_PEEK(req.pathPtr, req.pathLen);    IN_STREAM_COPY(uploadFlags);    IN_STREAM_COPY(initialOffset);    dprintf("Got file upload start request: %.*s\n", (unsigned short)req.pathLen, req.pathPtr);    // Open the file for writing    Str63 fileName;    processRequestPath(req, fileName);    OSType type, creator;    mapFileExtension(fileName, &type, &creator);    OSErr err = HCreate(req.vRefNum, req.dirId, fileName, type, creator);    if (err != noErr) {        dprintf("Unable to create file %#p\n", fileName);    }    err = HOpenDF(req.vRefNum, req.dirId, fileName, fsWrPerm, &req.fRefNum);    if (err != noErr) {        dprintf("Unable to open file\n");    }    // Write out the reply    tightVNCSendReply(0xFC000107);}pascal void tightVNCFileUploadDataFragment(TCPiopb *pb);pascal void tightVNCFileUploadBuffersReturned(TCPiopb *pb);pascal void tightVNCFileUploadBuffersFilled(TCPiopb *pb);void tightVNCFileUploadData(MessageData *pb);void tightVNCFileUploadData(MessageData *pb) {    /**        Message Format:            unsigned char  compressionLevel;            size_t         compressedSize;            size_t         uncompressedSize;        Followed by File[compressedSize],            but if (realSize = compressedSize = 0) followed by uint32_t modTime     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    unsigned char  compressionLevel;    IN_STREAM_COPY(message);    IN_STREAM_COPY(compressionLevel);    IN_STREAM_COPY(req.compressedSize);    IN_STREAM_COPY(req.uncompressedSize);    dprintf("Got file data (decompress: %ld => %ld)\n", req.compressedSize, req.uncompressedSize);    tightVNCFileUploadDataFragment(&epb_recv.pb);}pascal void tightVNCFileUploadDataFragment(TCPiopb *pb) {    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    while (req.compressedSize > 0) {        size_t avail;        const char *data = inStream.getDataBlock(&avail);        const size_t bytesRead = min(avail, req.compressedSize);        inStream.skip(bytesRead);        req.compressedSize -= bytesRead;        long bytesWritten = bytesRead;        OSErr err = FSWrite(req.fRefNum, &bytesWritten, data);        if (err != noErr) {            dprintf("Unable to write file data\n");        }        if (inStream.finished()) {            // Return the buffers            tcp.then(pb, tightVNCFileUploadBuffersReturned);            tcp.receiveReturnBuffers(pb);            return;        }    }    // Write out the reply    tightVNCSendReply(0xFC000109);}pascal void tightVNCFileUploadBuffersReturned(TCPiopb *pb) {    if (tcpSuccess(pb)) {        tcp.then(pb, tightVNCFileUploadBuffersFilled);        tcp.receiveNoCopy(pb, stream, myRDS, kNumRDS);    }}pascal void tightVNCFileUploadBuffersFilled(TCPiopb *pb) {    if (tcpSuccess(pb)) {        inStream.setPosition(0);        tightVNCFileUploadDataFragment(pb);    }}void tightVNCFileUploadEnd(MessageData *pb);void tightVNCFileUploadEnd(MessageData *pb) {    /**        Message Format:            long           message;            unsigned short flags;            uint64         lastModified;     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    unsigned short fileFlags;    uint64 modificationTime;    IN_STREAM_COPY(message);    IN_STREAM_COPY(fileFlags);    IN_STREAM_COPY(modificationTime);    dprintf("Got file end\n");    // Close the file    OSErr err = FSClose(req.fRefNum);    // Write out the reply    tightVNCSendReply(0xFC00010B);}void tightVNCMakeDirectory(MessageData *pb);void tightVNCMakeDirectory(MessageData *pb) {    /**        Message Format:            unsigned long  message;            unsigned long  dirNameSize;            const char    *dirName; // char dirname[dirNameSize]     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    IN_STREAM_COPY(message);    IN_STREAM_COPY(req.pathLen);    IN_STREAM_PEEK(req.pathPtr, req.pathLen);    dprintf("Got mkdir request: %.*s\n", (unsigned short)req.pathLen, req.pathPtr);    // Open the file for writing    Str63 dirName;    processRequestPath(req, dirName);    long createDirId;    OSErr err = DirCreate(req.vRefNum, req.dirId, dirName, &createDirId);    if (err != noErr) {        dprintf("Unable to create directory %#p\n", dirName);    }    // Write out the reply    tightVNCSendReply(0xFC000112);}void tightVNCRemoveFile(MessageData *pb);void tightVNCRemoveFile(MessageData *pb) {    /**        Message Format:            unsigned long  message;            unsigned long  fileNameSize;            const char    *fileName; // char fileName[fileNameSize]     */    TightVNCFileUploadData &req = vncClientMessage.tightFileUploadData;    unsigned long  message;    IN_STREAM_COPY(message);    IN_STREAM_COPY(req.pathLen);    IN_STREAM_PEEK(req.pathPtr, req.pathLen);    dprintf("Got remove request: %.*s\n", (unsigned short)req.pathLen, req.pathPtr);    Str63 fileName;    processRequestPath(req, fileName);    long createDirId;    OSErr err = HDelete(req.vRefNum, req.dirId, fileName);    if (err != noErr) {        dprintf("Unable to delete %#p\n", fileName);    }    // Write out the reply    tightVNCSendReply(0xFC000114);}DispatchMsgResult dispatchTightClientMessage(MessageData *pb) {    READ_ALL(tightVncExtMsg);    MAIN_LOOP_ONLY();    switch (pb->msgPtr->tightVncExtMsg) {        case 0xFC000102:            tightVNCFileList(pb);            break;        case 0xFC000106:            tightVNCFileUploadStart(pb);            break;        case 0xFC000108:            tightVNCFileUploadData(pb);            break;        case 0xFC00010A:            tightVNCFileUploadEnd(pb);            break;        case 0xFC000111:            tightVNCMakeDirectory(pb);            break;        case 0xFC000113:            tightVNCRemoveFile(pb);            break;        default:            dprintf("Invalid TightVNC message: %ld\n", pb->msgPtr->tightVncExtMsg);            vncState = VNC_ERROR;            break;    }    return returnToCaller;}#endif // USE_TIGHT_AUTH