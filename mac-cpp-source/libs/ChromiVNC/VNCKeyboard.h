// The routines in this file taken more or less unchanged from ChromiVNC Server v3.40 alpha5
// (c) 2005-2005 Jonathan "Chromatix" Morton

// This source file is part of the TridiaVNC package, as originally developed by Jonathan "Chromatix" Morton for Tridia Corporation.

// TridiaVNC is distributed under the GNU General Public Licence (GPL).  You may modify, copy, and/or distribute this program as much
// as you please, provided you remain within the terms of the GPL.  You may use Version 2 of the GPL, or, at your option, any later
// version.  The current version as of 9th October 2000 is reproduced below for your convenience; the most up-to-date version can
// be found on http://www.fsf.org/.  TridiaVNC is Free Software; it has no warranty, not even the normally implied warranties such
// as "fitness for purpose" and so on.

#include "LICENSE.h"


#include "LICENSE.h"

class VNCKeyboard {
    public:
        static void Setup();
        static void PressKey(unsigned long keysym, Boolean down);
        static void GetKeys(UInt32 *theKeys);
};