/************************************************************

    Gestalt.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 9/18/94

       LAST REVISION: 9/18/94

       (c) 1994 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

extern  short   hasColorQD;
extern  long    gSysVers;

short   HasColorQD( void );
long    GetSysVersion( void );
short   GestaltTestAttr( OSType selector, long responseBit );
short   GestaltTestMin( OSType selector, long minMask );
Boolean TrapAvailable( short trapWord );