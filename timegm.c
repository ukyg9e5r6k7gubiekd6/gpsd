#include "gpsd_config.h"
#include "gps.h"

#ifndef HAVE_TIMEGM
#include <time.h>
#include <stdlib.h>
time_t timegm(struct tm *tm)
{
    time_t ret;
    char *tz;
    tz = getenv("TZ");
#ifdef HAVE_SETENV
    setenv("TZ", "", 1);
#else /* ndef HAVE_SETENV */
    /* TODO: Thread safety. Can race with getenv */
    putenv("TZ=");
#endif /* ndef HAVE_SETENV */
    tzset();
    ret = mktime(tm);
    if (tz)
#ifdef HAVE_SETENV
        setenv("TZ", tz, 1);
#else /* ndef HAVE_SETENV */
	{
	    static char tmp[1024];
	    sprintf(tmp, "TZ=%.*s", ELEMENTSOF(tmp) - 3 - 1, tz);
	    tmp[ELEMENTSOF(tmp) - 1] = '\0';
	    putenv(tmp);
	}
#endif /* ndef HAVE_SETENV */
    else
#ifdef HAVE_UNSETENV
        unsetenv("TZ");
#else /* ndef HAVE_UNSETENV */
        putenv("TZ=");
#endif /* ndef HAVE_UNSETENV */
    tzset();
    return ret;
}
#endif /* HAVE_TIMEGM */
