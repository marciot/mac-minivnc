/* (c) Copyright 1994 Ari Halberstadt */

typedef long ThreadTicksType;                   /* clock ticks */

Boolean EventGet(short mask, EventRecord *event, ThreadTicksType sleep, RgnHandle cursor);
void SetDText(DialogPtr dlg, short item, const Str255 str);
void SetDNum(DialogPtr dlg, short item, long num);

short TrapNumToolbox(void);
TrapType TrapTypeGet(short trap);
Boolean TrapAvailable(short trap);
Boolean GestaltAvailable(void);
long GestaltResponse(OSType selector);
Boolean GestaltBitTst(OSType selector, short bit);
Boolean MacHasWNE(void);