/*
 * Handle the proprietary extensions to NMEA 0183 supported by the
 * TripMATE GPS.  Also, if requested, intialize it with longitude,
 * latitude, and local time.  This will speed up its first fix.
 */
#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include "outdata.h"
#include "nmea.h"
#include "gpsd.h"

extern struct session_t session;

void send_init()
{
    char buf[82];
    time_t t;
    struct tm *tm;

    if (session.initpos.latitude && session.initpos.longitude) {
	t = time(NULL);
	tm = gmtime(&t);

	if(tm->tm_year > 100)
	    tm->tm_year = tm->tm_year - 100;

	sprintf(buf,
		"$PRWIINIT,V,,,%s,%c,%s,%c,100.0,0.0,M,0.0,T,%02d%02d%02d,%02d%02d%02d*",
		session.initpos.latitude, session.initpos.latd, 
		session.initpos.longitude, session.initpos.lond,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year);
	add_checksum(buf + 1);	/* add c-sum + cr/lf */
	if (session.gNMEAdata.fdout != -1)
	    write(session.gNMEAdata.fdout, buf, strlen(buf));
	if (session.debug > 1) {
	    fprintf(stderr, "Sending: %s", buf);
	}
    }
}

void do_init()
{
    static int count = 0;

    count++;

    if (count == 2) {
	count = 0;
	send_init();
    }
}

void process_exception(char *sentence)
{
    if (strncmp("ASTRAL", sentence, 6) == 0 && isatty(session.gNMEAdata.fdout)) {
	write(session.gNMEAdata.fdout, "$IIGPQ,ASTRAL*73\r\n", 18);
	syslog(LOG_NOTICE, "Found a TripMate, initializing...");
	do_init();
    } else if ((strncmp("EARTHA", sentence, 6) == 0 
		&& isatty(session.gNMEAdata.fdout))) {
	write(session.gNMEAdata.fdout, "EARTHA\r\n", 8);
	session.device_type = DEVICE_EARTHMATEb;
	syslog(LOG_NOTICE, "Found an EarthMate (id).");
	do_eminit();
    } else if (session.debug > 1) {
	fprintf(stderr, "Unknown exception: \"%s\"",
		sentence);
    }
}

void handle_message(char *sentence)
{
    if (session.debug > 5)
	fprintf(stderr, "%s\n", sentence);

    if (*sentence == '$')
	process_NMEA_message(sentence + 1, &session.gNMEAdata);
    else
	process_exception(sentence);

    if (session.debug > 2) {
	fprintf(stderr,
		"Lat: %f Lon: %f Alt: %f Sat: %d Mod: %d Time: %s\n",
		session.gNMEAdata.latitude,
		session.gNMEAdata.longitude,
		session.gNMEAdata.altitude,
		session.gNMEAdata.satellites,
		session.gNMEAdata.mode,
		session.gNMEAdata.utc);
    }
}
