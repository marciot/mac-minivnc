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

#include <Events.h>

#include "MacTCP.h"
#include "VNCConfig.h"
#include "VNCServer.h"
#include "VNCScreenHash.h"
#include "VNCFrameBuffer.h"
#include "VNCPalette.h"
#include "VNCEncoder.h"
#include "VNCEncodeCursor.h"
#include "OSUtilities.h"
#include "GestaltUtils.h"
#include "DebugLog.h"
#include "DialogUtils.h"

#define DEBUG_SEGMENT_LOAD 0

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
    iEnableLogs    = 7,
    iRaw           = 8,
    iHexTile       = 9,
    iTRLE          = 10,
    iZRLE          = 11
};

Boolean gCancel = false;    /* this is set to true if the user cancels an operation */
DialogPtr gDialog, gOptions;
Handle ourMenuBar;

#if USE_STDOUT
    #include <SIOUX.h>

    WindowPtr siouxWindow;
    Handle siouxMenuBar;
    void SetUpSIOUX();
#endif

void DoMenuEventPostSIOUX(EventRecord &event);
Boolean DoEvent(EventRecord *event);
void DoMenuSelection(long choice);
ControlHandle FindCHndl(DialogPtr dlg, int item, short *type = NULL);
OSErr StartServer();
void CheckServerState();
Boolean RunningAtStartup();
void SetupMenuBar();
void UpdateMenuState();
void RefreshServerSettings();
Boolean ToggleWindowVisibility(WindowPtr whatWindow);

#if DEBUG_SEGMENT_LOAD
    MenuHandle checkLoadedSegments();
#endif

main() {
    // Setup the toolbox ourselves

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nil);
    InitCursor();

    /* Make sure we are running in a compatible resolution */
    if (!VNCFrameBuffer::checkScreenResolution()) {
        ExitToShell();
    }

    /* Load the user interface */
    gDialog =  GetNewDialog(128, NULL, (WindowPtr) -1);
    gOptions = GetNewDialog(131, NULL, (WindowPtr) -1);

    ShowStatus("\pClick \"Start Server\" to begin.");

    /* Load the configuration and disable modes that
     * crash the Mac Plus */

    LoadPreferences();

    if (!HasColorQD()) {
        vncConfig.allowRaw = false;
        vncConfig.allowHextile = false;
        vncConfig.allowZRLE = false;
    }

    #if USE_STDOUT
        if (vncConfig.enableLogging) {
            SetUpSIOUX();
            HideWindow(siouxWindow);
        } else {
            SetupMenuBar();
        }
    #else
        SetupMenuBar();
        vncConfig.enableLogging = false;
    #endif

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
        #if USE_STDOUT
            if (vncConfig.enableLogging) {
                if (!SIOUXHandleOneEvent(&event)) {
                    DoEvent(&event);
                } else { // Trap unhandled SIOUX menu events
                    DoMenuEventPostSIOUX(event);
                }
            } else {
                DoEvent(&event);
            }
        #else
            DoEvent(&event);
        #endif
        CheckServerState();
        VNCEncodeCursor::idleTask();
        VNCFrameBuffer::idleTask();
        VNCPalette::idleTask();
        vncServerIdleTask();
        #if DEBUG_SEGMENT_LOAD
            checkLoadedSegments();
        #endif
        // Run tasks that need to happen right before a frame buffer update
        if(runFBSyncedTasks) {
            runFBSyncedTasks = false;
            dprintf("\n==== Starting FBSyncTasks ====\n");
            Boolean success = (VNCEncoder::fbSyncTasks() == noErr) &&
                              (VNCPalette::fbSyncTasks() == noErr);
            #if DEBUG_SEGMENT_LOAD
                checkLoadedSegments();
            #endif
            dprintf(  "==== FBSyncTasks Finished ====\n\n");
            if (success) {
                vncFBSyncTasksDone();
            } else {
                vncServerStop();
            }
        }
        do_deferred_output();
    }

    VNCEncodeCursor::adjustCursorVisibility(false);

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

void CheckServerState() {
    static VNCState lastState = VNC_STOPPED;
    if (lastState != vncState) {
        lastState = vncState;
        switch (vncState) {
            case VNC_WAITING:
                ShowStatus("\pWaiting for connection");
                break;
            case VNC_RUNNING:
                ShowStatus("\pConnection established!");
                break;
            case VNC_ERROR:
                switch(vncServerError()) {
                    case connectionClosing:
                    case connectionTerminated:
                        vncServerStop();
                        HiliteControl(FindCHndl(gDialog,iStart), 0);
                        EnableItem(GetMenuHandle(mServer), mStartServer);
                        ShowStatus("\pUser disconnected.");
                        break;
                    case noErr:
                        break;
                    default:
                        dprintf("Server error %d. Stopping.", vncServerError());
                        ShowStatus("\pServer error. Stopping.");
                        vncServerStop();
                        HiliteControl(FindCHndl(gDialog,iStart), 0);
                }
        }
    }
}

#if DEBUG_SEGMENT_LOAD
    MenuHandle checkLoadedSegments() {
        static MenuHandle segMenu = 0;
        static Handle *segList = 0;

        if (segMenu == NULL) {
            const int segNum  = Count1Resources ('CODE');
            segMenu = NewMenu (192, "\pSegments");
            segList = (Handle*) NewPtr (segNum * sizeof(Handle));
            SetResLoad(false);
            for (int i = 1; i <= segNum; i++) {
                Handle hndl = Get1IndResource('CODE', i);
                if (hndl) {
                    short id;
                    ResType type;
                    Str255 name;
                    GetResInfo(hndl, &id, &type, name);
                    AppendMenu (segMenu, name);
                } else {
                    AppendMenu (segMenu, "\pNULL");
                }
                segList[i-1] = hndl;
            }
            SetResLoad(true);
        }

        // Check whether the resources are loaded

        if (segList) {
            const int segNum = CountMItems(segMenu);
            for (int i = 1; i <= segNum; i++) {
                const Handle hndl = segList[i-1];
                if (hndl) {
                    short oldMark, newMark;
                    SignedByte state = HGetState(hndl);
                    const Boolean isPurgeable = (state & (1 << 6));
                    const Boolean isFree      = /*(state == -109) ||*/ !!(*hndl == NULL);
                    GetItemMark (segMenu, i, &oldMark);
                    CheckItem (segMenu,   i, !isFree);
                    if (isPurgeable) {
                        SetItemMark (segMenu, i,'×');
                    }
                    GetItemMark (segMenu, i, &newMark);
                    if (oldMark != newMark) {
                        Str255 name;
                        GetMenuItemText(segMenu, i, name);
                        /*if (isPurgeable) {
                            dprintf("Segment \"%#s\" set as purgeable\n", name);
                        } else if (isFree) {
                            dprintf("Segment \"%#s\" freed\n", name);
                        } else {
                            Size size = GetHandleSize(hndl);
                            dprintf("Reserved %ld bytes to load segment \"%#s\"\n", size, name);
                        }*/
                    }
                }
            }
        }
        return segMenu;
    }
#endif

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
                        #if USE_STDOUT
                            if (window == siouxWindow) {
                                SetMenuBar( siouxMenuBar);
                            } else {
                                SetMenuBar( ourMenuBar );
                            }
                        #endif
                    }
                    break;
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
                case iEnableLogs:
                    #if USE_STDOUT
                        vncConfig.enableLogging = value;
                        if(vncConfig.enableLogging) SetUpSIOUX();
                    #endif
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
    #if USE_STDOUT
        SetControlValue(FindCHndl(gOptions,iEnableLogs),  vncConfig.enableLogging);
    #else
        HiliteControl(FindCHndl(gOptions,iEnableLogs), 255);
    #endif

    #ifndef VNC_HEADLESS_MODE
        HiliteControl(FindCHndl(gOptions,iAutoRestart), 255);
    #endif
}

void SetupMenuBar() {
    // Configure the menubar ourselves, SIOUX Edit menu will be non-functional
    ourMenuBar = GetNewMBar(128);
    SetMenuBar( ourMenuBar );
    AppendResMenu( GetMenuHandle( mApple ), 'DRVR' );
    #if DEBUG_SEGMENT_LOAD
        InsertMenu(checkLoadedSegments(), 0);
    #endif
    ReleaseResource(ourMenuBar);
    ourMenuBar = GetMenuBar();
    DrawMenuBar();
}

#if USE_STDOUT
    #include <stdio.h>

    void SetUpSIOUX() {
        // Setup SIOUX defaults
        SIOUXSettings.initializeTB = FALSE;
        SIOUXSettings.autocloseonquit = TRUE;
        SIOUXSettings.asktosaveonclose = FALSE;
        SIOUXSettings.standalone = FALSE;
        SIOUXSettings.leftpixel = 8;
        SIOUXSettings.toppixel = 190;
        SIOUXSettings.rows = 10;
        SIOUXSettings.setupmenus = false;

        if (!siouxWindow) {
            // If MenuChoice is available, we can let SIOUX handle the menus,
            // otherwise we have to handle it ourselves
            SIOUXSettings.setupmenus = TrapAvailable(0xAA66);

            ClearMenuBar();
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

                SetupMenuBar();
            } else {
                SetupMenuBar();
                siouxMenuBar = ourMenuBar;
            }
        }
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
#endif

void DoMenuSelection(long choice) {
    Str255 daName;
    const int        menuId  = HiWord(choice);
    const int        itemNum = LoWord(choice);
    const MenuHandle hMenu   = GetMenuHandle(menuId);

    #define _STRINGIFY(A) #A
    #define STRINGIFY(A) _STRINGIFY(A)

    switch(menuId)  {
        case mApple:
            switch( itemNum ) {
                case 1:
                    #if USE_STDOUT
                        if(FrontWindow() == siouxWindow) {
                            break;
                        }
                    #endif
                    ShowAlert(0, 130,
                        "\pBuilt on " __DATE__
                        #ifdef VNC_HEADLESS_MODE
                            " for GitHub sponsors"
                        #endif
                        #if defined(VNC_FB_MONOCHROME)
                            " for B&W Macs"
                        #else
                            " for Color Macs (TRLE" STRINGIFY(VNC_COMPRESSION_LEVEL) ")"
                        #endif
                    );
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
                    #if USE_STDOUT
                        if (!siouxWindow) {
                            SetUpSIOUX();
                            break;
                        }
                        if (ToggleWindowVisibility(siouxWindow)) {
                            SetMenuBar( siouxMenuBar);
                        } else {
                            SetMenuBar( ourMenuBar );
                        }
                        DrawMenuBar();
                    #endif
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
    CheckItem( hMenu, mMainWindow, ((WindowPeek)gDialog)->visible );
    #if USE_STDOUT
        if (vncConfig.enableLogging) {
            EnableItem(hMenu,  mLogs);
            CheckItem( hMenu,  mLogs, ((WindowPeek)siouxWindow)->visible );
        } else {
            DisableItem(hMenu, mLogs);
        }
    #else
        DisableItem(hMenu, mLogs);
    #endif
}

OSErr StartServer() {
    OSErr err = vncServerStart();
    if(err == noErr) {
        HiliteControl(FindCHndl(gDialog,iStart), 255);
        DisableItem(GetMenuHandle(mServer), mStartServer);
    } else {
        dprintf("Error %d starting server", err);
        ShowStatus("\pError starting server");
    }
    return err;
}

ControlHandle FindCHndl(DialogPtr dlg, int item, short *type) {
    short tmp, value;
    Rect rect;
    ControlHandle hCntl;
    GetDItem(dlg, item, type ? type : &tmp, (Handle*) &hCntl, &rect);
    return hCntl;
}

void ShowStatus(Str255 pStr) {
    SetDText(gDialog, iStatus, pStr);
}

void SetDialogTitle(Str255 pStr) {
    SetWTitle(gDialog, pStr);
}

int ShowAlert(unsigned long type, short id, Str255 pStr) {
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
