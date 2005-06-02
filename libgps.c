/* libgps.c -- client interface library for the gpsd daemon */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>

#include "gpsd.h"

#ifdef __UNUSED__
/* 
 * check the environment to determine proper GPS units
 *
 * clients should only call this if no user preference on the command line or
 * Xresources
 *
 * return 0 - Use miles/feet
 *        1 - Use knots/feet
 *        2 - Use km/meters
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
int gpsd_units(void)
{
	char *envu = NULL;

 	if ((envu = getenv("GPSD_UNITS")) && *envu) {
		if (strcasecmp(envu, "imperial")) {
			return 0;
		}
		if (strcasecmp(envu, "nautical")) {
			return 1;
		}
		if (strcasecmp(envu, "metric")) {
			return 2;
		}
		/* unrecognized, ignore it */
	}
 	if (((envu = getenv("LC_MEASUREMENT")) && *envu) 
 	    || ((envu = getenv("LANG")) && *envu)) {
		if (   strstr(envu, "_US") 
		    || strcasecmp(envu, "C")
		    || strcasecmp(envu, "POSIX")) {
			return 0;
		}
		/* Other, must be metric */
		return 2;
	}
	/* TODO: allow a compile time default here */
	return 0;
}
#endif /* __UNUSED__ */

void gps_clear_fix(struct gps_fix_t *fixp)
/* stuff a fix structure with recognizable out-of-band values */
{
    fixp->time = TIME_NOT_VALID;
    fixp->mode = MODE_NOT_SEEN;
    fixp->track = TRACK_NOT_VALID;
    fixp->speed = SPEED_NOT_VALID;
    fixp->climb = SPEED_NOT_VALID;
    fixp->altitude = ALTITUDE_NOT_VALID;
    fixp->ept = UNCERTAINTY_NOT_VALID;
    fixp->eph = UNCERTAINTY_NOT_VALID;
    fixp->epv = UNCERTAINTY_NOT_VALID;
    fixp->epd = UNCERTAINTY_NOT_VALID;
    fixp->eps = UNCERTAINTY_NOT_VALID;
    fixp->epc = UNCERTAINTY_NOT_VALID;
}

struct gps_data_t *gps_open(const char *host, const char *port)
/* open a connection to a gpsd daemon */
{
    struct gps_data_t *gpsdata = (struct gps_data_t *)calloc(sizeof(struct gps_data_t), 1);

    if (!gpsdata)
	return NULL;
    if (!host)
	host = "localhost";
    if (!port)
	port = DEFAULT_GPSD_PORT;

    if ((gpsdata->gps_fd = netlib_connectsock(host, port, "tcp")) < 0) {
	errno = gpsdata->gps_fd;
	(void)free(gpsdata);
	return NULL;
    }

    gpsdata->status = STATUS_NO_FIX;
    gps_clear_fix(&gpsdata->fix);
    return gpsdata;
}

int gps_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
    int retval = close(gpsdata->gps_fd);
    if (gpsdata->gps_id)
	(void)free(gpsdata->gps_id);
	gpsdata->gps_id = NULL;
    if (gpsdata->gps_device) {
	(void)free(gpsdata->gps_device);
	gpsdata->gps_device = NULL;
    }
    if (gpsdata->devicelist) {
	int i;
	for (i = 0; i < gpsdata->ndevices; i++)
	    (void)free(gpsdata->devicelist[i]);
	(void)free(gpsdata->devicelist);
	gpsdata->devicelist = NULL;
	gpsdata->ndevices = -1;
    }    
    (void)free(gpsdata);
    return retval;
}

void gps_set_raw_hook(struct gps_data_t *gpsdata, 
		      void (*hook)(struct gps_data_t *, char *, int len, int level))
{
    gpsdata->raw_hook = hook;
}

static void gps_unpack(char *buf, struct gps_data_t *gpsdata)
/* unpack a daemon response into a status structure */
{
    char *ns, *sp, *tp;
    int i;

    for (ns = buf; ns; ns = strstr(ns+1, "GPSD")) {
	if (strncmp(ns, "GPSD", 4) == 0) {
	    for (sp = ns + 5; ; sp = tp) {
		tp = sp + strcspn(sp, ",\r\n");
		if (!*tp) break;
		*tp = '\0';

		switch (*sp) {
		case 'A':
		    if (sp[2] == '?') {
			    gpsdata->fix.altitude = ALTITUDE_NOT_VALID;
		    } else {
		        (void)sscanf(sp, "A=%lf", &gpsdata->fix.altitude);
		        gpsdata->set |= ALTITUDE_SET;
		    }
		    break;
		case 'B':
		    if (sp[2] == '?') {
			gpsdata->baudrate = gpsdata->stopbits = 0;
		    } else
			(void)sscanf(sp, "B=%d %*d %*s %u", 
			       &gpsdata->baudrate, &gpsdata->stopbits);
		    break;
		case 'C':
		    if (sp[2] == '?')
			gpsdata->cycle = 0;
		    else
			(void)sscanf(sp, "C=%d", &gpsdata->cycle);
		    break;
		case 'D':
		    if (sp[2] == '?') 
			gpsdata->fix.time = TIME_NOT_VALID;
		    else {
			gpsdata->fix.time = iso8601_to_unix(sp+2);
			gpsdata->set |= TIME_SET;
		    }
		    break;
		case 'E':
		    if (sp[2] == '?') {
			   gpsdata->epe = UNCERTAINTY_NOT_VALID;
			   gpsdata->fix.eph = UNCERTAINTY_NOT_VALID;
			   gpsdata->fix.epv = UNCERTAINTY_NOT_VALID;
		    } else {
		        (void)sscanf(sp, "E=%lf %lf %lf", 
			   &gpsdata->epe,&gpsdata->fix.eph,&gpsdata->fix.epv);
		        gpsdata->set |= HERR_SET| VERR_SET | PERR_SET;
		    }
		    break;
		case 'F':
		    if (sp[2] == '?') 
			gpsdata->gps_device = NULL;
		    else {
			if (gpsdata->gps_device)
			    free(gpsdata->gps_id);
			gpsdata->gps_device = strdup(sp+2);
			gpsdata->set |= DEVICE_SET;
		    }
		    break;
		case 'I':
		    if (sp[2] == '?') 
			gpsdata->gps_id = NULL;
		    else {
			if (gpsdata->gps_id)
			    free(gpsdata->gps_id);
			gpsdata->gps_id = strdup(sp+2);
			gpsdata->set |= DEVICEID_SET;
		    }
		    break;
		case 'K':
		    if (gpsdata->devicelist) {
			for (i = 0; i < gpsdata->ndevices; i++)
			    (void)free(gpsdata->devicelist[i]);
			(void)free(gpsdata->devicelist);
			gpsdata->devicelist = NULL;
			gpsdata->ndevices = -1;
			gpsdata->set |= DEVICELIST_SET;
		    }    
		    if (sp[2] != '?') {
			gpsdata->ndevices = (int)strtol(sp+2, &sp, 10);
			gpsdata->devicelist = (char **)calloc(
			    gpsdata->ndevices,
			    sizeof(char **));
			gpsdata->devicelist[i=0] = strtok_r(sp+2, " \r\n", &ns);
			while ((sp = strtok_r(NULL, " \r\n",  &ns)))
			    gpsdata->devicelist[++i] = strdup(sp);
			gpsdata->set |= DEVICELIST_SET;
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
			gpsdata->driver_mode = 0;
		    else
			gpsdata->driver_mode = (unsigned)atoi(sp+2);
		    break;
		case 'O':
		    if (sp[2] == '?') {
			gpsdata->set = MODE_SET | STATUS_SET;
			gps_clear_fix(&gpsdata->fix);
		    } else {
			struct gps_fix_t nf;
			char tag[MAXTAGLEN+1], alt[20];
			char eph[20], epv[20], track[20],speed[20], climb[20];
			char epd[20], eps[20], epc[20];
			int st = sscanf(sp+2, 
			       "%6s %lf %lf %lf %lf %s %s %s %s %s %s %s %s %s",
				tag, &nf.time, &nf.ept, 
				&nf.latitude, &nf.longitude,
			        alt, eph, epv, track, speed, climb,
			        epd, eps, epc);
			if (st == 14) {
#define DEFAULT(val, def) (val[0] == '?') ? (def) : atof(val)
			    nf.altitude = DEFAULT(alt, ALTITUDE_NOT_VALID);
			    nf.eph = DEFAULT(eph, UNCERTAINTY_NOT_VALID);
			    nf.epv = DEFAULT(epv, UNCERTAINTY_NOT_VALID);
			    nf.track = DEFAULT(track, TRACK_NOT_VALID);
			    nf.speed = DEFAULT(speed, SPEED_NOT_VALID);
			    nf.climb = DEFAULT(climb, SPEED_NOT_VALID);
			    nf.epd = DEFAULT(epd, UNCERTAINTY_NOT_VALID);
			    nf.eps = DEFAULT(eps, UNCERTAINTY_NOT_VALID);
			    nf.epc = DEFAULT(epc, UNCERTAINTY_NOT_VALID);
#undef DEFAULT
			    nf.mode = (alt[0] == '?') ? MODE_2D : MODE_3D;
			    if (nf.mode == MODE_3D)
				gpsdata->set |= ALTITUDE_SET | CLIMB_SET;
			    if (nf.eph != UNCERTAINTY_NOT_VALID)
				gpsdata->set |= HERR_SET;
			    if (nf.epv != UNCERTAINTY_NOT_VALID)
				gpsdata->set |= VERR_SET;
			    if (nf.track != TRACK_NOT_VALID)
				gpsdata->set |= TRACK_SET | SPEED_SET;
			    if (nf.eps != UNCERTAINTY_NOT_VALID)
				gpsdata->set |= SPEEDERR_SET;
			    if (nf.epc != UNCERTAINTY_NOT_VALID)
				gpsdata->set |= CLIMBERR_SET;

			    gpsdata->fix = nf;
			    (void)strcpy(gpsdata->tag, tag);
			    gpsdata->set = TIME_SET|TIMERR_SET|LATLON_SET|MODE_SET;
			}
		    }
		    break;
		case 'P':
		    if (sp[2] == '?') {
			   gpsdata->fix.latitude = LATITUDE_NOT_VALID;
			   gpsdata->fix.longitude = LONGITUDE_NOT_VALID;
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
		        gpsdata->fix.track = TRACK_NOT_VALID;
		    } else {
		        (void)sscanf(sp, "T=%lf", &gpsdata->fix.track);
		        gpsdata->set |= TRACK_SET;
		    }
		    break;
		case 'U':
		    if (sp[2] == '?') {
		        gpsdata->fix.climb = SPEED_NOT_VALID;
		    } else {
		        (void)sscanf(sp, "U=%lf", &gpsdata->fix.climb);
		        gpsdata->set |= CLIMB_SET;
		    }
		    break;
		case 'V':
		    if (sp[2] == '?') {
		        gpsdata->fix.speed = SPEED_NOT_VALID;
		    } else {
		        (void)sscanf(sp, "V=%lf", &gpsdata->fix.speed);
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
			int j, i1, i2, i3, i4, i5;
			int PRN[MAXCHANNELS];
			int elevation[MAXCHANNELS], azimuth[MAXCHANNELS];
			int ss[MAXCHANNELS], used[MAXCHANNELS];
			char tag[21], timestamp[21];

			(void)sscanf(sp, "Y=%20s %20s %d ", 
			       tag, timestamp, &gpsdata->satellites);
			(void)strncpy(gpsdata->tag, tag, MAXTAGLEN);
			if (timestamp[0] != '?') {
			    gpsdata->sentence_time = atof(timestamp);
			    gpsdata->set |= TIME_SET;
			}
			for (j = 0; j < gpsdata->satellites; j++) {
			    PRN[j]=elevation[j]=azimuth[j]=ss[j]=used[j]=0;
			}
			for (j = 0; j < gpsdata->satellites; j++) {
			    sp = strchr(sp, ':') + 1;
			    (void)sscanf(sp, "%d %d %d %d %d", &i1, &i2, &i3, &i4, &i5);
			    PRN[j] = i1;
			    elevation[j] = i2; azimuth[j] = i3;
			    ss[j] = i4; used[j] = i5;
			}
#ifdef __UNUSED__
			/*
			 * This won't catch the case where all values are identical
			 * but rearranged.  We can live with that.
			 */
			gpsdata->satellite_stamp.changed |= \
			    memcmp(gpsdata->PRN, PRN, sizeof(PRN)) ||
			    memcmp(gpsdata->elevation, elevation, sizeof(elevation)) ||
			    memcmp(gpsdata->azimuth, azimuth,sizeof(azimuth)) ||
			    memcmp(gpsdata->ss, ss, sizeof(ss)) ||
			    memcmp(gpsdata->used, used, sizeof(used));
#endif /* UNUSED */
			memcpy(gpsdata->PRN, PRN, sizeof(PRN));
			memcpy(gpsdata->elevation, elevation, sizeof(elevation));
			memcpy(gpsdata->azimuth, azimuth,sizeof(azimuth));
			memcpy(gpsdata->ss, ss, sizeof(ss));
			memcpy(gpsdata->used, used, sizeof(used));
		    }
		    gpsdata->set |= SATELLITE_SET;
		    break;
		case 'Z':
		    (void)sscanf(sp, "Z=%d", &gpsdata->profiling);
		    break;
		case '$':
		    (void)sscanf(sp, "$=%s %d %lf %lf %lf %lf %lf %lf", 
			   gpsdata->tag,
			   &gpsdata->sentence_length,
			   &gpsdata->fix.time, 
			   &gpsdata->d_xmit_time, 
			   &gpsdata->d_recv_time, 
			   &gpsdata->d_decode_time, 
			   &gpsdata->poll_time, 
			   &gpsdata->emit_time);
		    break;
		}
	    }
	}
    }

    if (gpsdata->raw_hook)
	gpsdata->raw_hook(gpsdata, buf, strlen(buf),  1);
    if (gpsdata->thread_hook)
	gpsdata->thread_hook(gpsdata, buf, strlen(buf), 1);
}

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

int gps_query(struct gps_data_t *gpsdata, const char *requests)
/* query a gpsd instance for new data */
{
    if (write(gpsdata->gps_fd, requests, strlen(requests)) <= 0)
	return -1;
    return gps_poll(gpsdata);
}

static void *poll_gpsd(void *args) 
/* helper for the thread launcher */
{
    int oldtype, oldstate;
    int res;
    struct gps_data_t *gpsdata;

    /* set thread parameters */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&oldstate);
    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&oldtype); /* we want to be canceled also when blocked on gps_poll() */
    gpsdata = (struct gps_data_t *) args;
    do {
	res = gps_poll(gpsdata); /* this is not actually polling */
    } while 
	(res == 0);
    /* if we are here an error occured with gpsd */
    return NULL;
}

int gps_set_callback(struct gps_data_t *gpsdata, 
		     void (*callback)(struct gps_data_t *sentence, char *buf, int len, int level),
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
    return pthread_create(handler,NULL,poll_gpsd,(void*)gpsdata);
}

int gps_del_callback(struct gps_data_t *gpsdata, pthread_t *handler)
/* delete asynchronous callback and kill its thread */
{
    int res;
    res = pthread_cancel(*handler);	/* we cancel the whole thread */
    gpsdata->thread_hook = NULL;	/* finally we cancel the callback */
    if (res == 0) 			/* tell gpsd to stop sending data */
	(void)gps_query(gpsdata,"w-\n");	/* disable watcher mode */
    return res;
}

#ifdef TESTMAIN
/*
 * A simple command-line exerciser for the library.
 * Not really useful for anything but debugging.
 */
void data_dump(struct gps_data_t *collect, time_t now)
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
    if (collect->fix.track != TRACK_NOT_VALID)
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
	    printf("    %2.2d: %2.2d %3.3d %3.3d %c\n", collect->PRN[i], collect->elevation[i], collect->azimuth[i], collect->ss[i], collect->used[i]? 'Y' : 'N');
	}
    }
    if (collect->set & DEVICE_SET)
	printf("Device is %s\n", collect->gps_device);
    if (collect->set & DEVICEID_SET)
	printf("GPSD ID is %s\n", collect->gps_id);
    if (collect->set & DEVICELIST_SET) {
	int i;
	printf("%d devices:\n", collect->ndevices);
	for (i = 0; i < collect->ndevices; i++) {
	    printf("%d: %s\n", collect->ndevices, collect->devicelist[i]);
	}
    }
	
}

static void dumpline(struct gps_data_t *ud UNUSED, char *buf)
{
    puts(buf);
}

#include <getopt.h>

main(int argc, char *argv[])
{
    struct gps_data_t *collect;
    char buf[BUFSIZ], *device = NULL;
    int option;

    collect = gps_open(NULL, 0);
    gps_set_raw_hook(collect, dumpline);
    if (optind < argc) {
	strcpy(buf, argv[optind]);
	strcat(buf,"\n");
	gps_query(collect, buf);
	data_dump(collect, time(NULL));
    } else {
	int	tty = isatty(0);

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
}

#endif /* TESTMAIN */


