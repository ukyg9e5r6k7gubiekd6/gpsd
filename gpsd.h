/* gpsd.h -- fundamental types and structures for the GPS daemon */

#define BUFSIZE		4096	/* longer than longest NMEA sentence (82) */
#define GPS_TIMEOUT	5	/* consider GPS connection lost after 5 sec */

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
    char *trigger;
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
    struct gps_data gNMEAdata;
    char *gps_device;	/* where to find the GPS */
    int baudrate;		/* baud rate of session */
    int fdin;		/* input fd from GPS */
    int fdout;		/* output fd to GPS */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    int debug;		/* debug verbosity level */
};

/* some multipliers for interpreting GPS output */
#define METERS_TO_FEET	3.2808399
#define METERS_TO_MILES	0.00062137119
#define KNOTS_TO_MPH	1.1507794

#define GPGLL "GPGLL"
#define GPVTG "GPVTG"
#define GPGGA "GPGGA"
#define GPGSA "GPGSA"
#define GPGSV "GPGSV"
#define GPRMC "GPRMC"
#define PRWIZCH "PRWIZCH"
#define PMGNST "PMGNST"

/* here are the available GPS drivers */
extern struct gps_type_t nmea;
extern struct gps_type_t tripmate;
extern struct gps_type_t earthmate_a;
extern struct gps_type_t earthmate_b;
extern struct gps_type_t logfile;
extern struct gps_type_t *gps_drivers[5];

/* GPS library internal prototypes */
extern int nmea_parse(char *sentence, struct gps_data *outdata);
extern int nmea_sane_satellites(struct gps_data *out);
extern void gps_NMEA_handle_message(struct gpsd_t *session, char *sentence);
extern void nmea_add_checksum(char *sentence);
extern short nmea_checksum(char *sentence);
extern int gps_open(char *device_name, int device_speed);
extern void gps_close();
extern int netlib_passiveTCP(char *service, int qlen);
extern int netlib_connectTCP(char *host, char *service);
extern int netlib_connectsock(char *host, char *service, char *protocol);

/* External interface */
extern void gps_init(struct gpsd_t *session, 
	      int timeout, char devtype, char *dgpsserver);
extern int gps_activate(struct gpsd_t *session);
extern void gps_deactivate(struct gpsd_t *session);
extern int gps_poll(struct gpsd_t *session);
extern void gps_wrap(struct gpsd_t *session);

/* caller must supply this */
extern void gpscli_report(int d, const char *fmt, ...);


