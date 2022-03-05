/************************************************************

    VNCScreenHash.h

       AUTHOR: Marcio Luis Teixeira
       CREATED: 1/16/22

       LAST REVISION: 1/16/22

       (c) 2021 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#pragma once

#include "VNCTypes.h"
#include <Retrace.h>

typedef pascal void (*HashCallbackPtr)(int x, int y, int w, int h);

class VNCScreenHash {
    private:
        static pascal void myVBLTask(VBLTaskPtr theVBL);
        static asm pascal void preVBLTask();
        static OSErr makeVBLTaskPersistent(VBLTaskPtr task);
        static OSErr disposePersistentVBLTask(VBLTaskPtr task);

        static OSErr initVBLTask();
        static OSErr destroyVBLTask();
        static OSErr runVBLTask();

        static void beginCompute();
        static void computeHashes(unsigned int rows);
        static void computeHashesFast(unsigned int rows);
        static void computeHashesFastest(unsigned int rows);
        static void computeDirty(int &x, int &y, int &w, int &h);
        static void endCompute();
    public:
        static OSErr setup();
        static OSErr destroy();
        static Boolean requestDirtyRect(int x, int y, int w, int h, HashCallbackPtr);
};