#include "gpsd_config.h"

#if !defined(HAVE_GMTIME_R) || !defined(HAVE_LOCALTIME_R)
#ifdef _WIN32
#include <sys/time.h>
#include <string.h>
#endif /* _WIN32 */
#include "gps.h"
#endif /* !defined(HAVE_GMTIME_R) || !defined(HAVE_LOCALTIME_R) */

/**
 * Windows has only the variants sans _r suffix.
 * However, the memory to which they return pointers is allocated
 * in thread-local storage, so they are implicitly thread safe.
 * So the naive implementation of the _r variants is actually correct.
 */
#ifndef HAVE_GMTIME_R
#ifdef _WIN32
extern struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	struct tm *tmp = gmtime(timep);
	memcpy(result, tmp, sizeof(*tmp));
	return result;
}
#else /* ndef _WIN32 */
#error "Cannot figure out how on this system to get a broken-down time in a thread safe way"
#endif /* ndef _WIN32 */
#endif /* ndef HAVE_GMTIME_R */

#ifndef HAVE_LOCALTIME_R
#ifdef _WIN32
extern struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	struct tm *tmp = localtime(timep);
	memcpy(result, tmp, sizeof(*tmp));
	return result;
}
#else /* ndef _WIN32 */
#error "Cannot figure out how on this system to get a broken-down local time in a thread safe way"
#endif /* ndef _WIN32 */
#endif /* ndef HAVE_LOCALTIME_R */
