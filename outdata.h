#include <time.h>

#define C_LATLON	1
#define C_SAT		2
#define C_ZCH		4

struct OUTDATA {
    int fdin;
    int fdout;

    time_t last_update;		/* When we got last data from GPS receiver */
				/* This will be copied to the ts_* members */

    long cmask;			/* Change flag, set by backend. Reset by app. */

    char utc[20];		/* UTC date / time in format "mm/dd/yy hh:mm:ss" */
    time_t ts_utc;		/* UTC last updated time stamp */

    double latitude;		/* Latitude and longitude in format "d.ddddd" */
    double longitude;
    time_t ts_latlon;		/* Update time stamp */

    double altitude;		/* Altitude in meters */
    time_t ts_alt;

    double speed;		/* Speed over ground, knots */
    time_t ts_speed;


    int status;			/* 0 = no fix, 1 = fix, 2 = dgps fix */
    int mode;			/* 1 = no fix, 2 = 2D, 3 = 3D */

    double pdop;		/* Position dilution of precision */
    double hdop;		/* Horizontal dilution of precision */
    double vdop;		/* Vertical dilution of precision */

    int in_view;		/* # of satellites in view */
    int satellites;		/* Number of satellites used in solution */
    int PRN[12];		/* PRN of satellite */
    int elevation[12];		/* elevation of satellite */
    int azimuth[12];		/* azimuth */
    int ss[12];			/* signal strength */
    int used[12];		/* used in solution */

    int ZCHseen;		/* flag */
    int Zs[12];			/* for the rockwell PRWIZCH */
    int Zv[12];			/*                  value */

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

