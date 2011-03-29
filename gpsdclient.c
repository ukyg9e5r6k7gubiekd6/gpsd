/*
 * gpsdclient.c -- support functions for GPSD clients
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* for strcasecmp() */
#include <math.h>
#include <assert.h>
#include <locale.h>

#include "gpsd_config.h"
#include "gps.h"
#include "gpsdclient.h"

/* convert double degrees to a static string and return a pointer to it
 *
 * deg_str_type:
 *   	deg_dd     : return DD.dddddd
 *      deg_ddmm   : return DD MM.mmmm'
 *      deg_ddmmss : return DD MM' SS.sss"
 *
 */
/*@observer@*/ char *deg_to_str(enum deg_str_type type, double f)
{
    static char str[40];
    int dsec, sec, deg, min;
    long frac_deg;
    double fdsec, fsec, fdeg, fmin;

    if (f < 0 || f > 360) {
	(void)strlcpy(str, "nan", sizeof(str));
	return str;
    }

    fmin = modf(f, &fdeg);
    deg = (int)fdeg;
    frac_deg = (long)(fmin * 1000000);

    if (deg_dd == type) {
	/* DD.dddddd */
	(void)snprintf(str, sizeof(str), "%3d.%06ld", deg, frac_deg);
	return str;
    }
    fsec = modf(fmin * 60, &fmin);
    min = (int)fmin;
    sec = (int)(fsec * 10000.0);

    if (deg_ddmm == type) {
	/* DD MM.mmmm */
	(void)snprintf(str, sizeof(str), "%3d %02d.%04d'", deg, min, sec);
	return str;
    }
    /* else DD MM SS.sss */
    fdsec = modf(fsec * 60, &fsec);
    sec = (int)fsec;
    dsec = (int)(fdsec * 1000.0);
    (void)snprintf(str, sizeof(str), "%3d %02d' %02d.%03d\"", deg, min, sec,
		   dsec);

    return str;
}

/* 
 * check the environment to determine proper GPS units
 *
 * clients should only call this if no user preference is specified on 
 * the command line or via X resources.
 *
 * return imperial    - Use miles/feet
 *        nautical    - Use knots/feet
 *        metric      - Use km/meters
 *        unspecified - use compiled default
 * 
 * In order check these environment vars:
 *    GPSD_UNITS one of: 
 *            	imperial   = miles/feet
 *              nautical   = knots/feet
 *              metric     = km/meters
 *    LC_MEASUREMENT
 *		en_US      = miles/feet
 *              C          = miles/feet
 *              POSIX      = miles/feet
 *              [other]    = km/meters
 *    LANG
 *		en_US      = miles/feet
 *              C          = miles/feet
 *              POSIX      = miles/feet
 *              [other]    = km/meters
 *
 * if none found then return compiled in default
 */
enum unit gpsd_units(void)
{
    char *envu = NULL;

    (void)setlocale(LC_NUMERIC, "C");
    if ((envu = getenv("GPSD_UNITS")) != NULL && *envu != '\0') {
	if (0 == strcasecmp(envu, "imperial")) {
	    return imperial;
	}
	if (0 == strcasecmp(envu, "nautical")) {
	    return nautical;
	}
	if (0 == strcasecmp(envu, "metric")) {
	    return metric;
	}
	/* unrecognized, ignore it */
    }
    if (((envu = getenv("LC_MEASUREMENT")) != NULL && *envu != '\0')
	|| ((envu = getenv("LANG")) != NULL && *envu != '\0')) {
	if (strncasecmp(envu, "en_US", 5) == 0
	    || strcasecmp(envu, "C") == 0 || strcasecmp(envu, "POSIX") == 0) {
	    return imperial;
	}
	/* Other, must be metric */
	return metric;
    }
    /* TODO: allow a compile time default here */
    return unspecified;
}

/*@ -observertrans -statictrans -mustfreeonly -branchstate -kepttrans @*/
void gpsd_source_spec(const char *arg, struct fixsource_t *source)
/* standard parsing of a GPS data source spec */
{
    source->server = "localhost";
    source->port = DEFAULT_GPSD_PORT;
    source->device = NULL;

    /*@-usedef@ Sigh, splint is buggy */
    if (arg != NULL) {
	char *colon1, *skipto, *rbrk;
	source->spec = strdup(arg);
	assert(source->spec != NULL);

	skipto = source->spec;
	if (*skipto == '[' && (rbrk = strchr(skipto, ']')) != NULL) {
	    skipto = rbrk;
	}
	colon1 = strchr(skipto, ':');

	if (colon1 != NULL) {
	    char *colon2;
	    *colon1 = '\0';
	    if (colon1 != source->spec) {
		source->server = source->spec;
	    }
	    source->port = colon1 + 1;
	    colon2 = strchr(source->port, ':');
	    if (colon2 != NULL) {
		*colon2 = '\0';
		source->device = colon2 + 1;
	    }
	} else if (strchr(source->spec, '/') != NULL) {
	    source->device = source->spec;
	} else {
	    source->server = source->spec;
	}
    }

    /*@-modobserver@*/
    if (*source->server == '[') {
	char *rbrk = strchr(source->server, ']');
	++source->server;
	if (rbrk != NULL)
	    *rbrk = '\0';
    }
    /*@+modobserver@*/
    /*@+usedef@*/
}

/*@ +observertrans -statictrans +mustfreeonly +branchstate +kepttrans @*/

char *maidenhead(double n, double e)
/* lat/lon to Maidenhead (from QGrid - http://users.pandora.be/on4qz/qgrid/) */
{
    static char buf[7];

    int t1;
    e=e+180.0;
    t1=(int)(e/20);
    buf[0]=(char)t1+'A';
    e-=(float)t1*20.0;
    t1=(int)e/2;
    buf[2]=(char)t1+'0';
    e-=(float)t1*2;
    buf[4]=(char)(int)(e*12.0+0.5)+'a';

    n=n+90.0;
    t1=(int)(n/10.0);
    buf[1]=(char)t1+'A';
    n-=(float)t1*10.0;
    buf[3]=(char)n+'0';
    n-=(int)n;
    n*=24; // convert to 24 division
    buf[5]=(char)(int)(n+0.5)+'a';
    buf[6] = '\0';
 
    return buf;
}

/* gpsclient.c ends here */
