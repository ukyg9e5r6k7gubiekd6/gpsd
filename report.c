/*
 * Stub function, only here becauuse the linker wants to see it even if
 * a client does not actually require it.
 */
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <stdarg.h>

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble report in printf(3) style, use stderr or syslog */
{
    va_list ap;

    va_start(ap, fmt) ;
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

