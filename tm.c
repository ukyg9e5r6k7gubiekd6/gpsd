#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include "nmea.h"


struct OUTDATA gNMEAdata;

extern char *latitude;
extern char *longitude;

extern int debug;
extern char latd;
extern char lond;

process_message(char *sentence)
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

send_init()
{
    char buf[82];
    time_t t;
    struct tm *tm;
    char lat[11], lon[11], latd[2], lond[2];

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

do_init()
{
    static count = 0;

    count++;

    if (count == 2) {
	count = 0;
	send_init();
    }
}

process_exception(char *sentence)
{
    if (strncmp("ASTRAL", sentence, 6) == 0 && isatty(gNMEAdata.fdout)) {
	write(gNMEAdata.fdout, "$IIGPQ,ASTRAL*73\r\n", 18);
	do_init();
    } else if (debug > 1) {
	fprintf(stderr, "Unknown exception: \"%s\"",
		sentence);
    }
}

handle_message(char *sentence)
{
    if (debug > 5)
	fprintf(stderr, "%s\n", sentence);
    if (*sentence == '$')
	process_message(sentence + 1);
    else
	process_exception(sentence);

    if (debug > 2) {
	fprintf(stderr,
		"Lat: %lf Lon: %lf Alt: %lf Sat: %d Mod: %d Time: %s\n",
		gNMEAdata.latitude,
		gNMEAdata.longitude,
		gNMEAdata.altitude,
		gNMEAdata.satellites,
		gNMEAdata.mode,
		gNMEAdata.utc);
    }
}
