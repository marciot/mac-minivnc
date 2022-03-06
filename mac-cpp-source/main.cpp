/************************************************************

    main.cpp

       AUTHOR: Marcio Luis Teixeira
       CREATED: 2/9/22

       LAST REVISION: 2/9/22

       (c) 2022 by Marcio Luis Teixeira.
       All rights reserved.

*************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <console.h>

#include <stdarg.h>
#include <string.h>

#include <Events.h>

#include "MacTCP.h"
#include "VNCServer.h"
#include "VNCScreenHash.h"
#include "VNCFrameBuffer.h"
#include "OSUtilities.h"

#include <SIOUX.h>

enum {
    iQuit  = 1,
    iStart = 2,
    iStatus = 3,
    iGraphics = 4,
    iIncremental = 5,
    iControl = 6
};

Boolean gCancel = false;    /* this is set to true if the user cancels an operation */
DialogPtr gDialog;

void ShowStatus(const char* format, ...);
void SetDialogTitle(const char* format, ...);
Boolean DoEvent(EventRecord *event);
ControlHandle FindCHndl(int item, short *type = NULL);

main() {
    #ifndef USE_STDOUT
        InitGraf((Ptr) &qd.thePort);
        InitFonts();
        InitWindows();
        InitMenus();
        TEInit();
        InitDialogs(0);
        FlushEvents(everyEvent, 0);
        InitCursor();
    #endif

    /* Create the new dialog */
    gDialog = GetNewDialog(128, NULL, (WindowPtr) -1);
    #if defined(VNC_FB_MONOCHROME)
        SetDialogTitle("%s (B&W)", __DATE__);
    #else
        SetDialogTitle("%s (Pack %d)", __DATE__, COMPRESSION_LEVEL);
    #endif

    if(VNCFrameBuffer::checkScreenResolution())
        ShowStatus("Click \"Start Server\" to begin.");

    SetControlValue(FindCHndl(iGraphics), sendGraphics);
    SetControlValue(FindCHndl(iIncremental), allowIncremental);
    SetControlValue(FindCHndl(iControl), allowControl);

    /* Run the event loop */
    while (!gCancel || !vncServerStopped()) {
        EventRecord event;
        EventGet(everyEvent, &event, 10, NULL);
        #ifdef USE_STDOUT
            if(!SIOUXHandleOneEvent(&event))
        #endif
        DoEvent(&event);
        switch(vncServerError()) {
            case connectionClosing:
            case connectionTerminated:
                vncServerStop();
                vncServerStart();
                break;
            case noErr:
                VNCFrameBuffer::copy();
                break;
            default:
                ShowStatus("Error %d. Stopping.", vncServerError());
                vncServerStop();
                HiliteControl(FindCHndl(iStart), 0);
        }
    }

    DisposeDialog(gDialog);
    gDialog = NULL;

    Alert(129, NULL); // Sponsorship dialog box

    return 0;
}

Boolean DoEvent(EventRecord *event) {
    OSErr err;
    WindowPtr window;   /* for handling window events */
    DialogPtr dlgHit;   /* dialog for which event was generated */
    short itemHit;      /* item selected from dialog */
    Rect dragRect;      /* rectangle in which to drag windows */
    Boolean stop;       /* set to true when stop or quit buttons are clicked */

    stop = false;
    switch (event->what) {
        case updateEvt:
            window = (WindowPtr) event->message;
            BeginUpdate(window);
            if (((WindowPeek) window)->windowKind == dialogKind) {
                DrawDialog(gDialog);
                event->what = nullEvent;
            }
            EndUpdate(window);
            break;
        case mouseDown:
            switch (FindWindow(event->where, &window)) {
                case inDrag:
                    dragRect = (**GetGrayRgn()).rgnBBox;
                    DragWindow(window, event->where, &dragRect);
                    break;
                case inSysWindow:
                    SystemClick(event, window);
                    break;
                case inGoAway:
                    vncServerStop();
                    gCancel = true;
                    break;
                case inContent:
                    if (window != FrontWindow()) {
                        SelectWindow(window);
                        event->what = nullEvent;
                    }
            }
            break;
        case nullEvent:
            break;
    }

    /* handle a dialog event */
    if (IsDialogEvent(event) && DialogSelect(event, &dlgHit, &itemHit)) {

        /* handle a click in one of the dialog's buttons */
        if (dlgHit == gDialog) {
            short type, value;
            ControlHandle hCntl = FindCHndl(itemHit, &type);
            if (type == ctrlItem + chkCtrl) {
                value = 1 - GetControlValue(hCntl);
                SetControlValue(hCntl, value);
            }
            switch (itemHit) {
                case iStart:
                    err = vncServerStart();
                    if(err == noErr) {
                        HiliteControl(FindCHndl(itemHit), 255);
                    } else {
                        ShowStatus("Error starting server %d.", err);
                    }
                    break;
                case iGraphics:
                    sendGraphics = value;
                    break;
                case iIncremental:
                    allowIncremental = value;
                    break;
                case iControl:
                    allowControl = value;
                    break;
                case iQuit:
                    vncServerStop();
                    gCancel = true;
                    break;
            }
        }
    }
    return(gCancel || stop);
}

ControlHandle FindCHndl(int item, short *type) {
    short tmp, value;
    Rect rect;
    ControlHandle hCntl;
    GetDItem(gDialog, item, type ? type : &tmp, (Handle*) &hCntl, &rect);
    return hCntl;
}

void ShowStatus(const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    const short len = strlen((char*)pStr + 1);
    pStr[0] = pStr[len] == '\n' ? len - 1 : len;
    SetDText(gDialog, iStatus, pStr);
}

void SetDialogTitle(const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    const short len = strlen((char*)pStr + 1);
    pStr[0] = pStr[len] == '\n' ? len - 1 : len;
    SetWTitle(gDialog, pStr);
}
