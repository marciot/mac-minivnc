// The routines in this file taken more or less unchanged from ChromiVNC Server v3.40 alpha5
// (c) 2005-2005 Jonathan "Chromatix" Morton

// This source file is part of the TridiaVNC package, as originally developed by Jonathan "Chromatix" Morton for Tridia Corporation.

// TridiaVNC is distributed under the GNU General Public Licence (GPL).  You may modify, copy, and/or distribute this program as much
// as you please, provided you remain within the terms of the GPL.  You may use Version 2 of the GPL, or, at your option, any later
// version.  The current version as of 9th October 2000 is reproduced below for your convenience; the most up-to-date version can
// be found on http://www.fsf.org/.  TridiaVNC is Free Software; it has no warranty, not even the normally implied warranties such
// as "fitness for purpose" and so on.

#include "LICENSE.h"

#include <Events.h>
#include <CursorDevices.h>
#include <Script.h>

#include "VNCKeyboard.h"
#include "keysymdef.h"

// From https://opensource.apple.com/source/python/python-3/python/Mac/Lib/Carbon/Events.py.auto.html

#define kNullCharCode 0
#define kHomeCharCode 1
#define kEnterCharCode 3
#define kEndCharCode 4
#define kHelpCharCode 5
#define kBellCharCode 7
#define kBackspaceCharCode 8
#define kTabCharCode 9
#define kLineFeedCharCode 10
#define kVerticalTabCharCode 11
#define kPageUpCharCode 11
#define kFormFeedCharCode 12
#define kPageDownCharCode 12
#define kReturnCharCode 13
#define kFunctionKeyCharCode 16
#define kEscapeCharCode 27
#define kClearCharCode 27
#define kLeftArrowCharCode 28
#define kRightArrowCharCode 29
#define kUpArrowCharCode 30
#define kDownArrowCharCode 31
#define kDeleteCharCode 127
#define kNonBreakingSpaceCharCode 202

enum {
    shiftKeyCode = 0x38,
    controlKeyCode = 0x3B,
    optionKeyCode = 0x3A,
    cmdKeyCode = 0x37
};

// The following macros modify a LowMem global as well as our internal idea of the keyboard.
#define PRESSKEY(_code) { getKeyBuffer[_code / 8] |= (1 << (_code % 8)); ((unsigned char *) 0x174)[_code / 8] |= (1 << (_code % 8)); }
#define RELEASEKEY(_code) { getKeyBuffer[_code / 8] &= ~(1 << (_code % 8)); ((unsigned char *) 0x174)[_code / 8] &= ~(1 << (_code % 8)); }
#define BITKEY(_code) (getKeyBuffer[_code / 8] & (1 << (_code % 8)))

EventModifiers modifiedKeys;
unsigned char getKeyBuffer[16], transMap[256][4];

void VNCKeyboard::Setup() {
    // for the GetKeys patch
    for(int c=0; c < 16; c++)
        getKeyBuffer[c] = 0;
    // for translating keysyms to keycodes
    Ptr theKCHR = (char *) ::GetScriptManagerVariable(smKCHRCache);
    unsigned long state, theChar;
    for(int c=3; c >= 0; c--) {
        for(int d=0; d < 256; d++)
            transMap[d][c] = 0xFF;
        modifiedKeys = ((c & 2) ? optionKey : 0) | ((c & 1) ? shiftKey : 0);
        for(int d=0; d < 127; d++) {
            state = 0;
            theChar = ::KeyTranslate(theKCHR, (d | modifiedKeys), &state) & 0xFF;
            if(theChar != 0 && transMap[theChar][c] == 0xFF)
                transMap[theChar][c] = d;
        }
    }
}

// Simulates pressing or releasing the key represented by keysym, values for which are defined by the X protocol.

void  VNCKeyboard::PressKey(unsigned long keysym, Boolean down) {
    EvQElPtr queueEntry = nil;
    EventRecord ev;
    Boolean modifier = false, onKeyPad = false;
    int modifierMask = 0;
    int theChar = 0, theKeyCode = 0;

    // Prepare the event record
    ev.what = down ? keyDown : keyUp;
    ev.modifiers = modifiedKeys;

    // Translate the X11 keysym into MacRoman encoding
    // Pay particular attention to modifier keys...
    switch(keysym) {
        case XK_Shift_L:
        case XK_Shift_R:
            modifier = true;
            modifierMask = shiftKey;
            theKeyCode = shiftKeyCode;
            break;

        case XK_Control_L:
        case XK_Control_R:
            modifier = true;
            modifierMask = controlKey;
            theKeyCode = controlKeyCode;
            break;

        case XK_Caps_Lock:
            modifier = true;
            modifierMask = alphaLock;
            theKeyCode = 0x39;
            break;

        // The Emacs META key, or the "space cadet" SUPER/HYPER keys, analogous to the Command key
        case XK_Meta_L:
        case XK_Meta_R:
        case XK_Super_L:
        case XK_Super_R:
        case XK_Hyper_L:
        case XK_Hyper_R:
            modifier = true;
            modifierMask = cmdKey;
            theKeyCode = cmdKeyCode;
            break;

        case XK_Alt_L:
        case XK_Alt_R:
            modifier = true;
            modifierMask = optionKey;
            theKeyCode = optionKeyCode;
            break;

        // That's all the modifiers - now test for 'special' keys
        case XK_BackSpace:
            theChar = kBackspaceCharCode;
            break;

        case XK_Tab:
            theChar = kTabCharCode;
            break;

        case XK_Return:
            theChar = kReturnCharCode;
            break;

        case XK_Escape:
            theChar = kEscapeCharCode;
            break;

        case XK_KP_Delete:
            onKeyPad = true;
        case XK_Delete:
            theChar = kDeleteCharCode;
            break;

        case XK_KP_Home:
            onKeyPad = true;
        case XK_Home:
            theChar = kHomeCharCode;
            break;

        case XK_KP_End:
            onKeyPad = true;
        case XK_End:
            theChar = kEndCharCode;
            break;

        case XK_KP_Page_Up:
            onKeyPad = true;
        case XK_Page_Up:
            theChar = kPageUpCharCode;
            break;

        case XK_KP_Page_Down:
            onKeyPad = true;
        case XK_Page_Down:
            theChar = kPageDownCharCode;
            break;

        case XK_KP_Left:
            onKeyPad = true;
        case XK_Left:
            theChar = kLeftArrowCharCode;
            break;

        case XK_KP_Right:
            onKeyPad = true;
        case XK_Right:
            theChar = kRightArrowCharCode;
            break;

        case XK_KP_Up:
            onKeyPad = true;
        case XK_Up:
            theChar = kUpArrowCharCode;
            break;

        case XK_KP_Down:
            onKeyPad = true;
        case XK_Down:
            theChar = kDownArrowCharCode;
            break;

        // The keypad is a strange beast.  It generates a separate set of X11 keysyms and can be interpreted differently by Mac apps.
        case XK_KP_Multiply:
            theChar = '*';
            onKeyPad = true;
            break;

        case XK_KP_Add:
            theChar = '+';
            onKeyPad = true;
            break;

        case XK_KP_Subtract:
            theChar = '-';
            onKeyPad = true;
            break;

        case XK_KP_Decimal:
            theChar = '.';
            onKeyPad = true;
            break;

        case XK_KP_Divide:
            theChar = '/';
            onKeyPad = true;
            break;

        case XK_KP_0:
        case XK_KP_1:
        case XK_KP_2:
        case XK_KP_3:
        case XK_KP_4:
        case XK_KP_5:
        case XK_KP_6:
        case XK_KP_7:
        case XK_KP_8:
        case XK_KP_9:
            theChar = (keysym - XK_KP_0) + '0';
            onKeyPad = true;
            break;

        // Hmmm...  it's not a modifier, and it's not special.  Wonder if it's plain ASCII?
        default:
            if(keysym < 0x20 || keysym > 0x7E) {
                // Nope!  Ignore it, it might do something *very* strange.
                //#warning FIXME!  Implement high-order ASCII translation from Latin-1 to MacRoman
                return;
            }
            // US-ASCII can go in without modification.
            theChar = keysym & 0x7F;
            break;
    }

    if(modifier) {
        if(down) {
            PRESSKEY(theKeyCode);
            modifiedKeys |= modifierMask;
        } else {
            RELEASEKEY(theKeyCode);
            modifiedKeys &= ~modifierMask;
        }
    }

    ev.modifiers = modifiedKeys;

    // If Ctrl+Alt are held down, treat it as a Command key hold.  This works around keyboards
    // which don't have any kind of META key, and brain-dead VNC clients which don't send it even if present.
    if((ev.modifiers & (optionKey | controlKey | cmdKey)) == (optionKey | controlKey)) {
        ev.modifiers &= ~(optionKey | controlKey);
        ev.modifiers |= cmdKey;
    }

    if(modifier) {
        if (ev.modifiers & optionKey)
            PRESSKEY(optionKeyCode)
        else
            RELEASEKEY(optionKeyCode)

        if (ev.modifiers & controlKey)
            PRESSKEY(controlKeyCode)
        else
            RELEASEKEY(controlKeyCode)

        if (ev.modifiers & cmdKey)
            PRESSKEY(cmdKeyCode)
        else
            RELEASEKEY(cmdKeyCode)

        return;
    }

    // Search for the correct KeyCode (plus any additional modifiers) to match the character...
    theKeyCode = 0xFF;
    for(int d=0; d < 3; d++) {
        if(transMap[theChar][d] != 0xFF) {
            theKeyCode = transMap[theChar][d];
            ev.modifiers |= ((d & 2) ? optionKey : 0) | ((d & 1) ? shiftKey : 0);
            break;
        }
    }

    // Didn't find a key to match?  Oops.  Bye!
    if(theKeyCode > 0x7F)
        return;

    // Update the GetKeys map
    if(down)
        getKeyBuffer[theKeyCode / 8] |= (1 << (theKeyCode % 8));
    else
        getKeyBuffer[theKeyCode / 8] &= ~(1 << (theKeyCode % 8));

    // Retranslate the keycode using *all* the modifiers.  This may result in zero, one or two characters...
    Ptr theKCHR = (char *) ::GetScriptManagerVariable(smKCHRCache);
    static unsigned long state = 0;
    short mods = ev.modifiers;
    long result = ::KeyTranslate(theKCHR, theKeyCode | mods, &state);

    // Extract Character #1
    if(result & 0xFF0000) {
        ev.message = ((result >> 16) & 0xFF) | ((int) theKeyCode << 8);
        ::PPostEvent(ev.what, ev.message, &queueEntry);
        if(queueEntry)
            queueEntry->evtQModifiers = mods;
    }

    // Extract Character #2
    if(result & 0xFF) {
        ev.message = (result & 0xFF) | ((int) theKeyCode << 8);
        ::PPostEvent(ev.what, ev.message, &queueEntry);
        if(queueEntry)
            queueEntry->evtQModifiers = mods;
    }

//  The following was producing quite an appreciable slowdown...  we might lose keypresses, however.
//  LThread::Yield();
}

// Overlay the remotely-pressed keys with those already pressed.

void VNCKeyboard::GetKeys(UInt32 *theKeys)
{
    char *outKeys = (char *) theKeys;

    for(int c=0; c < 16; c++)
        outKeys[c] |= getKeyBuffer[c];
}