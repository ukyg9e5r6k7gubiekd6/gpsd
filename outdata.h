#include <time.h>

#define C_LATLON	1
#define C_SAT		2
#define C_ZCH		4
#define C_STATUS	8
#define C_MODE		16

#define MAXSATS       12

struct life_t
/* lifetime structure to be associated with some piece of data */
{
    time_t	last_refresh;
    int		time_to_live;
};
#define REFRESH(stamp)	stamp.last_refresh = time(NULL)
#define FRESH(stamp, t) stamp.last_refresh + stamp.time_to_live >= t
#define REVOKE(stamp)	stamp.last_refresh = 0

struct OUTDATA {
    long cmask;			/* Change flag, set by backend. Reset by app. */

    char utc[20];		/* UTC date/time as "mm/dd/yy hh:mm:ss" */
    time_t ts_utc;		/* UTC last updated time stamp */


    /* location */
    double latitude;		/* Latitude/longitude in format "d.ddddd" */
    double longitude;
    struct life_t latlon_stamp;

    double altitude;		/* Altitude in meters */
    struct life_t altitude_stamp;

    /* velocity */
    double speed;		/* Speed over ground, knots */
    struct life_t speed_stamp;

    /* status and precision of fix */
    int    status;
#define STATUS_NO_FIX	0
#define STATUS_FIX	1
#define STATUS_DGPS_FIX	2
    int    mode;
#define MODE_NO_FIX	1
#define MODE_2D  	2
#define MODE_3D  	3
    struct life_t mode_stamp;
    struct life_t status_stamp;

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

void report(int d, const char *fmt, ...);
