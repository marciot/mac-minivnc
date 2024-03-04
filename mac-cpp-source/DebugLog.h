#pragma once

#include "VNCConfig.h"

void _dprintf(const char* format, ...);
void _do_deferred_output();

#define dprintf             if (vncConfig.enableLogging) _dprintf
#define do_deferred_output  if (vncConfig.enableLogging) _do_deferred_output