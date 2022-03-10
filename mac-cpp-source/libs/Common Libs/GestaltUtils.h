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

extern  short   hasColorQD;
extern  long    gSysVers;

short   HasColorQD( void );
long    GetSysVersion( void );
short   GestaltTestAttr( OSType selector, long responseBit );
short   GestaltTestMin( OSType selector, long minMask );
Boolean TrapAvailable( short trapWord );