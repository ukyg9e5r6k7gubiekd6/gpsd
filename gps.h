/* gps.h -- interface of the libgps library */

#include <sys/types.h>
#include <time.h>

#define MAXCHANNELS	12	/* maximum GPS channels (*not* satellites!) */

struct life_t
/* lifetime structure to be associated with some piece of data */
{
    time_t	last_refresh;
    int		changed;
};
#define INIT(stamp, now)	stamp.last_refresh=now
#define REFRESH(stamp)	stamp.last_refresh = time(NULL)
#define SEEN(stamp) stamp.last_refresh

struct gps_data_t {
    int	online;			/* 1 if GPS is on line, 0 if not */
    struct life_t online_stamp;

    char utc[20];		/* UTC date/time as "mm/dd/yy hh:mm:ss" */

    /* location */
    double latitude;		/* Latitude */
    double longitude;		/* Longitude */
    struct life_t latlon_stamp;
    double altitude;		/* Altitude in meters */
    struct life_t altitude_stamp;

    /* velocity */
    double speed;		/* Speed over ground, knots */
    struct life_t speed_stamp;
    double track;		/* Course made good */
    struct life_t track_stamp;
    double mag_var;		/* Magnetic variation in degrees */

    /* status of fix */
    int    status;		/* do we have a fix? */
#define STATUS_NO_FIX	0	/* no */
#define STATUS_FIX	1	/* yes, without DGPS */
#define STATUS_DGPS_FIX	2	/* yes, with DGPS */
    struct life_t status_stamp;
    int    mode;		/* mode of fix */
#define MODE_NO_FIX	1	/* none */
#define MODE_2D  	2	/* good for latitude/longitude */
#define MODE_3D  	3	/* good for altitude too */
    struct life_t mode_stamp;

    /* precision of fix */
    int satellites_used;	/* Number of satellites used in solution */
    int used[MAXCHANNELS];	/* used in last fix? */
    double pdop;		/* Position dilution of precision, meters */
    double hdop;		/* Horizontal dilution of precision, meters */
    double vdop;		/* Vertical dilution of precision, meters */
    double separation;		/* geoidal separation */
    struct life_t fix_quality_stamp;

    /* satellite status */
    int satellites;	/* # of satellites in view */
    int PRN[MAXCHANNELS];	/* PRNs of satellite */
    int elevation[MAXCHANNELS];	/* elevation of satellite */
    int azimuth[MAXCHANNELS];	/* azimuth */
    int ss[MAXCHANNELS];	/* signal strength */
    int part, await;		/* for tracking GSV parts */
    struct life_t satellite_stamp;

#ifdef PROCESS_PRWIZCH
    /*
     * Zodiac chipset channel status from PRWIZCH.
     * This is actually redundant with the SNRs in GPGSV,
     * and all known variants of the Zodiac chipsets issue GPGSV.
     */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
    struct life_t signal_quality_stamp;
#endif /* PROCESS_PRWIZCH */

    /* stuff after this point is private */
    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;

    void (*raw_hook)(char *buf);	/* raw-mode hook for GPS data */
};

int gps_open(struct gps_data_t *gpsdata, char *host, char *port);
int gps_close(int fd);
int gps_query(int fd, struct gps_data_t *gpsdata, char *requests);
int gps_poll(int fd, struct gps_data_t *gpsdata);
void gps_set_raw_hook(struct gps_data_t *gpsdata, void (*hook)(char *buf));

/* gps_open() error return values */
#define NL_NOSERVICE	-1	/* can't get service entry */
#define NL_NOHOST	-2	/* can't get host entry */
#define NL_NOPROTO	-3	/* can't get protocol entry */
#define NL_NOSOCK	-4	/* can't create socket */
#define NL_NOSOCKOPT	-5	/* error SETSOCKOPT SO_REUSEADDR */
#define NL_NOCONNECT	-6	/* can't connect to host */

#define DEFAULT_GPSD_PORT	"2497"	/* IANA assignment */

/* gps.h ends here */
