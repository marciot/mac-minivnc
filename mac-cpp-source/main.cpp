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

#include <stdio.h>
#include <stdlib.h>
#include <console.h>

#include <stdarg.h>
#include <string.h>

#include <Events.h>

#include "MacTCP.h"
#include "VNCConfig.h"
#include "VNCServer.h"
#include "VNCScreenHash.h"
#include "VNCFrameBuffer.h"
#include "VNCEncoder.h"
#include "OSUtilities.h"
#include "GestaltUtils.h"
#include "msgbuf.h"

#include <SIOUX.h>

enum {
    mApple         = 32000,
    mFile          = 32001,
    mEdit          = 32002,
    mServer        = 128,

    // Items in mFile
    mQuit          = 9,

    // Items in mServer
    mStartServer   = 1,
    mMainWindow    = 3,
    mOptions       = 4,
    mLogs          = 6
};

enum {
    // Controls in gDialog
    iQuit          = 1,
    iStart         = 2,
    iStatus        = 3,

    // Controls in gOptions
    iOkay          = 1,
    iGraphics      = 2,
    iIncremental   = 3,
    iControl       = 4,
    iHideCursor    = 5,
    iAutoRestart   = 6,
    iRaw           = 7,
    iHexTile       = 8,
    iTRLE          = 9,
    iZRLE          = 10
};

Boolean gCancel = false;    /* this is set to true if the user cancels an operation */
DialogPtr gDialog, gOptions;
WindowPtr siouxWindow;
Handle siouxMenuBar, ourMenuBar;

void SetUpSIOUX();
void ShowStatus(const char* format, ...);
void SetDialogTitle(const char* format, ...);
void DoMenuEventPostSIOUX(EventRecord &event);
Boolean DoEvent(EventRecord *event);
void DoMenuSelection(long choice);
ControlHandle FindCHndl(DialogPtr dlg, int item, short *type = NULL);
OSErr StartServer();
Boolean RunningAtStartup();
int ShowAlert(unsigned long type, short id, const char* format, ...);
void UpdateMenuState();
void RefreshServerSettings();
void AdjustCursorVisibility(Boolean allowHiding);
Boolean ToggleWindowVisibility(WindowPtr whatWindow);

main() {
    SetUpSIOUX();

    LoadPreferences();

    // Disable modes that crash the Mac Plus
    if(!HasColorQD()) {
        vncConfig.allowRaw = false;
        vncConfig.allowHextile = false;
        vncConfig.allowZRLE = false;
    }

    /* Create the new dialog */
    gDialog =  GetNewDialog(128, NULL, (WindowPtr) -1);
    gOptions = GetNewDialog(131, NULL, (WindowPtr) -1);

    if(VNCFrameBuffer::checkScreenResolution())
        ShowStatus("Click \"Start Server\" to begin.");

    #ifdef VNC_HEADLESS_MODE
        if(RunningAtStartup()) {
            StartServer();
        }
        vncConfig.autoRestart = 1;
    #else
        vncConfig.autoRestart = 0;
    #endif

    UpdateMenuState();

    /* Run the event loop */
    while (!gCancel || !vncServerStopped()) {
        EventRecord event;
        EventGet(everyEvent, &event, 10, NULL);
        #ifdef USE_STDOUT
            if(!SIOUXHandleOneEvent(&event)) {
                DoEvent(&event);
            } else { // Trap unhandled SIOUX menu events
                DoMenuEventPostSIOUX(event);
            }
        #else
            DoEvent(&event);
        #endif
        switch(vncServerError()) {
            case connectionClosing:
            case connectionTerminated:
                vncServerStop();
                HiliteControl(FindCHndl(gDialog,iStart), 0);
                EnableItem(GetMenuHandle(mServer), mStartServer);
                dprintf("-User disconnected.\n\nClick \"Start Server\" to restart.\n");
                break;
            case noErr:
                VNCFrameBuffer::copy();
                break;
            default:
                ShowStatus("Error %d. Stopping.", vncServerError());
                vncServerStop();
                HiliteControl(FindCHndl(gDialog,iStart), 0);
        }
        AdjustCursorVisibility(true);
        do_deferred_output();
        VNCFrameBuffer::idleTask();
        // Run tasks that need to happen right before a frame buffer update
        if(runFBSyncedTasks) {
            runFBSyncedTasks = false;
            dprintf("\n==== Starting FBSyncTasks ====\n");
            VNCEncoder::fbSyncTasks();
            VNCFrameBuffer::fbSyncTasks();
            dprintf(  "==== FBSyncTasks Finished ====\n\n");
            vncFBSyncTasksDone();
        }

    }

    AdjustCursorVisibility(false);
    DisposeDialog(gDialog);
    gDialog = NULL;

    if(gOptions) {
        DisposeDialog(gOptions);
        gOptions = NULL;
    }

    SavePreferences();

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
                DrawDialog(window);
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
                    ToggleWindowVisibility(window);
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
            switch (itemHit) {
                case iStart:
                    StartServer();
                    break;
                case iQuit:
                    vncServerStop();
                    gCancel = true;
                    break;
            }
        }
        else if (dlgHit == gOptions) {
            short type, value;
            ControlHandle hCntl = FindCHndl(gOptions,itemHit, &type);
            if (type == ctrlItem + chkCtrl) {
                value = 1 - GetControlValue(hCntl);
                SetControlValue(hCntl, value);
            }
            switch (itemHit) {
                case iGraphics:
                    vncConfig.allowStreaming = value;
                    break;
                case iIncremental:
                    vncConfig.allowIncremental = value;
                    break;
                 case iControl:
                    vncConfig.allowControl = value;
                    RefreshServerSettings();
                    break;
                case iHideCursor:
                    vncConfig.hideCursor = value;
                    break;
                case iAutoRestart:
                    vncConfig.autoRestart = value;
                    break;
                case iRaw:
                    vncConfig.allowRaw = value;
                    break;
                case iHexTile:
                    vncConfig.allowHextile = value;
                    break;
                case iTRLE:
                    vncConfig.allowTRLE = value;
                    break;
                case iZRLE:
                    vncConfig.allowZRLE = value;
                    break;
                case iOkay:
                    ToggleWindowVisibility(gOptions);
                    break;
            }
        }
    }
    return(gCancel || stop);
}

void RefreshServerSettings() {
    ControlHandle hHideCursor = FindCHndl(gOptions,iHideCursor);
    if(vncConfig.allowControl) {
        HiliteControl(hHideCursor, 0);
    } else {
        HiliteControl(hHideCursor, 255);
        vncConfig.hideCursor = false;
    }
    SetControlValue(hHideCursor, vncConfig.hideCursor);

    ControlHandle hRaw     = FindCHndl(gOptions,iRaw);
    ControlHandle hHexTile = FindCHndl(gOptions,iHexTile);
    ControlHandle hTRLE    = FindCHndl(gOptions,iTRLE);
    ControlHandle hZRLE    = FindCHndl(gOptions,iZRLE);

    if(!HasColorQD()) {
        HiliteControl  (hRaw,     255);
        HiliteControl  (hHexTile, 255);
        HiliteControl  (hTRLE,    0);
        HiliteControl  (hZRLE,    255);
    } else if(vncServerActive()) {
        HiliteControl  (hRaw,     vncFlags.clientTakesRaw     ? 0 : 255);
        HiliteControl  (hHexTile, vncFlags.clientTakesHextile ? 0 : 255);
        HiliteControl  (hTRLE,    vncFlags.clientTakesTRLE    ? 0 : 255);
        HiliteControl  (hZRLE,    vncFlags.clientTakesZRLE    ? 0 : 255);
    } else {
        HiliteControl  (hRaw,     0);
        HiliteControl  (hHexTile, 0);
        HiliteControl  (hTRLE,    0);
        HiliteControl  (hZRLE,    0);
    }

    if(vncServerActive()) {
        SetControlValue(hRaw,     vncFlags.clientTakesRaw     && vncConfig.allowRaw);
        SetControlValue(hHexTile, vncFlags.clientTakesHextile && vncConfig.allowHextile);
        SetControlValue(hTRLE,    vncFlags.clientTakesTRLE    && vncConfig.allowTRLE);
        SetControlValue(hZRLE,    vncFlags.clientTakesZRLE    && vncConfig.allowZRLE);
    } else {
        SetControlValue(hRaw,     vncConfig.allowRaw);
        SetControlValue(hHexTile, vncConfig.allowHextile);
        SetControlValue(hTRLE,    vncConfig.allowTRLE);
        SetControlValue(hZRLE,    vncConfig.allowZRLE);
    }

    SetControlValue(FindCHndl(gOptions,iGraphics),    vncConfig.allowStreaming);
    SetControlValue(FindCHndl(gOptions,iIncremental), vncConfig.allowIncremental);
    SetControlValue(FindCHndl(gOptions,iControl),     vncConfig.allowControl);
    SetControlValue(FindCHndl(gOptions,iAutoRestart), vncConfig.autoRestart);

    #ifndef VNC_HEADLESS_MODE
        HiliteControl(FindCHndl(gOptions,iAutoRestart), 255);
    #endif
}

void SetUpSIOUX() {
    SIOUXSettings.autocloseonquit = TRUE;
    SIOUXSettings.asktosaveonclose = FALSE;
    SIOUXSettings.standalone = FALSE;
    SIOUXSettings.leftpixel = 8;
    SIOUXSettings.toppixel = 190;
    SIOUXSettings.rows = 10;

    // If MenuChoice is available, we can let SIOUX handle the menus,
    // otherwise we have to handle it ourselves
    SIOUXSettings.setupmenus = TrapAvailable(0xAA66);

    // Force SIOUX to initialize
    printf("Build date: " __DATE__ "\n\n");

    printf("Started MiniVNC user interface\n");

    SIOUXSetTitle("\pServer Logs");
    siouxWindow = FrontWindow();

    // Setup the menu bar
    if(SIOUXSettings.setupmenus) {
        // Add our custom menus right after the SIOUX menus

        MenuHandle ourMenu = GetMenu(128);
        InsertMenu(ourMenu, 0);
        siouxMenuBar = GetMenuBar();

        // Replace the Apple menu
        ourMenuBar = GetNewMBar(128);
        SetMenuBar( ourMenuBar );
        AppendResMenu( GetMenuHandle( mApple ), 'DRVR' );
    } else {
        // Configure the menubar ourselves, SIOUX Edit menu will be non-functional
        siouxMenuBar = ourMenuBar = GetNewMBar(128);
        SetMenuBar( ourMenuBar );
        DrawMenuBar();
        AppendResMenu( GetMenuHandle( mApple ), 'DRVR' );
    }
    DrawMenuBar();
    HideWindow(siouxWindow);
}

void DoMenuEventPostSIOUX(EventRecord &event) {
    if(!SIOUXSettings.setupmenus) return;

    /* If MenuChoice is available, it is best to let SIOUX handle the menu
     * event so Copy and Paste will work. We can check after the fact
     * to see whether the user selected one of our menus using MenuChoice.
     * However, if that trap is not available, we must handle the menu
     * ourselves and certain menu items will not work
     */

    WindowPtr thisWindow;
    if((event.what == mouseDown) && (FindWindow(event.where, &thisWindow) == inMenuBar)) {
        DoMenuSelection(MenuChoice());
    }
}

void DoMenuSelection(long choice) {
    Str255 daName;
    const int        menuId  = HiWord(choice);
    const int        itemNum = LoWord(choice);
    const MenuHandle hMenu   = GetMenuHandle(menuId);

    switch(menuId)  {
        case mApple:
            switch( itemNum ) {
                case 1:
                    if(FrontWindow() != siouxWindow) {
                        ShowAlert(0, 130,
                            "Built on " __DATE__
                            #ifdef VNC_HEADLESS_MODE
                                " for GitHub sponsors"
                            #endif
                            #if defined(VNC_FB_MONOCHROME)
                                " for B&W Macs"
                            #else
                                " for Color Macs (TRLE%d)"
                            #endif
                            , VNC_COMPRESSION_LEVEL
                        );
                    }
                    break;
                default:
                    GetMenuItemText( hMenu, itemNum, daName );
                    OpenDeskAcc( daName );
                    break;
            }
            break;
        case mFile:
            if( itemNum == mQuit ) {
                vncServerStop();
                gCancel = true;
            }
            break;
        case mEdit:
            break;
        case mServer:
            switch( itemNum ) {
                case mStartServer:
                    StartServer();
                    break;
                case mLogs:
                    if(ToggleWindowVisibility(siouxWindow)) {
                        SetMenuBar( siouxMenuBar);
                    } else {
                        SetMenuBar( ourMenuBar );
                    }
                    DrawMenuBar();
                    break;
                case mMainWindow:
                    ToggleWindowVisibility(gDialog);
                    break;
                case mOptions:
                    if(ToggleWindowVisibility(gOptions)) {
                        RefreshServerSettings();
                    }
                    break;
            }
            UpdateMenuState();
            break;
    }
    HiliteMenu(0);
}

Boolean ToggleWindowVisibility(WindowPtr whatWindow) {
    if( ((WindowPeek)whatWindow)->visible) {
        HideWindow( whatWindow );
    } else {
        SelectWindow( whatWindow );
        ShowWindow( whatWindow );
    }
    UpdateMenuState();
    return ((WindowPeek)whatWindow)->visible;
}

void UpdateMenuState() {
    const MenuHandle hMenu = GetMenuHandle(mServer);
    CheckItem( hMenu, mLogs,       ((WindowPeek)siouxWindow)->visible );
    CheckItem( hMenu, mMainWindow, ((WindowPeek)gDialog)->visible );
}

OSErr StartServer() {
    OSErr err = vncServerStart();
    if(err == noErr) {
        HiliteControl(FindCHndl(gDialog,iStart), 255);
        DisableItem(GetMenuHandle(mServer), mStartServer);
    } else {
        ShowStatus("Error starting server %d.", err);
    }
    return err;
}

void AdjustCursorVisibility(Boolean allowHiding) {
    static Boolean hidden = false;

    if(vncConfig.hideCursor && vncServerActive()) {
        // If the user tried to move the mouse, unhide it.
        Point mousePosition;
        GetMouse(&mousePosition);
        if((vncLastMousePosition.h != mousePosition.h) ||
           (vncLastMousePosition.h != mousePosition.h)) {
            allowHiding = false;
        }

        if((!hidden) && allowHiding) {
            HideCursor();
            hidden = true;
        }
    }

    if(hidden && !(allowHiding && vncServerActive())) {
        ShowCursor();
        hidden = false;
    }
}

ControlHandle FindCHndl(DialogPtr dlg, int item, short *type) {
    short tmp, value;
    Rect rect;
    ControlHandle hCntl;
    GetDItem(dlg, item, type ? type : &tmp, (Handle*) &hCntl, &rect);
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