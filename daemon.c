#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd_config.h"
#ifndef HAVE_DAEMON
#if defined (HAVE_PATH_H)
#include <paths.h>
#else
#if !defined (_PATH_DEVNULL)
#define _PATH_DEVNULL    "/dev/null"
#endif
#endif

extern int daemon(int nochdir, int noclose)
/* compatible with the daemon(3) found on Linuxes and BSDs */
{
#ifdef _WIN32
	/* TODO: The Windows equivalent of a daemon process is a 'Service' */
	(void)nochdir;
	(void)noclose;
	return -1;
#else /* ndef _WIN32 */
    int fd;

    /*@ -type @*//* weirdly, splint 3.1.2 is confused by fork() */
    switch (fork()) {
    case -1:
	return -1;
    case 0:			/* child side */
	break;
    default:			/* parent side */
	exit(0);
    }
    /*@ +type @*/

    if (setsid() == -1)
	return -1;
    if ((nochdir==0) && (chdir("/") == -1))
	return -1;
    /*@ -nullpass @*/
    if ((noclose==0) && (fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
	(void)dup2(fd, STDIN_FILENO);
	(void)dup2(fd, STDOUT_FILENO);
	(void)dup2(fd, STDERR_FILENO);
	if (fd > 2)
	    (void)close(fd);
    }
    /*@ +nullpass @*/
    /* coverity[leaked_handle] Intentional handle duplication */
    return 0;
#endif /* _WIN32 */
}

#endif /* HAVE_DAEMON */

// end
