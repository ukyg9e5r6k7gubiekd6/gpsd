/* gpsd.h -- fundamental types and structures for the GPS daemon */

#define BUFSIZE	4096	/* longer than max-length NMEA sentence (82 chars) */

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
    struct gps_data gNMEAdata;
    char *gps_device;	/* where to find the GPS */
    int baudrate;		/* baud rate of session */
    int fdin;		/* input fd from GPS */
    int fdout;		/* output fd to GPS */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    void (*raw_hook)(char *buf);	/* raw-mode hook for GPS data */
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

/* GPS library internal prototypes */
extern int gps_process_NMEA_message(char *sentence, struct gps_data *outdata);
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
	      int timeout, char devtype, char *dgpsserver);
int gps_activate(struct gpsd_t *session);
void gps_deactivate(struct gpsd_t *session);
void gps_poll(struct gpsd_t *session);
void gps_wrap(struct gpsd_t *session);

/* caller must supply this */
void gpscli_report(int d, const char *fmt, ...);


