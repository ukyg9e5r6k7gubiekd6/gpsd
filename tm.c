#include "config.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include "gpsd.h"
#include "nmea.h"


struct OUTDATA gNMEAdata;

extern char *latitude;
extern char *longitude;

extern int debug;
extern int device_type;
extern char latd;
extern char lond;

void process_message(char *sentence)
{
    if (checksum(sentence)) {
	if (strncmp(GPRMC, sentence, 5) == 0) {
	    processGPRMC(sentence);
	} else if (strncmp(GPGGA, sentence, 5) == 0) {
	    processGPGGA(sentence);
	} else if (strncmp(GPGSA, sentence, 5) == 0) {
	    processGPGSA(sentence);
	} else if (strncmp(GPGSV, sentence, 5) == 0) {
	    processGPGSV(sentence);
	} else if (strncmp(PRWIZCH, sentence, 7) == 0) {
	    processPRWIZCH(sentence);
	} else {
	    if (debug > 1) {
		fprintf(stderr, "Unknown sentence: \"%s\"\n",
			sentence);
	    }
	}
    }
}

void send_init()
{
    char buf[82];
    time_t t;
    struct tm *tm;

    if (latitude && longitude) {
	t = time(NULL);
	tm = gmtime(&t);

	sprintf(buf,
		"$PRWIINIT,V,,,%s,%c,%s,%c,100.0,0.0,M,0.0,T,%02d%02d%02d,%02d%02d%02d*",
		latitude, latd, longitude, lond,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year);
	add_checksum(buf + 1);	/* add c-sum + cr/lf */
	write(gNMEAdata.fdout, buf, strlen(buf));
	if (debug > 1) {
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
    if (strncmp("ASTRAL", sentence, 6) == 0 && isatty(gNMEAdata.fdout)) {
	write(gNMEAdata.fdout, "$IIGPQ,ASTRAL*73\r\n", 18);
	syslog(LOG_NOTICE, "Found a TripMate, initializing...");
	do_init();
    } else if ((strncmp("EARTHA", sentence, 6) == 0 
		&& isatty(gNMEAdata.fdout))) {
	write(gNMEAdata.fdout, "EARTHA\r\n", 8);
	device_type = DEVICE_EARTHMATEb;
	syslog(LOG_NOTICE, "Found an EarthMate (id).");
	do_eminit();
    } else if (debug > 1) {
	fprintf(stderr, "Unknown exception: \"%s\"",
		sentence);
    }
}

void handle_message(char *sentence)
{
    if (debug > 5)
	fprintf(stderr, "%s\n", sentence);
    if (*sentence == '$')
	process_message(sentence + 1);
    else
	process_exception(sentence);

    if (debug > 2) {
	fprintf(stderr,
		"Lat: %f Lon: %f Alt: %f Sat: %d Mod: %d Time: %s\n",
		gNMEAdata.latitude,
		gNMEAdata.longitude,
		gNMEAdata.altitude,
		gNMEAdata.satellites,
		gNMEAdata.mode,
		gNMEAdata.utc);
    }
}
