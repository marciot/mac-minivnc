/* (c) Copyright 1994 Ari Halberstadt */

#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
//#include <LoMem.h>

#include "OSUtilities.h"

/*----------------------------------------------------------------------------*/
/* functions for determining features of the operating environment */
/*----------------------------------------------------------------------------*/

/* return number of toolbox traps */
short TrapNumToolbox(void)
{
    short result = 0;

    if (NGetTrapAddress(_InitGraf, ToolTrap) == NGetTrapAddress(0xAA6E, ToolTrap))
        result = 0x0200;
    else
        result = 0x0400;
    return(result);
}

/* return the type of the trap */
TrapType TrapTypeGet(short trap)
{
    return((trap & 0x0800) > 0 ? ToolTrap : OSTrap);
}

/* true if the trap is available  */
Boolean TrapAvailable(short trap)
{
    TrapType type;

    type = TrapTypeGet(trap);
    if (type == ToolTrap) {
        trap &= 0x07FF;
        if (trap >= TrapNumToolbox())
            trap = _Unimplemented;
    }
    return(NGetTrapAddress(trap, type) != NGetTrapAddress(_Unimplemented, ToolTrap));
}

/* true if gestalt trap is available */
Boolean GestaltAvailable(void)
{
    static Boolean initialized, available;

    if (! initialized) {
        available = TrapAvailable(0xA1AD);
        initialized = true;
    }
    return(available);
}

/* return gestalt response, or 0 if error or gestalt not available */
long GestaltResponse(OSType selector)
{
    long response, result;

    response = result = 0;
    if (GestaltAvailable() && Gestalt(selector, &response) == noErr)
        result = response;
    return(result);
}

/* test bit in gestalt response; false if error or gestalt not available */
Boolean GestaltBitTst(OSType selector, short bit)
{
    return((GestaltResponse(selector) & (1 << bit)) != 0);
}

/* true if the WaitNextEvent trap is available */
Boolean MacHasWNE(void)
{
    static Boolean initialized;
    static Boolean wne;

    if (! initialized) {
        /* do only once for efficiency */
        wne = TrapAvailable(_WaitNextEvent);
        initialized = true;
    }
    return(wne);
}

/*----------------------------------------------------------------------------*/
/* event utilities */
/*----------------------------------------------------------------------------*/

/* Call GetNextEvent or WaitNextEvent, depending on which one is available.
    The parameters to this function are identical to those to WaitNextEvent.
    If GetNextEvent is called the extra parameters are ignored. */
Boolean EventGet(short mask, EventRecord *event,
    ThreadTicksType sleep, RgnHandle cursor)
{
    Boolean result = false;

    if (MacHasWNE())
        result = WaitNextEvent(mask, event, sleep, cursor);
    else {
        SystemTask();
        result = GetNextEvent(mask, event);
    }
    if (! result) {
        /* make sure it's a null event, even if the system thinks otherwise, e.g.,
            some desk accessory events (see comment in TransSkell event loop) */
        event->what = nullEvent;
    }
    return(result);
}

/*----------------------------------------------------------------------------*/
/* dialog utilities */
/*----------------------------------------------------------------------------*/

/* set the text of the dialog item */
void SetDText(DialogPtr dlg, short item, const Str255 str)
{
    short type;
    Handle hitem;
    Rect box;

    GetDItem(dlg, item, &type, &hitem, &box);
    SetIText(hitem, str);
}

/* set the text of the dialog item to the number */
void SetDNum(DialogPtr dlg, short item, long num)
{
    Str255 str;

    NumToString(num, str);
    SetDText(dlg, item, str);
}