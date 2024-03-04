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
#include <string.h>
#include <stdarg.h>

#include "DialogUtils.h"

void ShowStatus(const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    const short len = strlen((char*)pStr + 1);
    pStr[0] = pStr[len] == '\n' ? len - 1 : len;
    ShowStatus(pStr);
}

void SetDialogTitle(const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    const short len = strlen((char*)pStr + 1);
    pStr[0] = pStr[len] == '\n' ? len - 1 : len;
    SetDialogTitle(pStr);
}

int ShowAlert(unsigned long type, short id, const char* format, ...) {
    Str255 pStr;
    va_list argptr;
    va_start(argptr, format);
    vsprintf((char*)pStr + 1, format, argptr);
    va_end(argptr);
    pStr[0] = strlen((char*)pStr + 1);
    return ShowAlert(type, id, pStr);
}