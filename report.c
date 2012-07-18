#include <stdio.h>
#include <stdarg.h>

#include "gpsd.h"

static void gpsd_report_default(int level UNUSED, const char *fmt, va_list argp)
{
    (void)vfprintf(stderr, fmt, argp);
}

static report_callback_fn report_callback = gpsd_report_default;

extern void gpsd_report(int level, const char * fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    report_callback(level, fmt, argp);
    va_end(argp);
}

extern void set_report_callback(report_callback_fn func)
{
    report_callback = func;
}
