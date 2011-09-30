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

#include "gpsd_config.h"
#include "gps.h"
#include "gpsdclient.h"

static struct exportmethod_t exportmethods[] = {
#if defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S)
    {"dbus", GPSD_DBUS_EXPORT, "DBUS broadcast"},
#endif /* defined(DBUS_EXPORT_ENABLE) && !defined(S_SPLINT_S) */
#ifdef SHM_EXPORT_ENABLE
    {"shm", GPSD_SHARED_MEMORY, "shared memory"},
#endif /* SOCKET_EXPORT_ENABLE */
#ifdef SOCKET_EXPORT_ENABLE
    {"sockets", NULL, "JSON via sockets"},
#endif /* SOCKET_EXPORT_ENABLE */
};

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
/* lat/lon to Maidenhead */
{
    /*
     * Specification at
     * http://en.wikipedia.org/wiki/Maidenhead_Locator_System
     *
     * There's a fair amount of slop in how Maidenhead converters operate
     * that can make it look like this one is wrong.  
     *
     * 1. Many return caps for paces 5 and 6 when according to the spwec
     *    they should return smalls.
     *
     * 2. Some converters, including QGrid from which this code was originally
     *    derived, add an 0.5 offset to the divided e and n just before it
     *    is cast to integer and used for places 5 and 6. This appears to be 
     *    intended as a round-to-nearest hack (as opposed to the implicit
     *    round down from the cast). If I'm reading the spec right it
     *    is not correct to do this.
     */
    static char buf[7];

    int t1;
    e=e+180.0;
    t1=(int)(e/20);
    buf[0]=(char)t1+'A';
    e-=(float)t1*20.0;
    t1=(int)e/2;
    buf[2]=(char)t1+'0';
    e-=(float)t1*2;
    buf[4]=(char)(int)(e*12.0)+'a';

    n=n+90.0;
    t1=(int)(n/10.0);
    buf[1]=(char)t1+'A';
    n-=(float)t1*10.0;
    buf[3]=(char)n+'0';
    n-=(int)n;
    n*=24; // convert to 24 division
    buf[5]=(char)(int)(n)+'a';
    buf[6] = '\0';
 
    return buf;
}

#define NITEMS(x) (int)(sizeof(x)/sizeof(x[0])) /* from gpsd.h-tail */

/*@null observer@*/struct exportmethod_t *export_lookup(const char *name)
/* Look up an available export method by name */
{
    /*@-globstate@*/
    struct exportmethod_t *mp, *method = NULL;

    for (mp = exportmethods;
	 mp < exportmethods + NITEMS(exportmethods);
	 mp++)
	if (strcmp(mp->name, name) == 0)
	    method = mp;
    return method;
    /*@+globstate@*/
}

void export_list(FILE *fp)
/* list known export methods */
{
    struct exportmethod_t *method;

    for (method = exportmethods;
	 method < exportmethods + NITEMS(exportmethods);
	 method++)
	(void)fprintf(fp, "%s: %s\n", method->name, method->description);
}

/*@null observer@*/struct exportmethod_t *export_default(void)
{
    return (NITEMS(exportmethods) > 0) ? &exportmethods[0] : NULL;
}

/* Convert true heading to magnetic.  Taken from the Aviation
   Formulary v1.43.  Valid to within two degrees within the
   continiental USA except for the following airports: MO49 MO86 MO50
   3K6 02K and KOOA.  AK correct to better than one degree.  Western
   Europe correct to within 0.2 deg.

   If you're not in one of these areas, I apologize, I don't have the
   math to compute your varation.  This is obviously extremely
   floating-point heavy, so embedded people, beware of using.

   Note that there are issues with using magnetic heading.  This code
   does not account for the possibility of travelling into or out of
   an area of valid calculation beyond forcing the magnetic conversion
   off.  A better way to communicate this to the user is probably
   desirable (in case the don't notice the subtle change from "(mag)"
   to "(true)" on their display).
 */
float true2magnetic(double lat, double lon, double heading)
{
    /* Western Europe */
    /*@ -evalorder +relaxtypes @*/
    if ((lat > 36.0) && (lat < 68.0) && (lon > -10.0) && (lon < 28.0)) {
	heading =
	    (10.4768771667158 - (0.507385322418858 * lon) +
	     (0.00753170031703826 * pow(lon, 2))
	     - (1.40596203924748e-05 * pow(lon, 3)) -
	     (0.535560699962353 * lat)
	     + (0.0154348808069955 * lat * lon) -
	     (8.07756425110592e-05 * lat * pow(lon, 2))
	     + (0.00976887198864442 * pow(lat, 2)) -
	     (0.000259163929798334 * lon * pow(lat, 2))
	     - (3.69056939266123e-05 * pow(lat, 3)) + heading);
    }
    /* USA */
    else if ((lat > 24.0) && (lat < 50.0) && (lon > 66.0) && (lon < 125.0)) {
	lon = 0.0 - lon;
	heading =
	    ((-65.6811) + (0.99 * lat) + (0.0128899 * pow(lat, 2)) -
	     (0.0000905928 * pow(lat, 3)) + (2.87622 * lon)
	     - (0.0116268 * lat * lon) - (0.00000603925 * lon * pow(lat, 2)) -
	     (0.0389806 * pow(lon, 2))
	     - (0.0000403488 * lat * pow(lon, 2)) +
	     (0.000168556 * pow(lon, 3)) + heading);
    }
    /* AK */
    else if ((lat > 54.0) && (lon > 130.0) && (lon < 172.0)) {
	lon = 0.0 - lon;
	heading =
	    (618.854 + (2.76049 * lat) - (0.556206 * pow(lat, 2)) +
	     (0.00251582 * pow(lat, 3)) - (12.7974 * lon)
	     + (0.408161 * lat * lon) + (0.000434097 * lon * pow(lat, 2)) -
	     (0.00602173 * pow(lon, 2))
	     - (0.00144712 * lat * pow(lon, 2)) +
	     (0.000222521 * pow(lon, 3)) + heading);
    } else {
	/* We don't know how to compute magnetic heading for this
	 * location. */
	heading = NAN;
    }

    /* No negative headings. */
    if (isnan(heading)== 0 && heading < 0.0)
	heading += 360.0;

    return (heading);
    /*@ +evalorder -relaxtypes @*/
}

/* gpsclient.c ends here */
