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

#include "VNCEncoder.h"
#include "VNCEncodeTRLE.h"
#include "VNCEncodeZRLE.h"

#if !defined(VNC_FB_MONOCHROME)
    #if  VNC_COMPRESSION_LEVEL < 2
        #define UPDATE_BUFFER_SIZE 16L*1024
    #else
        #define UPDATE_BUFFER_SIZE 10L*1024
    #endif

    Size VNCEncodeZRLE::minBufferSize() {
        return UPDATE_BUFFER_SIZE;
    }
#endif