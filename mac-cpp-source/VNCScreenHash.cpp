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

#include <string.h>

#include "VNCServer.h"
#include "VNCFrameBuffer.h"
#include "VNCScreenHash.h"
#include "msgbuf.h"

#ifdef VNC_BYTES_PER_LINE
    #define COL_HASH_SIZE ((VNC_BYTES_PER_LINE + sizeof(unsigned long) - 1)/sizeof(unsigned long))
    #define ROW_HASH_SIZE (VNC_FB_HEIGHT)
#else
    #define COL_HASH_SIZE ((fbStride + sizeof(unsigned long) - 1)/sizeof(unsigned long))
    #define ROW_HASH_SIZE fbHeight;
#endif

//#define TEST_HASH
//#define HASH_COLUMNS_FIRST

typedef pascal void (*VBLProcPtr)(VBLTaskPtr recPtr);

struct ExtendedVBLTaskRec {
    unsigned long ourA5; // Application A5
    VBLProcPtr ourProc;  // Address of VBL routine written in a high-level language
    VBLTask    vblTask;  // VBLTask record
} evbl;

struct MonoHashData {
    unsigned long     *rowHashPrev;
    unsigned long     *rowHashNext;
    unsigned long     *colHashPrev;
    unsigned long     *colHashNext;
};

#ifdef HASH_COLUMNS_FIRST
    static int col = 0;
#else
    static int row = 0;
#endif

static VNCRect dirtyRect;
static HashCallbackPtr callback;

static MonoHashData *data = NULL;

// Prototypes

void unionRect(const VNCRect *a,VNCRect *b);

OSErr VNCScreenHash::setup() {
    const size_t colHashSize = COL_HASH_SIZE;
    const size_t rowHashSize = ROW_HASH_SIZE;
    const size_t dataSize = sizeof(MonoHashData) + (colHashSize + rowHashSize) * 2 * sizeof(unsigned long);
    data = (MonoHashData*) NewPtr(dataSize);
    if (MemError() != noErr)
        return MemError();

    Ptr hashPtr = (Ptr)data + sizeof(MonoHashData);
    data->rowHashPrev = (unsigned long*)hashPtr;
    data->rowHashNext = data->rowHashPrev + rowHashSize;
    data->colHashPrev = data->rowHashNext + rowHashSize;
    data->colHashNext = data->colHashPrev + colHashSize;

    // Setup the VBL task record
    evbl.ourA5 = SetCurrentA5();
    evbl.ourProc = myVBLTask;
    evbl.vblTask.qType = vType;
    evbl.vblTask.vblAddr = preVBLTask;
    evbl.vblTask.vblCount = 0;
    evbl.vblTask.vblPhase = 0;

    dirtyRect.x = 0;
    dirtyRect.y = 0;
    dirtyRect.w = 0;
    dirtyRect.h = 0;

    callback = 0;

    OSErr err = makeVBLTaskPersistent(&evbl.vblTask);

    // Compute the first checksum
    requestDirtyRect(0, 0, 0, 0, 0);
    return err;
}

OSErr VNCScreenHash::destroy() {
    DisposPtr((Ptr)data);
    data = NULL;

    VRemove((QElemPtr)&evbl.vblTask);
    callback = NULL;
    return disposePersistentVBLTask(&evbl.vblTask);
}

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

void unionRect(const VNCRect *a,VNCRect *b) {
    if(b->w && b->h) {
        if(a->w && a->h) {
            const int ax2 = a->x + a->w, ay2 = a->y + a->h;
            const int bx2 = b->x + b->w, by2 = b->y + b->h;
            const int cx1 = min(a->x,b->x);
            const int cy1 = min(a->y,b->y);
            const int cx2 = max(ax2,bx2);
            const int cy2 = max(ay2,by2);
            b->x = cx1;
            b->y = cy1;
            b->w = cx2 - cx1;
            b->h = cy2 - cy1;
        }
    } else {
        *b = *a;
    }
}

/************************** VBL TASK ************************/

// From Inside Macintosh: Process page 4-20, Using the Vertical Retrace Manager
OSErr VNCScreenHash::makeVBLTaskPersistent(VBLTaskPtr task) {
    struct JMPInstr {
        unsigned short  opcode;
        void            *address;
    } *sysHeapPtr;

    // get a block in the system heap
    sysHeapPtr = (JMPInstr*) NewPtrSys(sizeof(JMPInstr));
    OSErr err = MemError();
    if(err != noErr) return err;

    // populate the instruction
    sysHeapPtr->opcode  = 0x4EF9;       // this is an absolute JMP
    sysHeapPtr->address = task->vblAddr; // this is the JMP address

    task->vblAddr = (VBLUPP) sysHeapPtr;
    return noErr;
}

OSErr VNCScreenHash::disposePersistentVBLTask(VBLTaskPtr task) {
    DisposPtr((Ptr)task->vblAddr);
    task->vblAddr = 0;
    return MemError();
}

asm pascal void VNCScreenHash::preVBLTask() {
    link    a6,#0                // Link for the debugger
    movem.l a5,-(sp)             // Preserve the A5 register
    move.l  a0,-(sp)             // Pass PB pointer as the parameter
    move.l  -8(a0),a5            // Set A5 to passed value (ourA5).
    move.l  -4(a0),a0            // A0 = real completion routine address
    jsr     (a0)                 // Transfer control to ourCompletion
    movem.l (sp)+,a5             // Restore A5 register
    unlk    a6                   // Unlink.
    rts                          // Return
    dc.b    0x8A,"PreVBLTask"
    dc.w    0x0000
}

Boolean VNCScreenHash::requestDirtyRect(int x, int y, int w, int h, HashCallbackPtr func) {
    if(callback == NULL) {
        evbl.vblTask.vblCount = 1;

        callback = func;
        //dirtyRect.x = x;
        //dirtyRect.y = y;
        //dirtyRect.w = w;
        //dirtyRect.h = h;

        #ifdef HASH_COLUMNS_FIRST
            col = 0;
        #else
            row = 0;
        #endif
        beginCompute();

        return VInstall((QElemPtr)&evbl.vblTask);
    }
    return -999;
}

pascal void VNCScreenHash::myVBLTask(VBLTaskPtr theVBL) {
    #if defined(TEST_HASH)
        if(1) {
            beginCompute();
            computeHashesFast(VNC_FB_HEIGHT);
            endCompute();
            beginCompute();
            for(int i = 0; i < (VNC_BYTES_PER_LINE / 16); i++) {
                computeHashesFastest(i);
            }
            endCompute();
            short colChk = memcmp(data->colHashPrev, data->colHashNext, 16);
            short rowChk = memcmp(data->rowHashPrev, data->rowHashNext, ROW_HASH_SIZE);
            dprintf("Col: %s Row: %s ", colChk ? "ne" : "eq", rowChk ? "ne" : "eq");
            callback(0, 0, VNC_FB_WIDTH, VNC_FB_HEIGHT);
            callback = NULL;
        }
    #elif defined(HASH_COLUMNS_FIRST)
        #ifdef VNC_BYTES_PER_LINE
            const unsigned long fbStride = VNC_BYTES_PER_LINE;
        #endif
        if(col < (fbStride / 16)) {
            computeHashesFastest(col++);
            theVBL->vblCount = 1;
        }
    #else
        #ifdef VNC_FB_HEIGHT
            const unsigned int fbHeight = VNC_FB_HEIGHT;
        #endif
        if(row < fbHeight) {
            const unsigned int numRows = min(fbHeight - row, fbHeight/16);
            computeHashesFast(numRows);
            row += numRows;
            theVBL->vblCount = 1;
        }
    #endif
        else  {
            endCompute();
            VNCRect newDirt;
            computeDirty(newDirt.x, newDirt.y, newDirt.w, newDirt.h);

            Boolean gotOldDirt = dirtyRect.w && dirtyRect.h;
            Boolean gotNewDirt = newDirt.w && newDirt.h;

            // Merge the two rectangles
            unionRect(&newDirt, &dirtyRect);

            if(gotOldDirt) {
                // Update and forfeit the rect
                if(callback) {
                    callback(dirtyRect.x, dirtyRect.y, dirtyRect.w, dirtyRect.h);
                    callback = NULL;
                }
                dirtyRect.x = 0;
                dirtyRect.y = 0;
                dirtyRect.w = 0;
                dirtyRect.h = 0;
            } else {
                // Not enough dirt, so keep waiting
                #ifdef HASH_COLUMNS_FIRST
                    col = 0;
                #else
                    row = 0;
                #endif
                beginCompute();
                theVBL->vblCount = 16;
            }
        }
}

/************************** HASHING ************************/

static const unsigned long *scrnPtr;
static unsigned long *scrnRowHashPtr;
static unsigned long *scrnColHashPtr;

void VNCScreenHash::beginCompute() {
    scrnPtr = (unsigned long*) VNCFrameBuffer::getBaseAddr();
    scrnRowHashPtr = data->rowHashNext;
    scrnColHashPtr = data->colHashNext;

    // Clear the next column buffer
    #ifdef HASH_COLUMNS_FIRST
        #ifdef VNC_FB_HEIGHT
            memset(data->rowHashNext, 0, VNC_FB_HEIGHT * sizeof(unsigned long));
        #else
            memset(data->rowHashNext, 0, fbHeight * sizeof(unsigned long));
        #endif
    #else
        memset(data->colHashNext, 0, COL_HASH_SIZE * sizeof(unsigned long));
    #endif
}

void VNCScreenHash::computeDirty(int &x, int &y, int &w, int &h) {
    const size_t colHashSize = COL_HASH_SIZE;
    const size_t rowHashSize = ROW_HASH_SIZE;
    #ifdef VNC_FB_BITS_PER_PIX
        const unsigned char pixPerByte = 8 / VNC_FB_BITS_PER_PIX;
    #else
        const unsigned char pixPerByte = 8 / fbDepth;
    #endif
    unsigned int x1 = 0;
    unsigned int y1 = 0;
    while((x1 < colHashSize) && (data->colHashNext[x1] == data->colHashPrev[x1])) x1++;
    while((y1 < rowHashSize) && (data->rowHashNext[y1] == data->rowHashPrev[y1])) y1++;

    unsigned int x2 = colHashSize-1;
    unsigned int y2 = rowHashSize-1;
    while((x2 > x1) && (data->colHashNext[x2] == data->colHashPrev[x2])) x2--;
    while((y2 > y1) && (data->rowHashNext[y2] == data->rowHashPrev[y2])) y2--;
    x2++;
    y2++;
    x1 *= 4 * pixPerByte;
    x2 *= 4 * pixPerByte;

    if(x2 > x1 && y2 > y1) {
        x = x1;
        y = y1;
        w = x2 - x1;
        h = y2 - y1;
    } else {
        x = 0;
        y = 0;
        w = 0;
        h = 0;
    }
}

// Prepare the hashes so that we can start processing a new screen

void VNCScreenHash::endCompute() {
    // Swap the prev and next buffers
    unsigned long *tmp;
    #define SWAP(A,B) tmp = A; A = B; B = tmp;
    SWAP(data->colHashPrev, data->colHashNext);
    SWAP(data->rowHashPrev, data->rowHashNext);
}

/* This is the C++ implementation of computeHashes(). It was optimized
 * by looking at the disassembly and using temporary variables to try
 * to force the compiler to use register variables inside the loop.
 */
void VNCScreenHash::computeHashes(unsigned int rows) {
    const unsigned long *l = scrnPtr;

    #define PROCESS_CHUNK(col) pix = *l++; rowHash += pix; *colHash++ = *colHash + pix;

    //HideCursor();
    for(;rows--;) {
        unsigned long  rowHash = 0;
        unsigned long *colHash = data->colHashNext;
        unsigned long  pix;
        PROCESS_CHUNK(0);
        PROCESS_CHUNK(1);
        PROCESS_CHUNK(2);
        PROCESS_CHUNK(3);
        PROCESS_CHUNK(4);
        PROCESS_CHUNK(5);
        PROCESS_CHUNK(6);
        PROCESS_CHUNK(7);
        PROCESS_CHUNK(8);
        PROCESS_CHUNK(9);
        PROCESS_CHUNK(10);
        PROCESS_CHUNK(11);
        PROCESS_CHUNK(12);
        PROCESS_CHUNK(13);
        PROCESS_CHUNK(14);
        PROCESS_CHUNK(15);
        #if VNC_FB_WIDTH == 640
            PROCESS_CHUNK(16);
            PROCESS_CHUNK(17);
            PROCESS_CHUNK(18);
            PROCESS_CHUNK(19);
        #endif
        *scrnRowHashPtr++ = rowHash;
    }
    //ShowCursor();
    scrnPtr = l;
}

/* An optimized version of computeHashes() with half as many instruction
 * words, which is estimated to reduce total memory access by 25%. This
 * is done by using movem.l to read either 20 and 24 bytes at a time.
 */
asm void VNCScreenHash::computeHashesFast(unsigned int rows) {
    /*
     * Register Assignments:
     *   A0                     : Source ptr
     *   A1                     : Col hash ptr
     *   A2                     : Row hash ptr
     *   A3                     : Line sum
     *   A4, A5                 : Unused
     *   A6                     : Link for debugger
     *   A7                     : Stack ptr
     *   D0                     : Line count from argument rows
     *   D1,D2,D3,D4,D5,D6      : Source pixels (up to 192 at a time)
     *   D7                     : Unused
     */

    link    a6,#0000           // Link for debugger
    movem.l d3-d6/a2-a3,-(a7)  // Save registers

    //_HideCursor

    move.w  8(a6),d0           // Load the line count

    movea.l scrnPtr,a0
    movea.l scrnRowHashPtr,a2

    bra sumLine
sumLoop:
    movea.l scrnColHashPtr, a1;// Set pointer to column hashes
    movea.l #0,a3              // Clear the line sum

    #if (VNC_FB_WIDTH == 512) && (VNC_FB_BITS_PER_PIX == 1)
        // Use the fewest overall instructions since we are on the 68000 w/o a instruction cache

        // Columns 1 through 160
        movem.l (a0)+,d1-d5        // Load 160 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+

        // Columns 161 through 320
        movem.l (a0)+,d1-d5        // Load 160 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+

        // Columns 321 through 512
        movem.l (a0)+,d1-d6        // Load 192 pixels
        adda.l  d1,a3              // Add to row sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        adda.l  d5,a3
        adda.l  d6,a3
        add.l   d1,(a1)+           // Add to col sum
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+
        add.l   d5,(a1)+
        add.l   d6,(a1)+
    #else
        // We might have differing pixel depths or a resolution of either 512 or 640,
        // so transfer 16 bytes at time as this divides cleanly into all possibilities.

        #ifndef VNC_BYTES_PER_LINE
            move.w fbStride,d5
            asr.w #4,d5
        #else
            move.w #VNC_BYTES_PER_LINE/16, d5
        #endif

        bra transferChunk
    chunkLoop:
        // Transfer 16 bytes at a time using four registers
        movem.l (a0)+,d1-d4        // Load 128 pixels
        adda.l  d1,a3              // Add to line sum
        adda.l  d2,a3
        adda.l  d3,a3
        adda.l  d4,a3
        add.l   d1,(a1)+           // Add to values to column hashes
        add.l   d2,(a1)+
        add.l   d3,(a1)+
        add.l   d4,(a1)+

    transferChunk:
        dbra d5, chunkLoop

        // Transfer remaining bytes that are not divisible by 16 bytes
        // in chunks of two bytes instead.

        #ifndef VNC_BYTES_PER_LINE
            move.w fbStride,d5
            and.w #15,d5
            asr.w #1,d5
        #else
            move.w #(VNC_BYTES_PER_LINE & 15) / 2, d5
        #endif

        bra transferWords
    wordLoop:
        // Transfer two bytes at a time using one register
        move.w (a0)+,d1            // Load 16 pixels
        adda.w  d1,a3              // Add to line sum
        add.w   d1,(a1)+           // Add to values to column hashes

    transferWords:
        dbra d5, wordLoop
    #endif

    move.l a3,(a2)+            // Write the line sum

sumLine:
    dbra d0, sumLoop

    move.l   a0, scrnPtr       // Save updated ptr
    move.l   a2, scrnRowHashPtr

    //_ShowCursor

noRows:
    movem.l (a7)+,d3-d6/a2-a3  // Restore registers
    unlk    a6
    rts                        // Return
}

asm void VNCScreenHash::computeHashesFastest(unsigned int column) {
    /*
     * Register Assignments:
     *   A0                      : Source ptr
     *   A1                      : Row hash ptr
     *   A5                      : Application globals
     *   A6                      : Link for debugger
     *   A7                      : Stack ptr
     *   D0                      : Starting column offset
     *   D1                      : Rows in screen
     *   D2,D3,D4,D5             : Source pixels (up to 128 at a time)
     *   A2,A3,A4,D6             : Column sums
     *   D7                      : Linestride
     */

    link    a6,#0000           // Link for debugger
    movem.l d3-d7/a2-a5,-(a7)  // Save registers

    //_HideCursor

    movea.l scrnPtr,a0
    movea.l scrnRowHashPtr,a1

    // Compute offset to starting column
    move.w  8(a6),d0           // Load the starting column
    lsl.w   #4,d0              // Multiply by 16
    adda.w  d0, a0             // Offset to starting column

    // Compute linestride and rows

    #ifndef VNC_BYTES_PER_LINE
        move.w fbStride,d7
        sub.w #16,d7
    #else
        move.w  #VNC_BYTES_PER_LINE-16,d7
    #endif
    #ifndef VNC_FB_HEIGHT
        move.w  fbHeight,d1
    #else
        move.w  #VNC_FB_HEIGHT,d1
    #endif

    clr.l   d2                 // Clear column sums
    movea.l d2, a2
    movea.l d2, a3
    movea.l d2, a4
    move.l  d2, d6

    bra nextRow
rowLoop:
    movem.l (a0)+,d2-d5        // Load 128 pixels
    adda.l  d7, a0             // Move to next row

    adda.l  d2, a2             // Add to column sums
    adda.l  d3, a3
    adda.l  d4, a4
    add.l   d5, d6

    add.l   d2, d3             // Add to row sum
    add.l   d3, d4
    add.l   d4, d5
    add.l   d5, (a1)+

nextRow:
    dbra d1, rowLoop           // Are we on the last row?

    movea.l scrnColHashPtr,a1  // Set pointer to column hashes
    adda.w  d0, a1             // Offset to starting column
    move.l  a2,(a1)+           // Write column sums
    move.l  a3,(a1)+
    move.l  a4,(a1)+
    move.l  d6,(a1)+

    //_ShowCursor

    movem.l (a7)+,d3-d7/a2-a5  // Restore registers
    unlk    a6
    rts                        // Return
}