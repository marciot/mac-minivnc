#include "DebugLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CAPACITY 2048

/* msgbuf.cpp allows you to defer a printf from an interrupt handler to a later date in the
   main event loop, where the memory manager won't hate you.

   Call dprintf() in your interrupt routine, then do_deferred_output in your event loop. */

char buffer[CAPACITY];
int wrpos = 0;
int rdpos = 0;

// Prints a debugging error message. If the message begins with "-" it will be shown
// on the main VNC user interface.

void _dprintf(const char* format, ...) {
    char str[256];

    // Do the printf
    va_list args;
    va_start(args, format);
    const int len = vsprintf(str, format, args);
    if (len > 255) {
        // Abort if we overflow the buffer, shame CodeWarrior does not have svnprintf!
        abort();
    }
    va_end(args);

    // Copy characters to ring buffer
    for(int i = 0; i <= len; i++) {
        buffer[wrpos] = str[i];
        wrpos = (wrpos+1) % CAPACITY;
    }
}

void _do_deferred_output() {
    char str[256];
    unsigned char len = 0;
    while (rdpos != wrpos) {
        // Pull character from ring buffer
        unsigned char c = buffer[rdpos];
        rdpos = (rdpos+1) % CAPACITY;

        // Output the string
        str[len++] = c;
        if (c == '\0') {
            fputs(str, stdout);
            len = 0;
        }
    }
}