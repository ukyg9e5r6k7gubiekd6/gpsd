/* gps.h -- interface of the gps library */

#include <sys/types.h>
#include <time.h>

#define MAXCHANNELS	12
#define GPS_TIMEOUT	5	/* Consider GPS connection loss after 5 sec */

struct life_t
/* lifetime structure to be associated with some piece of data */
{
    time_t	last_refresh;
    int		time_to_live;
    int		refreshes;
    int		changed;
};
#define INIT(stamp, now, tl)	stamp.time_to_live=tl; stamp.last_refresh=now
#define REFRESH(stamp)	stamp.last_refresh = time(NULL); stamp.refreshes++
#define FRESH(stamp, t) stamp.last_refresh + stamp.time_to_live >= t
#define SEEN(stamp) stamp.refreshes
#define CHANGED(stamp) stamp.changed

struct gps_data {
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
    double mag_var;		/* magnetic variation in degrees */
    double track;		/* course made good */
    struct life_t speed_stamp;

    /* status and precision of fix */
    int    status;
#define STATUS_NO_FIX	0
#define STATUS_FIX	1
#define STATUS_DGPS_FIX	2
    struct life_t status_stamp;
    int    mode;
#define MODE_NO_FIX	1
#define MODE_2D  	2
#define MODE_3D  	3
    struct life_t mode_stamp;
    double pdop;		/* Position dilution of precision, meters */
    double hdop;		/* Horizontal dilution of precision, meters */
    double vdop;		/* Vertical dilution of precision, meters */
    double separation;		/* geoidal separation */
    struct life_t fix_quality_stamp;

    /* satellite status */
    int in_view;		/* # of satellites in view */
    int satellites;		/* Number of satellites used in solution */
    int PRN[MAXCHANNELS];	/* PRN of satellite */
    int elevation[MAXCHANNELS];	/* elevation of satellite */
    int azimuth[MAXCHANNELS];	/* azimuth */
    int ss[MAXCHANNELS];	/* signal strength */
    int used[MAXCHANNELS];	/* used in solution */
    struct life_t satellite_stamp;

    /* Zodiac chipset channel status from PRWIZCH */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
    struct life_t signal_quality_stamp;

    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;

    int	sock;	/* server socket -- user most not modify this! */
};

/* gps.h ends here */
