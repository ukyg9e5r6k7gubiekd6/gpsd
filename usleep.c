#include "gpsd_config.h"

#ifndef HAVE_USLEEP
#ifdef _WIN32
#include "gps.h"
#include <windows.h>

extern int usleep(unsigned int usecs) {
    __int64 time1 = 0, time2 = 0, freq = 0;

    QueryPerformanceCounter((LARGE_INTEGER *) (void *) &time1);
    QueryPerformanceFrequency((LARGE_INTEGER *) (void *) &freq);

    do {
        QueryPerformanceCounter((LARGE_INTEGER *) (void *) &time2);
    } while ((time2 - time1) < usecs);
    return 0;
}
#else /* ndef _WIN32 */
#error "Cannot find a way to perform a microsecond-resolution sleep"
#endif /* ndef HAVE_USLEEP, _WIN32 */
#endif /* ndef HAVE_USLEEP */

#ifndef HAVE_SLEEP
#ifdef _WIN32
extern unsigned int sleep(unsigned int secs)
{
	Sleep(secs * 1000);
	return 0;
}
#else /* ndef _WIN32 */
#error "Cannot find a way to perform a second-resolution sleep"
#endif /* ndef HAVE_SLEEP, _WIN32 */
#endif /* ndef HAVE_SLEEP */
