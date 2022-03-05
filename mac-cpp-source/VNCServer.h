/************************************************************

    VNCServer.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 12/14/21

       LAST REVISION: 12/14/21

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

//#define USE_STDOUT

extern Boolean allowControl, sendGraphics, allowIncremental, fbColorMapNeedsUpdate;

OSErr vncServerStart();
OSErr vncServerStop();
OSErr vncServerError();
Boolean vncServerStopped();