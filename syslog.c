#include "gpsd_config.h"

#ifndef HAVE_SYSLOG_H
#include <stdio.h>
#include <stdarg.h>

#include "gps.h"

extern void openlog(const char *ident UNUSED, int option UNUSED, int facility UNUSED)
{
}

extern void syslog(int priority UNUSED, const char *format, ...)
{
    va_list argptr;
    va_start(argptr, format);
    vfprintf(stderr, format, argptr);
    va_end(argptr);
}

extern void closelog(void)
{
}
#endif /* ndef HAVE_SYSLOG_H */
