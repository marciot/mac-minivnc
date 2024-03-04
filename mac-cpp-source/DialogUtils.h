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

ControlHandle FindCHndl(DialogPtr dlg, int item, short *type);

void ShowStatus(Str255 pStr);
void SetDialogTitle(Str255 pStr);
int ShowAlert(unsigned long type, short id, Str255 pStr);

void ShowStatus(const char* format, ...);
void SetDialogTitle(const char* format, ...);
int ShowAlert(unsigned long type, short id, const char* format, ...);