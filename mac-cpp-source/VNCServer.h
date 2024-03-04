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

#include "VNCConfig.h"
#include "VNCTypes.h"

enum VNCState {
    VNC_STOPPED,
    VNC_STARTING,
    VNC_WAITING,
    VNC_CONNECTED,
    VNC_RUNNING,
    VNC_STOPPING,
    VNC_ERROR
};

struct VNCFlags {
    unsigned short fbColorMapNeedsUpdate : 1;
    unsigned short fbUpdateInProgress : 1;
    unsigned short fbUpdatePending : 1;
    unsigned short clientTakesRaw : 1;
    unsigned short clientTakesHextile : 1;
    unsigned short clientTakesTRLE : 1;
    unsigned short clientTakesZRLE : 1;
    unsigned short clientTakesCursor : 1;
    unsigned short forceVNCAuth : 1;
    unsigned short zLibLoaded : 1;
};

extern VNCState       vncState;
extern VNCConfig      vncConfig;
extern VNCFlags       vncFlags;
extern Point          vncLastMousePosition;

extern VNCRect        fbUpdateRect;

extern Boolean        runFBSyncedTasks;
extern pascal void    vncFBSyncTasksDone();

OSErr vncServerStart();
OSErr vncServerStop();
OSErr vncServerError();
void vncServerIdleTask();
Boolean vncServerStopped();
Boolean vncServerActive();

// Constants for specialized builds

#if defined(VNC_FB_RES_512_342)
  // Plus, SE, Classic, SE/30
  #define VNC_FB_WIDTH        512
  #define VNC_FB_HEIGHT       342
  #define VNC_FB_MONOCHROME
#elif defined(VNC_FB_RES_512_384)
  // Classic II, all others
  #define VNC_FB_WIDTH        512
  #define VNC_FB_HEIGHT       384

#elif defined(VNC_FB_RES_640_480)
  // Mac II, LC, etc
  #define VNC_FB_WIDTH        640
  #define VNC_FB_HEIGHT       480

#elif defined(VNC_FB_RES_608_431)
  // Apple Lisa w/ Screen Kit
  #define VNC_FB_WIDTH        608
  #define VNC_FB_HEIGHT       431

#elif defined(VNC_FB_RES_720_364)
  // Apple Lisa
  #define VNC_FB_WIDTH        720
  #define VNC_FB_HEIGHT       364

#elif defined(VNC_FB_RES_1024_768)
  #define VNC_FB_WIDTH        1024
  #define VNC_FB_HEIGHT       768
#endif

#if defined(VNC_FB_MONOCHROME)
  #define VNC_FB_BITS_PER_PIX 1

#elif defined(VNC_FB_4_COLORS)
  #define VNC_FB_BITS_PER_PIX 2

#elif defined(VNC_FB_16_COLORS)
  #define VNC_FB_BITS_PER_PIX 4

#elif defined(VNC_FB_256_COLORS)
  #define VNC_FB_BITS_PER_PIX 8
#endif

#define VNC_FB_PIX_PER_BYTE (8 / VNC_FB_BITS_PER_PIX)
#define VNC_FB_PALETTE_SIZE (1 << VNC_FB_BITS_PER_PIX)

#ifdef VNC_FB_WIDTH
    #define VNC_BYTES_PER_LINE (VNC_FB_WIDTH / VNC_FB_PIX_PER_BYTE)
#endif

#define min(A,B) ((A) < (B) ? (A) : (B))
#define max(A,B) ((A) > (B) ? (A) : (B))

#define ZERO_ANY(T, a, n) do{\
   T *a_ = (a);\
   size_t n_ = (n);\
   for (; n_ > 0; --n_, ++a_)\
     *a_ = (T) 0;\
} while (0)

typedef unsigned long size_t;
