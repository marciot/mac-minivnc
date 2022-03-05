#ifndef _COMPAT_
#define _COMPAT_

/* compiler ifdef things */

#ifdef THINK_C

#ifndef nil
#define nil                 0L
#endif

#define CR                  '\r'
#define LF                  '\n'
#define CRSTR               "\r"
#define LFSTR               "\n"
#define CRLF                "\r\n"
#define QDARROW             arrow
#define QDTHEPORT           thePort
#define QDSCREENBITS        screenBits
#define QDBLACK             black
#define QDDKGRAY            dkGray
#define okButton            OK
#define cancelButton        Cancel
/*#define   c2pstr              CtoPstr
#define p2cstr              PtoCstr*/

#define PROTOS

#else

#define PROTOS

#define CR                  '\n'
#define LF                  '\r'
#define CRSTR               "\n"
#define LFSTR               "\r"
#define CRLF                "\n\r"
#define QDARROW             qd.arrow
#define QDTHEPORT           qd.thePort
#define QDSCREENBITS        qd.screenBits
#define QDBLACK             qd.black
#define QDDKGRAY            qd.dkGray
#define okButton            ok
#define cancelButton        cancel

#endif

typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned long UInt32;
typedef signed char SInt8;
typedef unsigned char byte;

#endif
