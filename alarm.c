#include "gpsd.h"

unsigned int my_alarm(unsigned int timeout, alarm_callback callback);

unsigned int my_alarm(unsigned int timeout, alarm_callback callback)
{
#if defined(HAVE_ALARM) && defined(SIGALRM)
    if (callback) {
	(void)signal(SIGALRM, callback);
	(void)alarm(timeout);
    } else {
	(void)signal(SIGALRM, SIGIGN);
    }
#elif defined(_WIN32)
    if (callback) {
	(void)SetTimer(NULL, 0, 1000 * timeout, callback);
    } else {
	(void)KillTimer(NULL, 0);
    }
#else /* ndef HAVE_ALARM && !defined(_WIN32) */
#error "I cannot figure out how on this system to set an alarm timeout"
#endif /* ndef _WIN32 */
    /* TODO: propogate errors */
    return 0;
}
