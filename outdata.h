#include <time.h>

#define C_LATLON	1
#define C_SAT		2
#define C_ZCH		4
#define C_STATUS	8
#define C_MODE		16

#define MAXSATS       12

struct OUTDATA {
    time_t last_update;		/* When we got last data from GPS receiver */
				/* This will be copied to the ts_* members */

    long cmask;			/* Change flag, set by backend. Reset by app. */

    char utc[20];		/* UTC date/time as "mm/dd/yy hh:mm:ss" */
    time_t ts_utc;		/* UTC last updated time stamp */


    /* location */
    double latitude;		/* Latitude/longitude in format "d.ddddd" */
    double longitude;
    time_t ts_latlon;		/* Update time stamp */
    int    v_latlon;		/* Valid for v_latlon seconds */

    double altitude;		/* Altitude in meters */
    time_t ts_alt;
    int    v_alt;

    /* velocity */
    double speed;		/* Speed over ground, knots */
    time_t ts_speed;
    int    v_speed;

    /* status and precision of fix */
    int    status;		/* 0 = no fix, 1 = fix, 2 = dgps fix */
    int    mode;		/* 1 = no fix, 2 = 2D, 3 = 3D */
    time_t ts_mode;
    time_t ts_status;
    int    v_mode;
    int    v_status;

    double pdop;		/* Position dilution of precision */
    double hdop;		/* Horizontal dilution of precision */
    double vdop;		/* Vertical dilution of precision */

    /* satellite status */
    int in_view;		/* # of satellites in view */
    int satellites;		/* Number of satellites used in solution */
    int PRN[12];		/* PRN of satellite */
    int elevation[12];		/* elevation of satellite */
    int azimuth[12];		/* azimuth */
    int ss[12];			/* signal strength */
    int used[12];		/* used in solution */

    /* Zodiac chipset channel status from PRWIZCH */
    int Zs[12];			/* satellite PRNs */
    int Zv[12];			/* signal values (0-7) */

    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;

    double separation;
    double mag_var;
    double course;

    int seen[12];
    int valid[12];		/* signal valid */
};

