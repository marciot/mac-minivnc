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

#include "DebugLog.h"

VNCConfig vncConfig = {
    1,            // Major version
    4,            // Minor version
    true,         // allowStreaming
    true,         // allowIncremental
    true,         // allowControl
    true,         // hideCursor
    true,         // allowRaw
    true,         // allowHexTile
    true,         // allowTRLE
    true,         // allowZRLE
    false,        // autoRestart
    false,        // forceVNCAuth
    false,        // allowTightAuth
    false,        // enableLogging
    5,            // zLibCompression
    "\pMacintosh",// Session name
    5900,         // tcpPort
    'µVNC'        // Validation
};

StringPtr prefFileName = "\pMiniVNC Preferences";

OSErr OpenPreferencesFile(const Str255 fileName, short *resFile);
OSErr OpenPreferencesFile(const Str255 fileName, short *resFile) {
    OSErr   err;
    long    gestaltAnswer;

    *resFile = -1;
    // Try to find the Preferences folder, since we are under System 7,
    // I do not have a fallback.
    err = Gestalt( gestaltFindFolderAttr, &gestaltAnswer );
    if ((err == noErr)  && (gestaltAnswer & (1 << gestaltFindFolderPresent))) {
        err = Gestalt( gestaltFSAttr, &gestaltAnswer );
        if ((err == noErr)  && (gestaltAnswer & (1 << gestaltHasFSSpecCalls))) {
            short   vRefNum;
            long    dirID;
            err = FindFolder( kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &vRefNum, &dirID);
            if (err == noErr) {
                FSSpec fsSpec;
                err = FSMakeFSSpec(vRefNum, dirID, fileName, &fsSpec);
                if(err == fnfErr) {
                    FSpCreateResFile(&fsSpec, 'µVNC', 'pref', smRoman);
                    err = ResError();
                }
                if (err == noErr) {
                    *resFile = FSpOpenResFile(&fsSpec,fsCurPerm);
                    err = ResError();
                }
            }
        }
    }
    return err;
}

OSErr LoadPreferences() {
    const short appResFile = CurResFile();
    OSErr err = ResError();
    if (err == noErr) {
        short prefResFile;
        err = OpenPreferencesFile(prefFileName, &prefResFile);
        if (err == noErr) {
            // Load the preferences object
            const Handle ourPref = GetResource('pref',128);
            if (ourPref && (HomeResFile(ourPref) == prefResFile)) {
                // Validate the configuration object
                if ( (GetHandleSize(ourPref) == sizeof(VNCConfig)) &&
                     (((VNCConfig*)*ourPref)->minorVersion == vncConfig.minorVersion) &&
                     (((VNCConfig*)*ourPref)->majorVersion == vncConfig.majorVersion) &&
                     (((VNCConfig*)*ourPref)->validation   == vncConfig.validation) ) {
                    // Read in the configuration settings
                    BlockMove((Ptr)*ourPref, &vncConfig, sizeof(VNCConfig));
                    dprintf("Loading preferences.\nTo change settings marked with [ResEdit], edit \"%#s\" in ResEdit\n", prefFileName);
                } else {
                    dprintf("Deleting invalid configuration, using defaults\n");
                    // Resource is invalid, remove it
                    RemoveResource(ourPref);
                    // ...also remove the template
                    const Handle ourTmpl = GetResource('TMPL',128);
                    if(ourTmpl && (HomeResFile(ourTmpl) == prefResFile)) {
                        RemoveResource(ourTmpl);
                    } else {
                        ReleaseResource(ourTmpl);
                    }
                }
            } else {
                dprintf("Could not load configuration, using defaults\n");
            }
            ReleaseResource(ourPref);
            CloseResFile(prefResFile);
        } else {
            dprintf("Unable to open preferences file! %d\n",err);
        }
        UseResFile(appResFile);
    }
    return err;
}

OSErr SavePreferences() {
    const short appResFile = CurResFile();
    OSErr err = ResError();
    if (err == noErr) {
        short prefResFile;
        err = OpenPreferencesFile(prefFileName, &prefResFile);
        if (err == noErr) {
            // Try loading the TMPL resource, if it comes from
            // the app's file, the copy it to the preferences
            const Handle ourTmpl = GetResource('TMPL',128);
            if( ourTmpl && (HomeResFile(ourTmpl) == appResFile) ) {
                DetachResource(ourTmpl);
                AddResource(ourTmpl,'TMPL',128,"\ppref");
            } else {
                ReleaseResource(ourTmpl);
            }
            // Load the preferences object
            Handle ourPref = GetResource('pref',128);
            if((ourPref == NULL) || (HomeResFile(ourPref) != prefResFile)) {
                // If we don't have a resource, or we found
                // the wrong one, then create a new resource
                ReleaseResource(ourPref);
                ourPref = NewHandle(sizeof(VNCConfig));
                if (ourPref) {
                    AddResource(ourPref,'pref',128,NULL);
                    err = ResError();
                }
            }
            if((err == noErr) && ourPref && (GetHandleSize(ourPref) == sizeof(VNCConfig))) {
                BlockMove(&vncConfig, *ourPref, sizeof(VNCConfig));
                ChangedResource(ourPref);
            } else {
                dprintf("Unable to update preferences! %d\n",err);
            }
            CloseResFile(prefResFile);
        } else {
            dprintf("Unable to open preferences file! %d\n",err);
        }
        UseResFile(appResFile);
    }
    return err;
}