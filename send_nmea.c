#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>

#include "gpsd.h"
#include "version.h"

#define BUFSIZE	4096

extern struct session_t session;

static void onexit(void)
{
    close(session.dsock);
}

void gps_init(char *device, int timeout, char *dgpsserver, char *dgpsport)
/* initialize GPS polling */
{
    time_t now = time(NULL);

    session.dsock = -1;
    if (dgpsserver) {
	char hn[256], buf[BUFSIZE];

	if (!getservbyname(dgpsport, "tcp"))
	    dgpsport = "2101";

	session.dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
	if (session.dsock < 0)
	    gpscli_errexit("Can't connect to dgps server");

	gethostname(hn, sizeof(hn));

	sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	write(session.dsock, buf, strlen(buf));
	atexit(onexit);
    }

    /* mark fds closed */
    session.gps_device = strdup(device);
    session.fdin = -1;
    session.fdout = -1;

    INIT(session.gNMEAdata.latlon_stamp, now, timeout);
    INIT(session.gNMEAdata.altitude_stamp, now, timeout);
    INIT(session.gNMEAdata.speed_stamp, now, timeout);
    INIT(session.gNMEAdata.status_stamp, now, timeout);
    INIT(session.gNMEAdata.mode_stamp, now, timeout);
    session.gNMEAdata.mode = MODE_NO_FIX;
}

static void send_dgps(void)
{
  char buf[BUFSIZE];

  sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", session.gNMEAdata.latitude,
	  session.gNMEAdata.longitude, session.gNMEAdata.altitude);
  write(session.dsock, buf, strlen(buf));
}

void gps_deactivate(void)
{
    session.fdin = -1;
    session.fdout = -1;
    gps_close();
    free(session.gps_device);
    if (session.device_type->wrapup)
	session.device_type->wrapup();
    gpscli_report(1, "closed GPS\n");
    session.gNMEAdata.mode = 1;
    session.gNMEAdata.status = 0;
}

void gps_activate(void)
{
    int input;

    if ((input = gps_open(session.gps_device, session.device_type->baudrate)) < 0)
    {
	gpscli_errexit("Exiting - serial open\n");
    }
    else
    {
	gpscli_report(1, "opened GPS\n");
	session.fdin = input;
	session.fdout = input;
    }
}

#include <sys/ioctl.h>

static int is_input_waiting(int fd)
{
    int	count;
    if (fd < 0 || ioctl(fd, FIONREAD, &count) < 0)
	return 0;
    return count;
}

void gps_poll(void (*raw_hook)(char *buf))
{
    /* accept a DGPS correction if one is pending */
    if (is_input_waiting(session.dsock))
    {
	char buf[BUFSIZE];
	int rtcmbytes;

	if ((rtcmbytes=read(session.dsock,buf,BUFSIZE))>0 && (session.fdout!=-1))
	{
	    if (session.device_type->rctm_writer(buf, rtcmbytes) <= 0)
		gpscli_report(1, "Write to rtcm sink failed\n");
	}
	else 
	{
	    gpscli_report(1, "Read from rtcm source failed\n");
	}
    }

    /* update the scoreboard structure from the GPS */
    if (is_input_waiting(session.fdin)) {
	session.device_type->handle_input(session.fdin, raw_hook);
    }

    /* count the good fixes */
    if (session.gNMEAdata.status > 0) 
	session.fixcnt++;

    /* may be time to ship a DGPS correction to the GPS */
    if (session.fixcnt > 10) {
	if (!session.sentdgps) {
	    session.sentdgps++;
	    if (session.dsock > -1)
		send_dgps();
	}
    }
}

void gps_force_repoll(void)
{
    session.reopen = 1;
}

