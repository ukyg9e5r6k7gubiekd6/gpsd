/* gpsdctl.c -- communicate with the control socket of a gpsd instance
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef S_SPLINT_S
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifndef INADDR_ANY
#include <netinet/in.h>
#endif /* INADDR_ANY */
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <syslog.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "gpsd.h"	/* only for the strlcpy() prototype */

static char *control_socket = "/var/run/gpsd.sock"; 
static char *gpsd_options = "";

static int gpsd_control_connect(bool complain)
/* acquire a connection to the gpsd control socket */
{
    int sock;

    if (access(control_socket, F_OK) != 0) {
	(void)syslog(LOG_ERR, "socket %s doesn't exist", control_socket);
	return -1;
    } else if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	if (complain)
	    (void)syslog(LOG_ERR, "socket creation failed");
	return -1;
    } else {
	struct sockaddr_un saddr;

	memset(&saddr, 0, sizeof(struct sockaddr_un));
	saddr.sun_family = AF_UNIX;
	(void)strlcpy(saddr.sun_path, 
		      control_socket, 
		      sizeof(saddr.sun_path));

	/*@-unrecog@*/
	if (connect(sock, (struct sockaddr *)&saddr, SUN_LEN(&saddr)) < 0) {
	    if (complain)
		(void)syslog(LOG_ERR, "socket connect failed");
	    return -1;
	}
	/*@+unrecog@*/

	return sock;
    }
}

static int gpsd_control(char *action, char *argument)
/* pass a command to gpsd; start the daemon if not already running */
{
    int connect;
    char buf[512];

    (void)syslog(LOG_ERR, "gpsd_control(action=%s, arg=%s)", action, argument);
    connect = gpsd_control_connect(false);
    if (connect >= 0)
	syslog(LOG_INFO, "reached a running gpsd");
    else if (strcmp(action, "add") == 0) {
	(void)snprintf(buf, sizeof(buf),
		       "gpsd %s -F %s", gpsd_options, control_socket);
	(void)syslog(LOG_NOTICE, "launching %s", buf);
	if (system(buf) != 0) {
	    (void)syslog(LOG_ERR, "launch of gpsd failed");
	    return -1;
	}
        connect = gpsd_control_connect(true);
    }
    if (connect < 0) {
	syslog(LOG_ERR, "can't reach gpsd");
	return -1;
    }
    /*
     * We've got a live connection to the gpsd control socket.  No
     * need to parse the response, because gpsd will lock on to the
     * device if it's really a GPS and ignore it if it's not.
     *
     * The only other place in the code that knows about the format of
     * the add and remove commands is the handle_control() function in
     * gpsd.c. Be careful about keeping them in sync, or hotplugging
     * will have nysterious failures.
     */
    if (strcmp(action, "add") == 0) {
	/*
	 * Force the group-read & group-write bits on, so gpsd will still be
         * able to use this device after dropping root privileges.
	 */
	struct stat sb;

	if (stat(argument, &sb) != 1)
	    (void)chmod(argument, sb.st_mode | S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	(void)snprintf(buf, sizeof(buf), "+%s\r\n", argument);
	(void)write(connect, buf, strlen(buf));
	(void)read(connect, buf, 12);
    } else if (strcmp(action, "remove") == 0) {
	(void)snprintf(buf, sizeof(buf), "-%s\r\n", argument);
	(void)write(connect, buf, strlen(buf));
	(void)read(connect, buf, 12);
    }  else if (strcmp(action, "send") == 0) {
	(void)snprintf(buf, sizeof(buf), "%s\r\n", argument);
	(void)write(connect, buf, strlen(buf));
	(void)read(connect, buf, 12);
    }
    (void)close(connect);
    //syslog(LOG_DEBUG, "gpsd_control ends");
    return 0;
}

int main(int argc, char *argv[])
{
    openlog("gpsdctl", 0, LOG_DAEMON);
    if (argc != 3) {
	(void)syslog(LOG_ERR, "requires action and argument (%d)", argc);
	exit(1);
    } else {
	/*@-observertrans@*/
	char *sockenv = getenv("GPSD_SOCKET");
	char *optenv = getenv("GPSD_OPTIONS");

	if (sockenv != NULL)
	    control_socket = sockenv;
	if (optenv != NULL)
	    gpsd_options = optenv;

	if (gpsd_control(argv[1], argv[2]) < 0)
	    exit(1);
	else
	    exit(0);
	/*@+observertrans@*/
    }
}
