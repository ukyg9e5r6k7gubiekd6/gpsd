/* libgpsd.c -- client interface library for the gpsd daemon */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <gps.h>
#include <gpsd.h>

int gpsd_open(struct gps_data *gpsdata, int timeout, char *host, char *port)
/* open a connection to a gpsd daemon */
{
    int fd;
    time_t now;

    if (!host)
	host = "localhost";
    if (!port)
	port = "2947";

    if ((fd = netlib_connectTCP(host, port)) == -1)
	return (-1);

    now = time(NULL);
    INIT(gpsdata->latlon_stamp, now, timeout);
    INIT(gpsdata->altitude_stamp, now, timeout);
    INIT(gpsdata->track_stamp, now, timeout);
    INIT(gpsdata->speed_stamp, now, timeout);
    INIT(gpsdata->status_stamp, now, timeout);
    INIT(gpsdata->mode_stamp, now, timeout);
    gpsdata->mode = MODE_NO_FIX;

    return fd;
}

int gpsd_close(int fd)
/* close a gpsd connection */
{
    return close(fd);
}

static int gpsd_unpack(char *buf, struct gps_data *gpsdata)
/* unpack a daemon response into a status structure */
{
    char *sp, *tp;
    double d1, d2, d3;
    int i1, i2;

    for (sp = buf + 5; ; sp = tp+1)
    {
	if (!(tp = strchr(sp, ',')))
	    tp = strchr(sp, '\r');
	if (!tp) break;
	*tp = '\0';

	switch (*sp)
	{
	case 'A':
	    sscanf(sp, "A=%lf", &d1);
	    gpsdata->altitude_stamp.changed = (gpsdata->altitude != d1);
	    gpsdata->altitude = d1;
	    REFRESH(gpsdata->altitude_stamp);
	    break;
	case 'D':
	    strcpy(gpsdata->utc, sp+2);
	    break;
	case 'M':
	    i1 = atoi(sp+2);
	    gpsdata->mode_stamp.changed = (gpsdata->mode != i1);
	    gpsdata->mode = i1;
	    REFRESH(gpsdata->mode_stamp);
	    break;
	case 'P':
	    sscanf(sp, "P=%lf %lf", &d1, &d2);
	    gpsdata->latlon_stamp.changed = (gpsdata->latitude != d1) || (gpsdata->longitude != d2);
	    gpsdata->latitude = d1;
	    gpsdata->longitude = d2;
	    REFRESH(gpsdata->latlon_stamp);
	    break;
	case 'Q':
	    sscanf(sp, "Q=%d %lf %lf %lf", &i1, &d1, &d2, &d3);
	    gpsdata->fix_quality_stamp.changed = \
		(gpsdata->satellites_used != i1)
		|| (gpsdata->pdop != d1)
		|| (gpsdata->hdop != d2)
		|| (gpsdata->vdop != d3);
	    gpsdata->satellites_used = i1;
	    gpsdata->pdop = d1;
	    gpsdata->hdop = d2;
	    gpsdata->vdop = d3;
	    REFRESH(gpsdata->fix_quality_stamp);
	    break;
	case 'S':
	    i1 = atoi(sp+2);
	    gpsdata->status_stamp.changed = (gpsdata->status != i1);
	    gpsdata->status = i1;
	    REFRESH(gpsdata->status_stamp);
	    break;
	case 'T':
	    sscanf(sp, "T=%lf", &d1);
	    gpsdata->track_stamp.changed = (gpsdata->track != d1);
	    gpsdata->track = d1;
	    REFRESH(gpsdata->track_stamp);
	    break;
	case 'V':
	    sscanf(sp, "V=%lf", &d1);
	    gpsdata->speed_stamp.changed = (gpsdata->speed != d1);
	    gpsdata->speed = d1;
	    REFRESH(gpsdata->speed_stamp);
	    break;
	case 'X':
	    if (!strncmp(sp, "X=1", 3))
		gpsdata->online = 1;
	    else if (!strncmp(sp, "X=0", 3))
		gpsdata->online = 0;
	    break;
	case 'Y':
	    i1 = atoi(sp+2);
	    gpsdata->satellite_stamp.changed = (gpsdata->satellites != i1);
	    gpsdata->satellites = i1;
	    if (gpsdata->satellites)
	    {
		char *sp;
		int j, i3, i4;
		int PRN[MAXCHANNELS];
		int elevation[MAXCHANNELS];
		int azimuth[MAXCHANNELS];
		int ss[MAXCHANNELS];

		for (j = 0; j < gpsdata->satellites; j++) {
		    PRN[j]=elevation[j]=azimuth[j]=ss[j]=0;
		}
		sp = buf;
		for (j = 0; j < gpsdata->satellites; j++) {
		    sp = strchr(sp, ':') + 1;
		    sscanf(sp, "%d %d %d %d", &i1, &i2, &i3, &i4);
		    PRN[j] = i1;
		    elevation[j] = i2;
		    azimuth[j] = i3;
		    ss[j] = i4;
		}
		/*
		 * This won't catch the case where all values are identical
		 * but rearranged.  We can live with that.
		 */
		gpsdata->satellite_stamp.changed |= \
		    memcmp(gpsdata->PRN, PRN, sizeof(PRN)) ||
		    memcmp(gpsdata->elevation, elevation, sizeof(elevation)) ||
		    memcmp(gpsdata->azimuth, azimuth,sizeof(azimuth)) ||
		    memcmp(gpsdata->ss, ss, sizeof(ss));
		memcpy(gpsdata->PRN, PRN, sizeof(PRN));
		memcpy(gpsdata->elevation, elevation, sizeof(elevation));
		memcpy(gpsdata->azimuth, azimuth,sizeof(azimuth));
		memcpy(gpsdata->ss, ss, sizeof(ss));
	    }
	    REFRESH(gpsdata->satellite_stamp);
	    break;
	}
    }

    return gpsdata->latlon_stamp.changed 
	|| gpsdata->altitude_stamp.changed 
	|| gpsdata->speed_stamp.changed 
	|| gpsdata->track_stamp.changed 
	|| gpsdata->fix_quality_stamp.changed 
	|| gpsdata->fix_quality_stamp.changed 
	|| gpsdata->status_stamp.changed 
	|| gpsdata->mode_stamp.changed 
	|| gpsdata->satellite_stamp.changed 
#ifdef PROCESS_PRWIZCH
	|| gpsdata->signal_quality_stamp.changed
#endif /* PROCESS_PRWIZCH */
	;
}


int gpsd_query(int fd, char *requests, struct gps_data *gpsdata)
/* query a gpsd instance for new data */
{
    char	buf[BUFSIZE];

    if (write(fd, requests, strlen(requests)) <= 0)
	return -1;
    else if (read(fd, buf, sizeof(buf)-1) <= 0)
	return -1;

    return gpsd_unpack(buf, gpsdata);
}

#ifdef TESTMAIN

void gpscli_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    char buf[BUFSIZ];
    va_list ap;

    strcpy(buf, "gpsd: ");
    va_start(ap, fmt) ;
#ifdef HAVE_VSNPRINTF
    vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
#else
    vsprintf(buf + strlen(buf), fmt, ap);
#endif
    va_end(ap);

    fputs(buf, stderr);
}

void data_dump(struct gps_data *collect, time_t now)
{
    char *status_values[] = {"NO_FIX", "FIX", "DGPS_FIX"};
    char *mode_values[] = {"", "NO_FIX", "MODE_2D", "MODE_3D"};

    printf("utc: %s\n", collect->utc);
    if (collect->latlon_stamp.refreshes)
    {
	printf("P: lat/lon: %lf %lf ", collect->latitude, collect->longitude);
	printf("(lr=%ld, ttl=%d, refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->latlon_stamp.last_refresh,
	       collect->latlon_stamp.time_to_live,
	       collect->latlon_stamp.refreshes,
	       collect->latlon_stamp.changed,
	       FRESH(collect->latlon_stamp, now));
    }
    if (collect->altitude_stamp.refreshes)
    {
	printf("A: altitude: %lf ", collect->altitude);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->altitude_stamp.last_refresh,
	       collect->altitude_stamp.time_to_live,
	       collect->altitude_stamp.refreshes,
	       collect->altitude_stamp.changed,
	       FRESH(collect->altitude_stamp, now));
    }
    if (collect->speed_stamp.refreshes)
    {
	printf("V: speed: %lf ", collect->speed);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->speed_stamp.last_refresh,
	       collect->speed_stamp.time_to_live,
	       collect->speed_stamp.refreshes,
	       collect->speed_stamp.changed,
	       FRESH(collect->speed_stamp, now));
    }
    if (collect->track_stamp.refreshes)
    {
	printf("T: track: %lf ", collect->track);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->track_stamp.last_refresh,
	       collect->track_stamp.time_to_live,
	       collect->track_stamp.refreshes,
	       collect->track_stamp.changed,
	       FRESH(collect->track_stamp, now));
    }
    if (collect->status_stamp.refreshes)
    {
	printf("S: status: %d (%s) ", 
	       collect->status,status_values[collect->status]);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->status_stamp.last_refresh,
	       collect->status_stamp.time_to_live,
	       collect->status_stamp.refreshes,
	       collect->status_stamp.changed,
	       FRESH(collect->status_stamp, now));
    }
    if (collect->mode_stamp.refreshes)
    {
	printf("M: mode: %d (%s) ", collect->mode, mode_values[collect->mode]);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)",
	       collect->mode_stamp.last_refresh,
	       collect->mode_stamp.time_to_live,
	       collect->mode_stamp.refreshes,
	       collect->mode_stamp.changed,
	       FRESH(collect->mode_stamp, now));
    }
    if (collect->fix_quality_stamp.refreshes)
    {
	printf("Q: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf ",
	      collect->satellites_used, 
	      collect->pdop, collect->hdop, collect->vdop);
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->fix_quality_stamp.last_refresh,
	       collect->fix_quality_stamp.time_to_live,
	       collect->fix_quality_stamp.refreshes,
	       collect->fix_quality_stamp.changed,
	       FRESH(collect->fix_quality_stamp, now));
    }
    if (collect->satellite_stamp.refreshes)
    {
	int i;

	printf("satellites in view: %d\n", collect->satellites);
	for (i = 0; i < collect->satellites; i++) {
	    printf("    %2.2d: %2.2d %3.3d %3.3d\n", collect->PRN[i], collect->elevation[i], collect->azimuth[i], collect->ss[i]);
	}
	printf("(lr=%ld, ttl=%d. refreshes=%d, changed=%d, fresh=%d)\n",
	       collect->satellite_stamp.last_refresh,
	       collect->satellite_stamp.time_to_live,
	       collect->satellite_stamp.refreshes,
	       collect->satellite_stamp.changed,
	       FRESH(collect->satellite_stamp, now));
    }
}

main(int argc, char *argv[])
{
    struct gps_data collect;
    int fd;
    char buf[BUFSIZE];

    memset(&collect, '\0', sizeof(collect));
    fd = gpsd_open(&collect, GPS_TIMEOUT, NULL, 0);

    strcpy(buf, argv[1]);
    strcat(buf,"\n");
    gpsd_query(fd, buf, &collect);

    data_dump(&collect, time(NULL));

    gpsd_close(fd);
}

#endif /* TESTMAIN */
