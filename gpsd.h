/* gpsd.h -- fundamental types and structures for the GPS daemon */

#define BUFSIZE		4096	/* longer than longest NMEA sentence (82) */

struct longlat_t
/* This structure is used to initialize some older GPS units */
{
    char *latitude;
    char *longitude;
    char latd;
    char lond;
};

struct gps_session_t;

struct gps_type_t
/* GPS method table, describes how to talk to a particular GPS type */
{
    char typekey, *typename;
    char *trigger;
    void (*initializer)(struct gps_session_t *session);
    int (*handle_input)(struct gps_session_t *session);
    int (*rtcm_writer)(struct gps_session_t *session, char *rtcmbuf, int rtcmbytes);
    void (*wrapup)(struct gps_session_t *session);
    int baudrate;
    int stopbits;
    int interval;
};

struct gps_session_t
/* session object, encapsulates all global state */
{
    struct gps_type_t *device_type;
    struct longlat_t initpos;
    struct gps_data_t gNMEAdata;
    char *gpsd_device;	/* where to find the GPS */
    int baudrate;	/* baud rate of session */
    int fdin;		/* input fd from GPS */
    int fdout;		/* output fd to GPS */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */

#ifdef ZODIAC_ENABLE
    /* private housekeeping stuff for the Zodiac driver */
    double mag_var;		/* Magnetic variation in degrees */  
    double separation;		/* Geoidal separation */
    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;
    /*
     * Zodiac chipset channel status from PRWIZCH.
     * This is actually redundant with the SNRs in GPGSV,
     * and all known variants of the Zodiac chipsets issue GPGSV.
     * The only reason we stash it here is so that raw-mode
     * translation of Zodiac binary protocol will send it
     * up to the client.
     */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
#endif /* ZODIAC_ENABLE */
};

/* some multipliers for interpreting GPS output */
#define METERS_TO_FEET	3.2808399
#define METERS_TO_MILES	0.00062137119
#define KNOTS_TO_MPH	1.1507794

#define GPGLL "$GPGLL"
#define GPVTG "$GPVTG"
#define GPGGA "$GPGGA"
#define GPGSA "$GPGSA"
#define GPGSV "$GPGSV"
#define GPRMC "$GPRMC"
#define PRWIZCH "$PRWIZCH"
#define PMGNST "$PMGNST"

/* here are the available GPS drivers */
extern struct gps_type_t nmea;
extern struct gps_type_t tripmate;
extern struct gps_type_t earthmate;
extern struct gps_type_t zodiac_binary;
extern struct gps_type_t logfile;
extern struct gps_type_t **gpsd_drivers;

/* GPS library internal prototypes */
extern int nmea_parse(char *sentence, struct gps_data_t *outdata);
extern void nmea_send(int fd, const char *fmt, ... );
extern int nmea_sane_satellites(struct gps_data_t *out);
extern void nmea_add_checksum(char *sentence);
extern int gpsd_open(char *device_name, int device_speed, int stopbits);
extern void gpsd_close(void);
extern int netlib_connectsock(char *host, char *service, char *protocol);

/* External interface */
extern struct gps_session_t * gpsd_init(char devtype, char *dgpsserver);
extern int gpsd_activate(struct gps_session_t *session);
extern void gpsd_deactivate(struct gps_session_t *session);
extern int gpsd_poll(struct gps_session_t *session);
extern void gpsd_wrap(struct gps_session_t *session);

/* caller must supply this */
void gpsd_report(int d, const char *fmt, ...);


