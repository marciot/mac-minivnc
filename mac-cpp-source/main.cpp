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
#include "GestaltUtils.h"
#include "msgbuf.h"

#include <SIOUX.h>

enum {
    mApple = 1,
    mFile = 2,
    mEdit = 3
};

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
Boolean DoMenuSelection(long choice);
ControlHandle FindCHndl(int item, short *type = NULL);
OSErr StartServer();
Boolean RunningAtStartup();
void SetUpMenus();
int ShowAlert(unsigned long type, short id, const char* format, ...);

main() {
    InitGraf((Ptr) &qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    FlushEvents(everyEvent, 0);
    InitCursor();
    
    #ifdef USE_STDOUT
    SIOUXSettings.standalone = FALSE;
    SIOUXSettings.setupmenus = FALSE;
    SIOUXSettings.initializeTB = FALSE;
    #endif

    SetUpMenus();

    /* Create the new dialog */
    gDialog = GetNewDialog(128, NULL, (WindowPtr) -1);

    if(VNCFrameBuffer::checkScreenResolution())
        ShowStatus("Click \"Start Server\" to begin.");

    SetControlValue(FindCHndl(iGraphics), sendGraphics);
    SetControlValue(FindCHndl(iIncremental), allowIncremental);
    SetControlValue(FindCHndl(iControl), allowControl);

    #ifdef VNC_HEADLESS_MODE
        SetDialogTitle("GitHub Sponsor Edition");
        if(RunningAtStartup()) {
            StartServer();
        }
    #endif

    /* Run the event loop */
    while (!gCancel || !vncServerStopped()) {
        EventRecord event;
        EventGet(everyEvent, &event, 10, NULL);
        #ifdef USE_STDOUT
            if(!SIOUXHandleOneEvent(&event))
        #endif
        DoEvent(&event);
        do_deferred_output();
        switch(vncServerError()) {
            case connectionClosing:
            case connectionTerminated:
                #ifdef VNC_HEADLESS_MODE
                    vncServerStop();
                    vncServerStart();
                #else
                    vncServerStop();
                    HiliteControl(FindCHndl(iStart), 0);
                    ShowStatus("Connection closed");
                #endif
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

    #ifndef VNC_HEADLESS_MODE
        Alert(129, NULL); // Sponsorship dialog box
    #endif

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
                case inMenuBar:
                    DoMenuSelection(MenuSelect(event->where));
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
                    StartServer();
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

void SetUpMenus() {
    Handle ourMenu = GetNewMBar(128);
    SetMenuBar( ourMenu );
    DrawMenuBar();
    AppendResMenu( GetMenuHandle( mApple ), 'DRVR' );
}

Boolean DoMenuSelection(long choice) {
    Str255 daName;
    Boolean handled = false;
    const int menuId = HiWord(choice);
    const int itemNum = LoWord(choice);
    switch(menuId)  {
        case mApple:
            switch( itemNum ) {
                case 1:
                    ShowAlert(0, 130,
                        #if defined(VNC_FB_MONOCHROME)
                            "B&W for Compact Macs (%s)", __DATE__
                        #else
                            "Color Packing %d (%s)", VNC_COMPRESSION_LEVEL, __DATE__
                        #endif

                    );
                    break;
                default:
                    GetMenuItemText( GetMenuHandle( mApple ), itemNum, daName );
                    OpenDeskAcc( daName );
                    break;
            }
            break;
        case mFile: // File menu
            if (itemNum == 1) { // Quit
                vncServerStop();
                gCancel = true;
            }
            break;
        case mEdit: // Edit menu
            break;
    }
    HiliteMenu(0);
    return handled;
}

OSErr StartServer() {
    OSErr err = vncServerStart();
    if(err == noErr) {
        HiliteControl(FindCHndl(iStart), 255);
    } else {
        ShowStatus("Error starting server %d.", err);
    }
    return err;
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

int ShowAlert(unsigned long type, short id, const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    pStr[0] = strlen((char*)pStr + 1);
    ParamText(pStr, "\p", "\p", "\p");
    switch(type) {
        case 'ERR': return StopAlert(id, NULL);
        case 'YN': return NoteAlert(id, NULL);
        default: return Alert(id, NULL);
    }
    return 0;
}

#ifdef VNC_HEADLESS_MODE
    /**
     * Determines whether we are in the "Startup Items" folder.
     */
    Boolean RunningAtStartup() {
        OSErr           err;
        short           parentVRefNum, startupVRefNum;
        long            parentDirID, startupDirID;

        if (GestaltTestAttr( gestaltFindFolderAttr, gestaltFindFolderPresent)) {
            /* Obtain file handle to my application's resource file */
            const short myRes = CurResFile();

            /* Find my parent directory */
            FCBPBRec param;
            param.ioCompletion  = NULL;
            param.ioNamePtr     = NULL;
            param.ioVRefNum     = 0;
            param.ioRefNum      = myRes;
            param.ioFCBIndx     = 0;

            OSErr err = PBGetFCBInfo( &param, FALSE );
            const short parentVRefNum = param.ioFCBVRefNum;
            const long parentDirID = param.ioFCBParID;

            /* Determine whether we are in the "Startup Items" folder */
            err = FindFolder( kOnSystemDisk, kStartupFolderType, kDontCreateFolder,
                &startupVRefNum, &startupDirID );

            if (err != noErr) return false;

            return startupVRefNum == parentVRefNum &&
                   startupDirID   == startupDirID;
        }
        return false;
    }
#endif