/* (c) Copyright 1994 Ari Halberstadt */

typedef long ThreadTicksType;                   /* clock ticks */

Boolean EventGet(short mask, EventRecord *event, ThreadTicksType sleep, RgnHandle cursor);
void SetDText(DialogPtr dlg, short item, const Str255 str);