#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/ioctl.h>

#include "gps.h"
#include "gpsd.h"
#include "version.h"

static struct gps_type_t *set_device_type(char what)
/* select a device driver by key letter */
{
    struct gps_type_t **dp, *drivers[] = {&nmea, 
					  &tripmate,
					  &earthmate_a, 
					  &earthmate_b,
    					  &logfile};
    for (dp = drivers; dp < drivers + sizeof(drivers)/sizeof(drivers[0]); dp++)
	if ((*dp)->typekey == what) {
	    gpscli_report(3, "Selecting %s driver...\n", (*dp)->typename);
	    goto foundit;
	}
    return NULL;
 foundit:;
    return *dp;
}

void gps_init(struct gpsd_t *session, int timeout, char devicetype, char *dgpsserver)
/* initialize GPS polling */
{
    time_t now = time(NULL);
    struct gps_type_t *devtype;

    session->gps_device = "/dev/gps";
    session->device_type = &nmea;
    devtype = set_device_type(devicetype);
    if (!devtype)
	gpscli_report(1, "invalid GPS type \"%s\", using NMEA instead\n", optarg);
    else
    {
	session->device_type = devtype;
	session->baudrate = devtype->baudrate;
    }
	

    session->dsock = -1;
    if (dgpsserver) {
	char hn[256], buf[BUFSIZE];
	char *colon, *dgpsport = "rtcm-sc104";

	if ((colon = strchr(optarg, ':'))) {
	    dgpsport = colon+1;
	    *colon = '\0';
	}

	if (!getservbyname(dgpsport, "tcp"))
	    dgpsport = "2101";

	session->dsock = netlib_connectsock(dgpsserver, dgpsport, "tcp");
	if (session->dsock < 0)
	    gpscli_report(1, "Can't connect to dgps server");

	gethostname(hn, sizeof(hn));

	sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	write(session->dsock, buf, strlen(buf));
    }

    /* mark fds closed */
    session->fdin = -1;
    session->fdout = -1;

    INIT(session->gNMEAdata.latlon_stamp, now, timeout);
    INIT(session->gNMEAdata.altitude_stamp, now, timeout);
    INIT(session->gNMEAdata.track_stamp, now, timeout);
    INIT(session->gNMEAdata.speed_stamp, now, timeout);
    INIT(session->gNMEAdata.status_stamp, now, timeout);
    INIT(session->gNMEAdata.mode_stamp, now, timeout);
    session->gNMEAdata.mode = MODE_NO_FIX;
}

void gps_deactivate(struct gpsd_t *session)
/* temporarily release the GPS device */
{
    session->gNMEAdata.online = 0;
    session->gNMEAdata.mode = MODE_NO_FIX;
    session->gNMEAdata.status = STATUS_NO_FIX;
    session->fdin = -1;
    session->fdout = -1;
    gps_close();
    if (session->device_type->wrapup)
	session->device_type->wrapup(session);
    gpscli_report(1, "closed GPS\n");
}

int gps_activate(struct gpsd_t *session)
/* acquire a connection to the GPS device */
{
    int input;

    if ((input = gps_open(session->gps_device, session->baudrate)) < 0)
	return -1;
    else
    {
	session->gNMEAdata.online = 1;
	session->fdin = input;
	session->fdout = input;
	gpscli_report(1, "gps_activate: opened GPS (%d)\n", input);
	return input;
    }
}

static int is_input_waiting(int fd)
{
    int	count;
    if (fd < 0 || ioctl(fd, FIONREAD, &count) < 0)
	return -1;
    return count;
}

int gps_poll(struct gpsd_t *session)
/* update the stuff in the scoreboard structure */
{
    int waiting;

    /* accept a DGPS correction if one is pending */
    if (is_input_waiting(session->dsock) > 0)
    {
	char buf[BUFSIZE];
	int rtcmbytes;

	if ((rtcmbytes=read(session->dsock,buf,BUFSIZE))>0 && (session->fdout!=-1))
	{
	    if (session->device_type->rctm_writer(session, buf, rtcmbytes) <= 0)
		gpscli_report(1, "Write to rtcm sink failed\n");
	}
	else 
	{
	    gpscli_report(1, "Read from rtcm source failed\n");
	}
    }

    /* update the scoreboard structure from the GPS */
    waiting = is_input_waiting(session->fdin);
    gpscli_report(4, "GPS has %d chars waiting\n", waiting);
    if (waiting <= 0)
    {
	session->gNMEAdata.online = 0;
	return waiting;
    }
    else if (waiting) {
	session->gNMEAdata.online = 1;

	/* call the input routine from the device-specific driver */
	session->device_type->handle_input(session);

	/* count the good fixes */
	if (session->gNMEAdata.status > STATUS_NO_FIX) 
	    session->fixcnt++;

	/* may be time to ship a DGPS correction to the GPS */
	if (session->fixcnt > 10) {
	    if (!session->sentdgps) {
		session->sentdgps++;
		if (session->dsock > -1)
		{
		  char buf[BUFSIZE];

		  sprintf(buf, "R %0.8f %0.8f %0.2f\r\n", 
			  session->gNMEAdata.latitude,
			  session->gNMEAdata.longitude, 
			  session->gNMEAdata.altitude);
		  write(session->dsock, buf, strlen(buf));
		}
	    }
	}
    }
    return waiting;
}

void gps_wrap(struct gpsd_t *session)
/* end-of-session wrapup */
{
    gps_deactivate(session);
    close(session->dsock);
}
