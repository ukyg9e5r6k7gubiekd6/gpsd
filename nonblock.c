#include "gpsd_config.h"

#ifdef HAVE_FCNTL
#include <fcntl.h>
#endif /* HAVE_FCNTL */
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif /* HAVE_WINSOCK2_H */
#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif /* HAVE_WS2TCPIP_H */
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif /* HAVE_WINDOWS_H */

#include "gpsd.h"

#if defined(HAVE_FCNTL) && defined(F_GETFL) && defined(F_SETFL) && defined(O_NONBLOCK)
/* All appearances are that POSIX non-blocking I/O is present */
extern void nonblock_enable(socket_t s)
{
	int res = fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
	if (res < 0) {
		libgps_debug_trace((DEBUG_CALLS, "fcntl O_NONBLOCK returns error %d\n", res));
	}
}

extern void nonblock_disable(socket_t s)
{
	int res = fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
	if (res < 0) {
		libgps_debug_trace((DEBUG_CALLS, "fcntl ~O_NONBLOCK returns error %d\n", res));
	}
}
#elif defined(_WIN32)
extern void nonblock_enable(socket_t s)
{
	u_long one = 1;
	int res = ioctlsocket(s, FIONBIO, &one);
	if (res != NO_ERROR) {
		libgps_debug_trace((DEBUG_CALLS, "ioctlsocket FIONBIO returns error %d\n", res));
	}
}

extern void nonblock_disable(socket_t s)
{
	u_long zero = 0;
	int res = ioctlsocket(s, FIONBIO, &zero);
	if (res != NO_ERROR) {
		libgps_debug_trace((DEBUG_CALLS, "ioctlsocket !FIONBIO returns error %d\n", res));
	}
}

#else /* ndef HAVE_FCNTL, WIN32 */
#error "Cannot figure out how to get non-blocking I/O on this system"
#endif /* ndef HAVE_FNCTL, _WIN32 */
