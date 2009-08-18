/* $Id$ */
/* libgps.c -- client interface library for the gpsd daemon */
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#ifndef S_SPLINT_S
#include <pthread.h>	/* pacifies OpenBSD's compiler */
#endif
#include <math.h>
#include <locale.h>

#include "gpsd_config.h"
#include "gpsd.h"
#ifdef GPSDNG_ENABLE
#include "gps_json.h"
#endif /* GPSDNG_ENABLE */

#ifdef S_SPLINT_S
extern char *strtok_r(char *, const char *, char **);
#endif /* S_SPLINT_S */

struct gps_data_t *gps_open(const char *host, const char *port)
/* open a connection to a gpsd daemon */
{
    struct gps_data_t *gpsdata = (struct gps_data_t *)calloc(sizeof(struct gps_data_t), 1);

    /*@ -branchstate @*/
    if (!gpsdata)
	return NULL;
    if (!host)
	host = "127.0.0.1";
    if (!port)
	port = DEFAULT_GPSD_PORT;

    if ((gpsdata->gps_fd = netlib_connectsock(host, port, "tcp")) == -1) {
	errno = gpsdata->gps_fd;
	(void)free(gpsdata);
	return NULL;
    }

    gpsdata->status = STATUS_NO_FIX;
    gps_clear_fix(&gpsdata->fix);
    return gpsdata;
    /*@ +branchstate @*/
}

int gps_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
    int retval = close(gpsdata->gps_fd);
    if (gpsdata->gps_id) {
	(void)free(gpsdata->gps_id);
	gpsdata->gps_id = NULL;
    }
    gpsdata->gps_device[0] = '\0';
    /*@i@*/(void)free(gpsdata);
    return retval;
}

void gps_set_raw_hook(struct gps_data_t *gpsdata,
		      void (*hook)(struct gps_data_t *, char *, size_t len, int level))
{
    gpsdata->raw_hook = hook;
}

/*@ -branchstate -usereleased -mustfreefresh @*/
static void gps_unpack(char *buf, struct gps_data_t *gpsdata)
/* unpack a gpsd response into a status structure, buf must be writeable */
{
    char *ns, *sp, *tp;
    int i;

#ifdef GPSDNG_ENABLE
    /* detect and process a JSON response */
    if (buf[0] == '{' && (sp = strchr(buf, '='))!= NULL) {
	(void)libgps_json_unpack(buf, gpsdata);
    }
#endif /* GPSDNG_ENABLE */
#if defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE)
    else
#endif /* defined(OLDSTYLE_ENABLE) && defined(GPSDNG_ENABLE) */
#ifdef OLDSTYLE_ENABLE
    {
	/*
	 * Get the decimal separator for the current application locale.
	 * This looks thread-unsafe, but it's not.  The key is that
	 * character assignment is atomic.
	 */
	static char decimal_point = '\0';
	if (decimal_point == '\0') {
	    struct lconv *locale_data = localeconv();
	    if (locale_data != NULL && locale_data->decimal_point[0] != '.')
		decimal_point = locale_data->decimal_point[0];
	}

	for (ns = buf; ns; ns = strstr(ns+1, "GPSD")) {
	    if (/*@i1@*/strncmp(ns, "GPSD", 4) == 0) {
		bool eol = false;
		/* the following should execute each time we have a good next sp */
		for (sp = ns + 5; *sp != '\0'; sp = tp+1) {
		    tp = sp + strcspn(sp, ",\r\n");
		    eol = *tp == '\r' || *tp == '\n';
		    if (*tp == '\0')
			tp--;
		    else
			*tp = '\0';

		    /*
		     * The daemon always emits the Anglo-American and SI
		     * decimal point.  Hack these into whatever the
		     * application locale requires if it's not the same.
		     * This has to happen *after* we grab the next
		     * comma-delimited response, or we'll lose horribly
		     * in locales where the decimal separator is comma.
		     */
		    if (decimal_point != '\0') {
			char *cp;
			for (cp = sp; cp < tp; cp++)
			    if (*cp == '.')
				*cp = decimal_point;
		    }

		    /* note, there's a bit of skip logic after the switch */

		    switch (*sp) {
		    case 'A':
			if (sp[2] == '?') {
				gpsdata->fix.altitude = NAN;
			} else {
			    (void)sscanf(sp, "A=%lf", &gpsdata->fix.altitude);
			    gpsdata->set |= ALTITUDE_SET;
			}
			break;
		    case 'B':
			if (sp[2] == '?') {
			    gpsdata->baudrate = gpsdata->stopbits = 0;
			} else
			    (void)sscanf(sp, "B=%u %*d %*s %u",
				   &gpsdata->baudrate, &gpsdata->stopbits);
			break;
		    case 'C':
			if (sp[2] == '?')
			    gpsdata->mincycle = gpsdata->cycle = 0;
			else {
			    if (sscanf(sp, "C=%lf %lf",
					 &gpsdata->cycle,
				       &gpsdata->mincycle) < 2)
				gpsdata->mincycle = gpsdata->cycle;
			}
			break;
		    case 'D':
			if (sp[2] == '?')
			    gpsdata->fix.time = NAN;
			else {
			    gpsdata->fix.time = iso8601_to_unix(sp+2);
			    gpsdata->set |= TIME_SET;
			}
			break;
		    case 'E':
			gpsdata->epe = gpsdata->fix.eph = gpsdata->fix.epv = NAN;
			/* epe should always be present if eph or epv is */
			if (sp[2] != '?') {
			    char epe[20], eph[20], epv[20];
			    (void)sscanf(sp, "E=%s %s %s", epe, eph, epv);
#define DEFAULT(val) (val[0] == '?') ? NAN : atof(val)
				/*@ +floatdouble @*/
				gpsdata->epe = DEFAULT(epe);
				gpsdata->fix.eph = DEFAULT(eph);
				gpsdata->fix.epv = DEFAULT(epv);
				/*@ -floatdouble @*/
#undef DEFAULT
			}
			break;
		    case 'F':
			/*@ -mustfreeonly */
			if (sp[2] == '?')
			    gpsdata->gps_device[0] = '\0';
			else {
			    /*@ -mayaliasunique @*/
			    strncpy(gpsdata->gps_device, sp+2, PATH_MAX);
			    /*@ +mayaliasunique @*/
			    gpsdata->set |= DEVICE_SET;
			}
			/*@ +mustfreeonly */
			break;
		    case 'I':
			/*@ -mustfreeonly */
			if (gpsdata->gps_id)
			    free(gpsdata->gps_id);
			if (sp[2] == '?')
			    gpsdata->gps_id = NULL;
			else {
			    gpsdata->gps_id = strdup(sp+2);
			    gpsdata->set |= DEVICEID_SET;
			}
			/*@ +mustfreeonly */
			break;
		    case 'K':
			if (sp[2] != '?') {
			    char *rc = strdup(sp);
			    char *sp2 = rc;
			    char *ns2 = ns;
			    memset(&gpsdata->devices, '\0', sizeof(gpsdata->devices));
			    gpsdata->devices.ndevices = (int)strtol(sp2+2, &sp2, 10);
			    strlcpy(gpsdata->devices.list[i=0].path,
				    strtok_r(sp2+1," \r\n", &ns2),
				    sizeof(gpsdata->devices.list[i=0].path));
			    while ((sp2 = strtok_r(NULL, " \r\n",  &ns2)))
				if (i < MAXDEVICES_PER_USER-1)
				    (void)strlcpy(gpsdata->devices.list[++i].path, 
						 sp2,
						 sizeof(gpsdata->devices.list[i=0].path));
			    free(rc);
			    gpsdata->set |= DEVICELIST_SET;
			    gpsdata->devices.time = timestamp();
			}
			break;
		    case 'M':
			if (sp[2] == '?') {
			    gpsdata->fix.mode = MODE_NOT_SEEN;
			} else {
			    gpsdata->fix.mode = atoi(sp+2);
			    gpsdata->set |= MODE_SET;
			}
			break;
		    case 'N':
			if (sp[2] == '?')
			    gpsdata->driver_mode = MODE_NMEA;
			else
			    gpsdata->driver_mode = (unsigned)atoi(sp+2);
			break;
		    case 'O':
			if (sp[2] == '?') {
			    gpsdata->set = MODE_SET | STATUS_SET;
			    gpsdata->status = STATUS_NO_FIX;
			    gps_clear_fix(&gpsdata->fix);
			} else {
			    struct gps_fix_t nf;
			    char tag[MAXTAGLEN+1], alt[20];
			    char eph[20], epv[20], track[20],speed[20], climb[20];
			    char epd[20], eps[20], epc[20], mode[2];
			    char timestr[20], ept[20], lat[20], lon[20];
			    int st = sscanf(sp+2,
				   "%8s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %19s %1s",
				    tag, timestr, ept, lat, lon,
				    alt, eph, epv, track, speed, climb,
				    epd, eps, epc, mode);
			    if (st >= 14) {
    #define DEFAULT(val) (val[0] == '?') ? NAN : atof(val)
				/*@ +floatdouble @*/
				nf.time = DEFAULT(timestr);
				nf.latitude = DEFAULT(lat);
				nf.longitude = DEFAULT(lon);
				nf.ept = DEFAULT(ept);
				nf.altitude = DEFAULT(alt);
				nf.eph = DEFAULT(eph);
				nf.epv = DEFAULT(epv);
				nf.track = DEFAULT(track);
				nf.speed = DEFAULT(speed);
				nf.climb = DEFAULT(climb);
				nf.epd = DEFAULT(epd);
				nf.eps = DEFAULT(eps);
				nf.epc = DEFAULT(epc);
				/*@ -floatdouble @*/
    #undef DEFAULT
				if (st >= 15)
				    nf.mode = (mode[0] == '?') ? MODE_NOT_SEEN : atoi(mode);
				else
				    nf.mode = (alt[0] == '?') ? MODE_2D : MODE_3D;
				if (alt[0] != '?')
				    gpsdata->set |= ALTITUDE_SET | CLIMB_SET;
				if (isnan(nf.eph)==0)
				    gpsdata->set |= HERR_SET;
				if (isnan(nf.epv)==0)
				    gpsdata->set |= VERR_SET;
				if (isnan(nf.track)==0)
				    gpsdata->set |= TRACK_SET | SPEED_SET;
				if (isnan(nf.eps)==0)
				    gpsdata->set |= SPEEDERR_SET;
				if (isnan(nf.epc)==0)
				    gpsdata->set |= CLIMBERR_SET;
				gpsdata->fix = nf;
				(void)strlcpy(gpsdata->tag, tag, MAXTAGLEN+1);
				gpsdata->set |= TIME_SET|TIMERR_SET|LATLON_SET|MODE_SET;
				gpsdata->status = STATUS_FIX;
				gpsdata->set |= STATUS_SET;
			    }
			}
			break;
		    case 'P':
			if (sp[2] == '?') {
			       gpsdata->fix.latitude = NAN;
			       gpsdata->fix.longitude = NAN;
			} else {
			    (void)sscanf(sp, "P=%lf %lf",
			       &gpsdata->fix.latitude, &gpsdata->fix.longitude);
			    gpsdata->set |= LATLON_SET;
			}
			break;
		    case 'Q':
			if (sp[2] == '?') {
			       gpsdata->satellites_used = 0;
			       gpsdata->pdop = 0;
			       gpsdata->hdop = 0;
			       gpsdata->vdop = 0;
			} else {
			    (void)sscanf(sp, "Q=%d %lf %lf %lf %lf %lf",
				   &gpsdata->satellites_used,
				   &gpsdata->pdop,
				   &gpsdata->hdop,
				   &gpsdata->vdop,
				   &gpsdata->tdop,
				   &gpsdata->gdop);
			    gpsdata->set |= HDOP_SET | VDOP_SET | PDOP_SET;
			}
			break;
		    case 'S':
			if (sp[2] == '?') {
			    gpsdata->status = -1;
			} else {
			    gpsdata->status = atoi(sp+2);
			    gpsdata->set |= STATUS_SET;
			}
			break;
		    case 'T':
			if (sp[2] == '?') {
			    gpsdata->fix.track = NAN;
			} else {
			    (void)sscanf(sp, "T=%lf", &gpsdata->fix.track);
			    gpsdata->set |= TRACK_SET;
			}
			break;
		    case 'U':
			if (sp[2] == '?') {
			    gpsdata->fix.climb = NAN;
			} else {
			    (void)sscanf(sp, "U=%lf", &gpsdata->fix.climb);
			    gpsdata->set |= CLIMB_SET;
			}
			break;
		    case 'V':
			if (sp[2] == '?') {
			    gpsdata->fix.speed = NAN;
			} else {
			    (void)sscanf(sp, "V=%lf", &gpsdata->fix.speed);
			    /* V reply is in kt, fix.speed is in metres/sec */
			    gpsdata->fix.speed = gpsdata->fix.speed / MPS_TO_KNOTS;
			    gpsdata->set |= SPEED_SET;
			}
			break;
		    case 'X':
			if (sp[2] == '?')
			    gpsdata->online = -1;
			else {
			    (void)sscanf(sp, "X=%lf", &gpsdata->online);
			    gpsdata->set |= ONLINE_SET;
			}
			break;
		    case 'Y':
			if (sp[2] == '?') {
			    gpsdata->satellites = 0;
			} else {
			    int j, i1, i2, i3, i5;
			    int PRN[MAXCHANNELS];
			    int elevation[MAXCHANNELS], azimuth[MAXCHANNELS];
			    int used[MAXCHANNELS];
			    double ss[MAXCHANNELS], f4;
			    char tag[MAXTAGLEN+1], timestamp[21];

			    (void)sscanf(sp, "Y=%8s %20s %d ",
				   tag, timestamp, &gpsdata->satellites);
			    (void)strncpy(gpsdata->tag, tag, MAXTAGLEN);
			    if (timestamp[0] != '?') {
				gpsdata->sentence_time = atof(timestamp);
				gpsdata->set |= TIME_SET;
			    }
			    for (j = 0; j < gpsdata->satellites; j++) {
				PRN[j]=elevation[j]=azimuth[j]=ss[j]=used[j]=0;
			    }
			    for (j = 0, gpsdata->satellites_used = 0; j < gpsdata->satellites; j++) {
				if ((sp != NULL) && ((sp = strchr(sp, ':')) != NULL)) {
				    sp++;
				    (void)sscanf(sp, "%d %d %d %lf %d", &i1, &i2, &i3, &f4, &i5);
				    PRN[j] = i1;
				    elevation[j] = i2; azimuth[j] = i3;
				    ss[j] = f4; used[j] = i5;
				    if (i5 == 1)
					gpsdata->satellites_used++;
				}
			    }
			    /*@ -compdef @*/
			    memcpy(gpsdata->PRN, PRN, sizeof(PRN));
			    memcpy(gpsdata->elevation, elevation, sizeof(elevation));
			    memcpy(gpsdata->azimuth, azimuth,sizeof(azimuth));
			    memcpy(gpsdata->ss, ss, sizeof(ss));
			    memcpy(gpsdata->used, used, sizeof(used));
			    /*@ +compdef @*/
			}
			gpsdata->set |= SATELLITE_SET;
			break;
		    case 'Z':
			gpsdata->profiling = (sp[2] == '1');
			break;
		    case '$':
			if (gpsdata->profiling != true)
			    break;
			/*@ +matchanyintegral -formatcode @*/
			(void)sscanf(sp, "$=%8s %zd %lf %lf %lf %lf %lf %lf",
			       gpsdata->tag,
			       &gpsdata->sentence_length,
			       &gpsdata->fix.time,
			       &gpsdata->d_xmit_time,
			       &gpsdata->d_recv_time,
			       &gpsdata->d_decode_time,
			       &gpsdata->poll_time,
			       &gpsdata->emit_time);
			/*@ -matchanyintegral +formatcode @*/
			break;
		    }

		    /*
		     * Skip to next GPSD when we see \r or \n;
		     * we don't want to try interpreting stuff
		     * in between that might be raw mode data.
		     */
		    if (eol)
			break;
		}
	    }
	}
    }
#endif /* GPSDNG_ENABLE */

/*@ -nullstate -compdef @*/
    if (gpsdata->raw_hook)
	gpsdata->raw_hook(gpsdata, buf, strlen(buf),  1);
    if (gpsdata->thread_hook)
	gpsdata->thread_hook(gpsdata, buf, strlen(buf), 1);
}
/*@ +nullstate +compdef @*/
/*@ -branchstate +usereleased +mustfreefresh @*/

/*
 * return: 0, success
 *        -1, read error
 */

int gps_poll(struct gps_data_t *gpsdata)
/* wait for and read data being streamed from the daemon */
{
    char	buf[BUFSIZ];
    ssize_t	n;
    double received = 0;

    /* the daemon makes sure that every read is NUL-terminated */
    n = read(gpsdata->gps_fd, buf, sizeof(buf)-1);
    if (n <= 0) {
	 /* error or nothing read */
	return -1;
    }
    buf[n] = '\0';

    received = gpsdata->online = timestamp();
    gps_unpack(buf, gpsdata);
    if (gpsdata->profiling)
    {
	gpsdata->c_decode_time = received - gpsdata->fix.time;
	gpsdata->c_recv_time = timestamp() - gpsdata->fix.time;
    }
    return 0;
}

int gps_query(struct gps_data_t *gpsdata, const char *fmt, ... )
/* query a gpsd instance for new data */
{
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf)-2, fmt, ap);
    va_end(ap);
    if (buf[strlen(buf)-1] != '\n')
	(void)strlcat(buf, "\n", BUFSIZ);
    if (write(gpsdata->gps_fd, buf, strlen(buf)) <= 0)
	return -1;
    return gps_poll(gpsdata);
}

#ifdef HAVE_LIBPTHREAD
static /*@null@*/void *poll_gpsd(void *args)
/* helper for the thread launcher */
{
    int oldtype, oldstate;
    int res;
    struct gps_data_t *gpsdata;

    /* set thread parameters */
    /*@ -compdef @*/
    /*@ -unrecog (splint has no pthread declarations as yet) @*/
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&oldstate);
    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&oldtype); /* we want to be canceled also when blocked on gps_poll() */
    /*@ +unrecog @*/
    /*@ +compdef @*/
    gpsdata = (struct gps_data_t *) args;
    do {
	res = gps_poll(gpsdata); /* this is not actually polling */
    } while
	(res == 0);
    /* if we are here an error occured with gpsd */
    return NULL;
}

int gps_set_callback(struct gps_data_t *gpsdata,
		     void (*callback)(struct gps_data_t *sentence, char *buf, size_t len, int level),
		     pthread_t *handler)
/* set an asynchronous callback and launch a thread for it */
{
    (void)gps_query(gpsdata,"w+\n");	/* ensure gpsd is in watcher mode, so we'll have data to read */
    if (gpsdata->thread_hook != NULL) {
	gpsdata->thread_hook = callback;
	return 0;
    }
    gpsdata->thread_hook = callback;

    /* start the thread which will read data from gpsd */
    /*@ -unrecog (splint has no pthread declarations as yet */
    return pthread_create(handler,NULL,poll_gpsd,(void*)gpsdata);
    /*@ +unrecog @*/
}

int gps_del_callback(struct gps_data_t *gpsdata, pthread_t *handler)
/* delete asynchronous callback and kill its thread */
{
    /*@ -nullstate @*/
    int res;

    /*@i@*/res = pthread_cancel(*handler);	/* we cancel the whole thread */
    /*@i1@*/pthread_join(*handler, NULL);	/* wait for thread to actually terminate */
    gpsdata->thread_hook = NULL;	/* finally we cancel the callback */
    if (res == 0) 			/* tell gpsd to stop sending data */
	/*@i1@*/(void)gps_query(gpsdata,"w-\n");	/* disable watcher mode */
    return res;
    /*@ +nullstate @*/
}
#endif /* HAVE_LIBPTHREAD */


#ifdef TESTMAIN
/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 */
static void data_dump(struct gps_data_t *collect, time_t now)
{
    char *status_values[] = {"NO_FIX", "FIX", "DGPS_FIX"};
    char *mode_values[] = {"", "NO_FIX", "MODE_2D", "MODE_3D"};

    if (collect->set & ONLINE_SET)
	printf("online: %lf\n", collect->online);
    if (collect->set & LATLON_SET)
	printf("P: lat/lon: %lf %lf\n", collect->fix.latitude, collect->fix.longitude);
    if (collect->set & ALTITUDE_SET)
	printf("A: altitude: %lf  U: climb: %lf\n",
	       collect->fix.altitude, collect->fix.climb);
    if (!isnan(collect->fix.track))
	printf("T: track: %lf  V: speed: %lf\n",
	       collect->fix.track, collect->fix.speed);
    if (collect->set & STATUS_SET)
	printf("S: status: %d (%s)\n",
	       collect->status, status_values[collect->status]);
    if (collect->fix.mode & MODE_SET)
	printf("M: mode: %d (%s)\n",
	   collect->fix.mode, mode_values[collect->fix.mode]);
    if (collect->fix.mode & (HDOP_SET | VDOP_SET | PDOP_SET))
	printf("Q: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf\n",
	   collect->satellites_used,
	   collect->pdop, collect->hdop, collect->vdop);

    if (collect->set & SATELLITE_SET) {
	int i;

	printf("Y: satellites in view: %d\n", collect->satellites);
	for (i = 0; i < collect->satellites; i++) {
	    printf("    %2.2d: %2.2d %3.3d %3.0f %c\n", collect->PRN[i], collect->elevation[i], collect->azimuth[i], collect->ss[i], collect->used[i]? 'Y' : 'N');
	}
    }
    if (collect->set & DEVICE_SET)
	printf("Device is %s\n", collect->gps_device);
    if (collect->set & DEVICEID_SET)
	printf("GPSD ID is %s\n", collect->gps_id);
    if (collect->set & DEVICELIST_SET) {
	int i;
	printf("%d devices:\n", collect->devices.ndevices);
	for (i = 0; i < collect->devices.ndevices; i++) {
	    printf("%d: %s\n", collect->devices.ndevices, collect->devices.list[i].path);
	}
    }

}

static void dumpline(struct gps_data_t *ud UNUSED, char *buf,
		     size_t ulen UNUSED, int level UNUSED)
{
    puts(buf);
}

#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <getopt.h>
#include <signal.h>

static void onsig(int sig)
{
    (void)fprintf(stderr, "libgps: died with signal %d\n", sig);
    exit(1);
}

/* must start zeroed, otherwise the unit test will try to chaser garbage pinter fields. */
struct gps_data_t gpsdata;
static char buf[] = "GPSD,O=RMC 1207318966.000 0.005 49.026225 12.188348 375.20 19.20 10.40 70.8900 24.899 0.000 75.6699 38.40 ? 3\r\n$GPVTG,70.89,T,,M,48.40,N,89.6,K,A*34\r\n";

static void unpack_unit_test(void)
/* torture the unpacking function */
{

    (void)signal(SIGSEGV, onsig);
    (void)signal(SIGBUS, onsig);

    gps_unpack(buf, &gpsdata);
    data_dump(&gpsdata, time(NULL));
}

int main(int argc, char *argv[])
{
    struct gps_data_t *collect;
    char buf[BUFSIZ];
    int option;
    bool unpack_test = false;

    while ((option = getopt(argc, argv, "uhs?")) != -1) {
	switch (option) {
	case 'u':
	    unpack_test = true;
	    break;
	case 's':
	    (void)printf("Sizes: rtcm2=%zd rtcm3=%zd ais=%zd compass=%zd raw=%zd devices=%zd\n",
		     sizeof(struct rtcm2_t),
		     sizeof(struct rtcm3_t),
		     sizeof(struct ais_t),
		     sizeof(struct compass_t),
		     sizeof(struct rawdata_t),
		     sizeof(collect->devices));
	    exit(0);
	case '?':
	case 'h':
	default:
	    (void)fputs("usage: libps [-u]\n", stderr);
	    exit(1);
	}
    }

    if (unpack_test) {
	unpack_unit_test();
	return 0;
    } else if ((collect = gps_open(NULL, 0)) == NULL) {
	(void)fputs("Daemon is not running.\n", stdout);
	exit(1);
    } else if (optind < argc) {
	gps_set_raw_hook(collect, dumpline);
	strlcpy(buf, argv[optind], BUFSIZ);
	strlcat(buf,"\n", BUFSIZ);
	gps_query(collect, buf);
	data_dump(collect, time(NULL));
    } else {
	int	tty = isatty(0);

	gps_set_raw_hook(collect, dumpline);
	if (tty)
	    (void)fputs("This is the gpsd exerciser.\n", stdout);
	for (;;) {
	    if (tty)
		(void)fputs("> ", stdout);
	    if (fgets(buf, sizeof(buf), stdin) == NULL) {
		if (tty)
		    putchar('\n');
		break;
	    }
	    collect->set = 0;
	    gps_query(collect, buf);
	    data_dump(collect, time(NULL));
	}
    }

    (void)gps_close(collect);
    return 0;
}

#endif /* TESTMAIN */


