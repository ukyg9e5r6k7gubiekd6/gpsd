/* libgpsd.c -- client interface library for the gpsd daemon */
#include <stdio.h>
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

int gpsd_query(int fd, char *requests, struct gps_data *gpsdata)
/* query a gpsd instance for new data */
{
    char buf[BUFSIZE], *sp, *tp;
    double d1, d2, d3;
    int i1, i2;

    if (write(fd, requests, strlen(requests)) <= 0)
	return -1;
    else if (read(fd, buf, sizeof(buf)-1) <= 0)
	return -1;

    for (sp = buf + 5; ; sp = tp+1)
    {
	if (!(tp = strchr(sp, ',')))
	    tp = strchr(sp, '\r');
	if (!tp) break;
	*tp = '\0';
	printf("part: %s\n", sp);

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
		for (j = 0; j < gpsdata->satellites; j++) {
		    sp = strchr(sp, ' ') + 1;
		    sscanf(sp, "%d %d %d %d", i1, i2, i3, i4);
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

    return 0;
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

void data_dump(struct gps_data *collect)
{
    char *status_values[] = {"NO_FIX", "FIX", "DGPS_FIX"};
    char *mode_values[] = {"", "NO_FIX", "MODE_2D", "MODE_3D"};

    printf("utc: %s\n", collect->utc);
    if (collect->latlon_stamp.refreshes)
    {
	printf("lat/lon: %lf %lf\n", collect->latitude, collect->longitude);
	printf("latlon_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->latlon_stamp.last_refresh,
	       collect->latlon_stamp.time_to_live,
	       collect->latlon_stamp.refreshes,
	       collect->latlon_stamp.changed);
    }
    if (collect->altitude_stamp.refreshes)
    {
	printf("altitude: %lf\n", collect->altitude);
	printf("altitude_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->altitude_stamp.last_refresh,
	       collect->altitude_stamp.time_to_live,
	       collect->altitude_stamp.refreshes,
	       collect->altitude_stamp.changed);
    }
    if (collect->speed_stamp.refreshes)
    {
	printf("speed: %lf\n", collect->speed);
	printf("speed_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->speed_stamp.last_refresh,
	       collect->speed_stamp.time_to_live,
	       collect->speed_stamp.refreshes,
	       collect->speed_stamp.changed);
    }
    if (collect->track_stamp.refreshes)
    {
	printf("track: %lf\n", collect->track);
	printf("track_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->track_stamp.last_refresh,
	       collect->track_stamp.time_to_live,
	       collect->track_stamp.refreshes,
	       collect->track_stamp.changed);
    }
    if (collect->status_stamp.refreshes)
    {
	printf("status: %d (%s)\n", 
	       collect->status,status_values[collect->status]);
	printf("status_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->status_stamp.last_refresh,
	       collect->status_stamp.time_to_live,
	       collect->status_stamp.refreshes,
	       collect->status_stamp.changed);
    }
    if (collect->mode_stamp.refreshes)
    {
	printf("mode: %d (%s)\n", collect->mode, mode_values[collect->mode]);
	printf("mode_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->mode_stamp.last_refresh,
	       collect->mode_stamp.time_to_live,
	       collect->mode_stamp.refreshes,
	       collect->mode_stamp.changed);
    }
    if (collect->fix_quality_stamp.refreshes)
    {
	printf("satellites: %d, pdop=%lf, hdop=%lf, vdop=%lf\n",
	      collect->satellites_used, 
	      collect->pdop, collect->hdop, collect->vdop);
	printf("fix_quality_stamp: lr=%ld, ttl=%d. refreshes=%d, changed=%d\n",
	       collect->fix_quality_stamp.last_refresh,
	       collect->fix_quality_stamp.time_to_live,
	       collect->fix_quality_stamp.refreshes,
	       collect->fix_quality_stamp.changed);
    }
    if (collect->satellite_stamp.refreshes)
    {
	int i;

	printf("satellites in view: %d\n", collect->satellites);
	for (i = 0; i < MAXCHANNELS; i++) {
	    printf("    %2d: %2d %2d %d\n", collect->PRN[i]);
	    printf("    %2d: %2d %2d %d\n", collect->elevation[i]);
	    printf("    %2d: %2d %2d %d\n", collect->azimuth[i]);
	    printf("    %2d: %2d %2d %d\n", collect->ss[i]);
	}
    }
}

main(int argc, char *argv[])
{
    struct gps_data collect;
    int fd;

    memset(&collect, '\0', sizeof(collect));
    fd = gpsd_open(&collect, GPS_TIMEOUT, NULL, 0);

    gpsd_query(fd, "PA\n", &collect);

    data_dump(&collect);

    gpsd_close(fd);
}

#endif /* TESTMAIN */
