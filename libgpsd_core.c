/*
 * This provides the interface to the library that supports direct access to
 * GPSes on serial or USB devices.
 */
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
    struct gps_type_t **dp;
    for (dp = gpsd_drivers; dp < gpsd_drivers + sizeof(gpsd_drivers)/sizeof(gpsd_drivers[0]); dp++)
	if ((*dp)->typekey == what) {
	    gpscli_report(3, "Selecting %s driver...\n", (*dp)->typename);
	    goto foundit;
	}
    return NULL;
 foundit:;
    return *dp;
}

void gpsd_init(struct gps_session_t *session, char devicetype, char *dgpsserver)
/* initialize GPS polling */
{
    time_t now = time(NULL);
    struct gps_type_t *devtype;

    session->gpsd_device = "/dev/gps";
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
	    gpscli_report(1, "Can't connect to dgps server, netlib error %d\n", session->dsock);
	else
	{
	    gethostname(hn, sizeof(hn));

	    sprintf(buf, "HELO %s gpsd %s\r\nR\r\n", hn, VERSION);
	    write(session->dsock, buf, strlen(buf));
	}
    }

    /* mark fds closed */
    session->fdin = -1;
    session->fdout = -1;

    INIT(session->gNMEAdata.online_stamp, now);
    INIT(session->gNMEAdata.latlon_stamp, now);
    INIT(session->gNMEAdata.altitude_stamp, now);
    INIT(session->gNMEAdata.track_stamp, now);
    INIT(session->gNMEAdata.speed_stamp, now);
    INIT(session->gNMEAdata.status_stamp, now);
    INIT(session->gNMEAdata.mode_stamp, now);
    INIT(session->gNMEAdata.fix_quality_stamp, now);
    INIT(session->gNMEAdata.satellite_stamp, now);
#ifdef PROCESS_PRWIZCH
    INIT(session->gNMEAdata.signal_quality_stamp, now);
#endif /* PROCESS_PRWIZCH */
    session->gNMEAdata.mode = MODE_NO_FIX;
}

void gpsd_deactivate(struct gps_session_t *session)
/* temporarily release the GPS device */
{
    session->gNMEAdata.online = 0;
    REFRESH(session->gNMEAdata.online_stamp);
    session->gNMEAdata.mode = MODE_NO_FIX;
    session->gNMEAdata.status = STATUS_NO_FIX;
    session->fdin = -1;
    session->fdout = -1;
    gpsd_close();
    if (session->device_type->wrapup)
	session->device_type->wrapup(session);
    gpscli_report(1, "closed GPS\n");
}

int gpsd_activate(struct gps_session_t *session)
/* acquire a connection to the GPS device */
{
    int input;

    if ((input = gpsd_open(session->gpsd_device, session->baudrate)) < 0)
	return -1;
    else
    {
	session->gNMEAdata.online = 1;
	REFRESH(session->gNMEAdata.online_stamp);
	session->fdin = input;
	session->fdout = input;
	gpscli_report(1, "gpsd_activate: opened GPS (%d)\n", input);
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

int gpsd_poll(struct gps_session_t *session)
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
	    if (session->device_type->rtcm_writer(session, buf, rtcmbytes) <= 0)
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
    if (waiting < 0)
	return waiting;
    else if (!waiting) {
	if (time(NULL) <= session->gNMEAdata.online_stamp.last_refresh + session->device_type->interval*2) {
	    return 0;
	} else {
	    session->gNMEAdata.online = 0;
	    REFRESH(session->gNMEAdata.online_stamp);
	    return -1;
	}
    } else {
	session->gNMEAdata.online = 1;
	REFRESH(session->gNMEAdata.online_stamp);

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

void gpsd_wrap(struct gps_session_t *session)
/* end-of-session wrapup */
{
    gpsd_deactivate(session);
    if (session->dsock >= 0)
	close(session->dsock);
}
