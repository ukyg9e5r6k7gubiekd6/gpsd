/* libgps.c -- client interface library for the gpsd daemon */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "gps.h"
#include "gpsd.h"

struct gps_data_t *gps_open(char *host, char *port)
/* open a connection to a gpsd daemon */
{
    time_t now;
    struct gps_data_t *gpsdata = (struct gps_data_t *)calloc(sizeof(struct gps_data_t), 1);

    if (!gpsdata)
	return NULL;

    if (!host)
	host = "localhost";
    if (!port)
	port = DEFAULT_GPSD_PORT;

    if ((gpsdata->gps_fd = netlib_connectsock(host, port, "tcp")) < 0)
    {
	errno = gpsdata->gps_fd;
	return NULL;
    }

    now = time(NULL);
    INIT(gpsdata->online_stamp, now);
    INIT(gpsdata->latlon_stamp, now);
    INIT(gpsdata->altitude_stamp, now);
    INIT(gpsdata->track_stamp, now);
    INIT(gpsdata->speed_stamp, now);
    INIT(gpsdata->status_stamp, now);
    INIT(gpsdata->mode_stamp, now);
    INIT(gpsdata->fix_quality_stamp, now);
    INIT(gpsdata->satellite_stamp, now);
    gpsdata->mode = MODE_NO_FIX;

    return gpsdata;
}

int gps_close(struct gps_data_t *gpsdata)
/* close a gpsd connection */
{
    return close(gpsdata->gps_fd);
}

void gps_set_raw_hook(struct gps_data_t *gpsdata, void (*hook)(char *buf))
{
    gpsdata->raw_hook = hook;
}

static int gps_unpack(char *buf, struct gps_data_t *gpsdata)
/* unpack a daemon response into a status structure */
{
    char *sp, *tp;
    double d1, d2, d3;
    int i1, i2;

    if (!strncmp(buf, "GPSD", 4))
    {
	for (sp = buf + 5; ; sp = tp+1)
	{
	    if (!(tp = strchr(sp, ',')))
		tp = strchr(sp, '\r');
	    if (!tp) break;
	    *tp = '\0';

	    if (sp[2] == '?')
		continue;

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
		{
		    gpsdata->online_stamp.changed = gpsdata->online != 1;
		    gpsdata->online = 1;
		    REFRESH(gpsdata->online_stamp);
		}
		else if (!strncmp(sp, "X=0", 3))
		{
		    gpsdata->online_stamp.changed = gpsdata->online != 0;
		    gpsdata->online = 0;
		    REFRESH(gpsdata->online_stamp);
		}
		break;
	    case 'Y':
		i1 = atoi(sp+2);
		gpsdata->satellite_stamp.changed = (gpsdata->satellites != i1);
		gpsdata->satellites = i1;
		if (gpsdata->satellites)
		{
		    int j, i3, i4, i5;
		    int PRN[MAXCHANNELS];
		    int elevation[MAXCHANNELS];
		    int azimuth[MAXCHANNELS];
		    int ss[MAXCHANNELS];
		    int used[MAXCHANNELS];

		    for (j = 0; j < gpsdata->satellites; j++) {
			PRN[j]=elevation[j]=azimuth[j]=ss[j]=used[j]=0;
		    }
		    for (j = 0; j < gpsdata->satellites; j++) {
			sp = strchr(sp, ':') + 1;
			sscanf(sp, "%d %d %d %d %d", &i1, &i2, &i3, &i4, &i5);
			PRN[j] = i1;
			elevation[j] = i2;
			azimuth[j] = i3;
			ss[j] = i4;
			used[j] = i5;
		    }
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
		    memcpy(gpsdata->PRN, PRN, sizeof(PRN));
		    memcpy(gpsdata->elevation, elevation, sizeof(elevation));
		    memcpy(gpsdata->azimuth, azimuth,sizeof(azimuth));
		    memcpy(gpsdata->ss, ss, sizeof(ss));
		    memcpy(gpsdata->used, used, sizeof(used));
		}
		REFRESH(gpsdata->satellite_stamp);
		break;
	    }
	}
    }

    if (gpsdata->raw_hook)
	gpsdata->raw_hook(buf);

    return gpsdata->online_stamp.changed
	|| gpsdata->latlon_stamp.changed 
	|| gpsdata->altitude_stamp.changed 
	|| gpsdata->speed_stamp.changed 
	|| gpsdata->track_stamp.changed 
	|| gpsdata->fix_quality_stamp.changed 
	|| gpsdata->fix_quality_stamp.changed 
	|| gpsdata->status_stamp.changed 
	|| gpsdata->mode_stamp.changed 
	|| gpsdata->satellite_stamp.changed 
	;
}

int gps_poll(struct gps_data_t *gpsdata)
/* wait for and read data being streamed from the daemon */ 
{
    char	buf[BUFSIZE];
    int		n;

    /* the daemon makes sure that every read is NUL-terminated */
    if ((n = read(gpsdata->gps_fd, buf, sizeof(buf)-1)) <= 0)
	return -1;
    buf[n] = '\0';

    return gps_unpack(buf, gpsdata);
}

int gps_query(struct gps_data_t *gpsdata, char *requests)
/* query a gpsd instance for new data */
{
    if (write(gpsdata->gps_fd, requests, strlen(requests)) <= 0)
	return -1;
    return gps_poll(gpsdata);
}

#ifdef TESTMAIN
/*
 * A simple command-line exerciser for the library.
 * Not meant to be installed in system directories,
 * as it isn't really useful for anything but debugging.
 * Build with:
 *    cc -o libgps -DTESTMAIN libgps.c .libs/libgps.a
 */

void data_dump(struct gps_data_t *collect, time_t now)
{
    char *status_values[] = {"NO_FIX", "FIX", "DGPS_FIX"};
    char *mode_values[] = {"", "NO_FIX", "MODE_2D", "MODE_3D"};

    if (collect->online_stamp.changed)
	printf("online: %d\n", collect->online);
    if (collect->latlon_stamp.changed)
    {
	printf("P: lat/lon: %lf %lf", 
	       collect->latitude, collect->longitude);
	printf("(lr=%ld, changed=%d)\n",
	       collect->latlon_stamp.last_refresh,
	       collect->latlon_stamp.changed);
    }
    if (collect->altitude_stamp.changed)
    {
	printf("A: altitude: %lf ", collect->altitude);
	printf("(lr=%ld, changed=%d)\n",
	       collect->altitude_stamp.last_refresh,
	       collect->altitude_stamp.changed);
    }
    if (collect->speed_stamp.changed)
    {
	printf("V: speed: %lf ", collect->speed);
	printf("(lr=%ld, changed=%d)\n",
	       collect->speed_stamp.last_refresh,
	       collect->speed_stamp.changed);
    }
    if (collect->track_stamp.changed)
    {
	printf("T: track: %lf ", collect->track);
	printf("(lr=%ld, changed=%d)\n",
	       collect->track_stamp.last_refresh,
	       collect->track_stamp.changed);
    }
    if (collect->status_stamp.changed)
    {
	printf("S: status: %d (%s) ", 
	       collect->status,status_values[collect->status]);
	printf("(lr=%ld, changed=%d)\n",
	       collect->status_stamp.last_refresh,
	       collect->status_stamp.changed);
    }
    if (collect->mode_stamp.changed)
    {
	printf("M: mode: %d (%s) ", collect->mode, mode_values[collect->mode]);
	printf("(lr=%ld, changed=%d)",
	       collect->mode_stamp.last_refresh,
	       collect->mode_stamp.changed);
    }
    if (collect->fix_quality_stamp.changed)
    {
	printf("Q: satellites %d, pdop=%lf, hdop=%lf, vdop=%lf ",
	      collect->satellites_used, 
	      collect->pdop, collect->hdop, collect->vdop);
	printf("(lr=%ld, changed=%d)\n",
	       collect->fix_quality_stamp.last_refresh,
	       collect->fix_quality_stamp.changed);
    }
    if (collect->satellite_stamp.changed)
    {
	int i;

	printf("Y: satellites in view: %d\n", collect->satellites);
	for (i = 0; i < collect->satellites; i++) {
	    printf("    %2.2d: %2.2d %3.3d %3.3d %c\n", collect->PRN[i], collect->elevation[i], collect->azimuth[i], collect->ss[i], collect->used[i]? 'Y' : 'N');
	}
	printf("(lr=%ld, changed=%d)\n",
	       collect->satellite_stamp.last_refresh,
	       collect->satellite_stamp.changed);
    }
}

static void dumpline(char *buf)
{
    fputs(buf, stdout);
    putchar('\n');
}

main(int argc, char *argv[])
{
    struct gps_data_t *collect;
    char buf[BUFSIZE];

    memset(&collect, '\0', sizeof(collect));
    collect = gps_open(NULL, 0);

    gps_set_raw_hook(collect, dumpline);
    if (argc > 1)
    {
	strcpy(buf, argv[1]);
	strcat(buf,"\n");
	gps_query(collect, buf);
	data_dump(collect, time(NULL));
    }
    else
    {
	int	tty = isatty(0);

	if (tty)
	    fputs("This is the gpsd exerciser.\n", stdout);
	for (;;)
	{
	    if (tty)
		fputs("> ", stdout);
	    if (fgets(buf, sizeof(buf), stdin) == NULL)
	    {
		if (tty)
		    putchar('\n');
		break;
	    }
	    if (!gps_query(collect, buf))
		fputs("No changes.\n", stdout);
	    data_dump(collect, time(NULL));
	}
    }

    gps_close(collect);
}

#endif /* TESTMAIN */
