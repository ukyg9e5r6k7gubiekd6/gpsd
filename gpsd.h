/* gpsd.h -- fundamental types and structures for the GPS daemon */

#include <sys/types.h>
#include <time.h>

#define MAXCHANNELS	12

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

struct OUTDATA {
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
    double course;		/* course made good */
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
};

#define BUFSIZE	4096	/* longer than max-length NMEA sentence (82 chars) */

#define GPGLL "GPGLL"
#define GPVTG "GPVTG"
#define GPGGA "GPGGA"
#define GPGSA "GPGSA"
#define GPGSV "GPGSV"
#define GPRMC "GPRMC"
#define PRWIZCH "PRWIZCH"
#define PMGNST "PMGNST"

struct longlat_t
/* This structure is used to initialize some older GPS units */
{
    char *latitude;
    char *longitude;
    char latd;
    char lond;
};

struct gpsd_t;

struct gps_type_t
/* GPS method table, describes how to talk to a particular GPS type */
{
    char typekey, *typename;
    void (*initializer)(struct gpsd_t *session);
    int (*handle_input)(struct gpsd_t *session);
    int (*rctm_writer)(struct gpsd_t *session, char *rtcmbuf, int rtcmbytes);
    void (*wrapup)(struct gpsd_t *session);
    int baudrate;
};

struct gpsd_t
/* session object, encapsulates all global state */
{
    struct gps_type_t *device_type;
    struct longlat_t initpos;
    struct OUTDATA gNMEAdata;
    char *gps_device;	/* where to find the GPS */
    int fdin;		/* input fd from GPS */
    int fdout;		/* output fd to GPS */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    void (*raw_hook)(char *buf);	/* raw-mode hook for GPS data */
    int debug;		/* debug verbosity level */
};

/* here are the available GPS drivers */
extern struct gps_type_t nmea;
extern struct gps_type_t tripmate;
extern struct gps_type_t earthmate_a;
extern struct gps_type_t earthmate_b;
extern struct gps_type_t logfile;

/* GPS library internal prototypes */
extern int gps_process_NMEA_message(char *sentence, struct OUTDATA *outdata);
extern void gps_NMEA_handle_message(struct gpsd_t *session, char *sentence);
extern void gps_add_checksum(char *sentence);
extern short gps_checksum(char *sentence);

/* GPS library supplies these */
int gps_open(char *device_name, int device_speed);
void gps_close();
int netlib_passiveTCP(char *service, int qlen);
int netlib_connectTCP(char *host, char *service);
int netlib_connectsock(char *host, char *service, char *protocol);

/* High-level interface */
void gps_init(struct gpsd_t *session, 
	      char *device, int timeout, char devtype,
	      char *dgpsserver,
	      void (*raw_hook)(char *buf));
int gps_activate(struct gpsd_t *session);
void gps_deactivate(struct gpsd_t *session);
void gps_poll(struct gpsd_t *session);
void gps_wrap(struct gpsd_t *session);

/* caller must supply these */
void gpscli_errexit(char *s);
void gpscli_report(int d, const char *fmt, ...);

/* some multipliers for interpreting GPS output */
#define METERS_TO_FEET	3.2808399
#define METERS_TO_MILES	0.00062137119
#define KNOTS_TO_MPH	1.1507794

/* gpsd.h ends here */
