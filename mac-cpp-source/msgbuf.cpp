#include "msgbuf.h"
#include "VNCConfig.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* msgbuf.cpp allows you to defer a printf from an interrupt handler to a later date in the
   main event loop, where the memory mangaer won't hate you.
   
   Call dprintf() in your interrupt routine, then do_deferred_output in your event loop. */

#ifndef USE_STDOUT
	#define CAPACITY 2
    #define printf ShowStatus
    int ShowStatus(const char* format, ...);
#else
	#define CAPACITY 16
#endif

Str255 blobs[CAPACITY];
int wrpos = 0;
int rdpos = 0;

void dprintf(const char* format, ...) {
	va_list args;
	int w = wrpos;
	
	// Do the printf
	va_start(args, format);
	vsprintf((char*)(blobs[w])+1, format, args);
	va_end(args);
	
	// Forcibly terminate the string, just in case
	blobs[w][255] = '\0';
	
	unsigned int len = strlen((char*)(blobs[w])+1);
	blobs[w][0] = (unsigned char)len;
	
	wrpos = (w+1) % CAPACITY;
	
	if (rdpos == wrpos) {
		// overflow
		if (wrpos == 0) {
			rdpos = CAPACITY - 1;
		} else {
			rdpos = (wrpos - 1) % CAPACITY;
		}
	}
}

void do_deferred_output() {
	int w = wrpos;

	while (rdpos != w) {
		printf((char*)(blobs[rdpos])+1);
	
		rdpos = (rdpos + 1) % CAPACITY;
	}
}