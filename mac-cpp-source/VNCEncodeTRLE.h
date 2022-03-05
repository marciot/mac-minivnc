/************************************************************

    VNCEncodeTRLE.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 2/12/22

       LAST REVISION: 2/12/22

       (c) 2022 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#pragma once

#include "MacTCP.h"

#define TileRaw        0
#define TileSolid      1
#define Tile2Color     2
#define TileReuse    127
#define TilePlainRLE 128

class VNCEncoder {
    public:
        static OSErr setup();
        static OSErr destroy();

        static void begin();
        static Boolean getChunk(int x, int y, int w, int h, wdsEntry *wds);
};

