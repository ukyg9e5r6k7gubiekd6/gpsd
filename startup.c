#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#endif /* _WIN32 */

#include "gpsd.h"

#ifdef _WIN32
BOOL WINAPI DllMain(
	HINSTANCE lib_handle,
	DWORD reason,
	LPVOID reserved);
/**
 * Initialise socket networking using Windows Sockets.
 * Until this step is performed successfully,
 * Windows applications cannot do most networking.
 */
extern bool init_libgps(void)
{
	WSADATA wsadata;
	/* request access to Windows Sockets API version 2.2 */
	int res = WSAStartup(MAKEWORD(2, 2), &wsadata);
	if (res != 0) {
		libgps_debug_trace((DEBUG_CALLS, "WSAStartup returns error %d\n", res));
	}
	return (0 == res);
}

/**
 * Shutdown Windows Sockets.
 */
extern void shutdown_libgps(void)
{
	int res = WSACleanup();
	if (res != 0) {
		libgps_debug_trace((DEBUG_CALLS, "WSACleanup returns error %d\n", res));
	}
	/* eat errors so we can be used in an atexit call */
}

/**
 * React to being [un]loaded from/to a process' address space.
 * FIXME: Is it legal to call WSAStartup from DllMain?
 * It works for me, but opinions on the Internet vary.
 */
BOOL WINAPI DllMain(
	HINSTANCE lib_handle,
	DWORD reason,
	LPVOID reserved)
{
	(void)lib_handle;
	(void)reserved;
	switch(reason) {
	case DLL_PROCESS_ATTACH:
		return (BOOL) init_libgps();
	case DLL_PROCESS_DETACH:
		shutdown_libgps();
		return TRUE;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	default:
		break; /* ignore */
	}
	return TRUE;
}
#else /* ndef _WIN32 */
/* Provide do-nothing stubs for compatibility */
extern bool init_libgps(void)
{
	return true;
}
extern void shutdown_libgps(void)
{
}
#endif /* ndef _WIN32 */
