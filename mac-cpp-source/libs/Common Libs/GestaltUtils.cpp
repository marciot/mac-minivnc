/****************************************************************************
 *   Common Libraries (c) 1994 Marcio Teixeira                              *
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

#include "GestaltUtils.h"

short   hasColorQD  = 0;
long    gSysVers    = 0;

short HasColorQD() {
    OSErr       err;
    long        tmpLong;

    hasColorQD = 0;

    if (!TrapAvailable(_Gestalt)) {
        SysEnvRec mySys;
        err = SysEnvirons( curSysEnvVers, &mySys );
        if( err != envNotPresent )
            hasColorQD = (mySys.hasColorQD) ? 1 : 0;
    } else {
        err = Gestalt(gestaltQuickdrawVersion, &tmpLong);
        if( tmpLong >= gestalt8BitQD )
            hasColorQD = 1;
        if( tmpLong >= gestalt32BitQD )
            hasColorQD = 2;
    }

    return hasColorQD;
}

long GetSysVersion() {
    OSErr err = Gestalt(gestaltSystemVersion, &gSysVers);
    return gSysVers;
}

short GestaltTestAttr( OSType selector, long responseBit ) {
    if (TrapAvailable(_Gestalt)) {
        long tmpLong;
        OSErr err = Gestalt( selector, &tmpLong );
        return err == noErr && BitTst(&tmpLong, 31 - responseBit);
    }
    return false;
}

short GestaltTestMin( OSType selector, long minMask ) {
    if (TrapAvailable(_Gestalt)) {
        long tmpLong;
        OSErr err = Gestalt( selector, &tmpLong );
        if( err != noErr )
            return( FALSE );
        return( tmpLong >= minMask );
    }
    return false;
}

/*Boolean TrapAvailable(short trapWord) {
    TrapType trapType = (trapWord & 0x0800) ? ToolTrap : OSTrap;
    return ((trapType == ToolTrap) &&
        ((trapWord & 0x03FF) >= 0x0200) &&
        (GetToolboxTrapAddress(0xA86E) == GetToolboxTrapAddress(0xAA6E)))
        ? false
        : NGetTrapAddress(trapWord, trapType) != GetToolboxTrapAddress(_Unimplemented);
}*/