/* gpsd.h -- fundamental types and structures for the GPS daemon */

#include "gps.h"

/*
 * Some internal capabilities depend on which drivers we're compiling.
 */
#if  FV18_ENABLE || TRIPMATE_ENABLE || EARTHMATE_ENABLE || LOGFILE_ENABLE
#define NON_NMEA_ENABLE
#endif /* FV18_ENABLE || TRIPMATE_ENABLE || EARTHMATE_ENABLE || LOGFILE_ENABLE */
#if EARTHMATE_ENABLE
#define ZODIAC_ENABLE
#endif /* EARTHMATE_ENABLE */

#define BUFSIZE		4096	/* longer than longest NMEA sentence (82) */

struct longlat_t {
/* This structure is used to initialize some older GPS units */
    char *latitude, *longitude;
    char latd, lond;
};

struct gps_session_t;

struct gps_type_t {
/* GPS method table, describes how to talk to a particular GPS type */
    char typekey, *typename, *trigger;
    void (*initializer)(struct gps_session_t *session);
    void (*handle_input)(struct gps_session_t *session);
    int (*rtcm_writer)(struct gps_session_t *session, char *rtcmbuf, int rtcmbytes);
    void (*wrapup)(struct gps_session_t *session);
    int baudrate, stopbits;
    int interval;
};

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

struct gps_session_t {
/* session object, encapsulates all global state */
    struct gps_data_t gNMEAdata;
    struct gps_type_t *device_type;
    char *gpsd_device;	/* where to find the GPS */
    int baudrate;	/* baud rate of session */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    struct termios ttyset, ttyset_old;
#if TRIPMATE_ENABLE
    struct longlat_t initpos;	/* public; set by -i option */
#endif /* TRIPMATE_ENABLE */
#ifdef ZODIAC_ENABLE
    /* private housekeeping stuff for the Zodiac driver */
    unsigned short sn;		/* packet sequence number */
    double mag_var;		/* Magnetic variation in degrees */  
    double separation;		/* Geoidal separation */
    int year, month, day;
    int hours, minutes, seconds;
    /*
     * Zodiac chipset channel status from PRWIZCH.
     * We stash it here is so that raw-mode translation of Zodiac binary 
     * protocol will send it up to the client.
     */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
#endif /* ZODIAC_ENABLE */
};

#define GPGLL "$GPGLL"
#define GPVTG "$GPVTG"
#define GPGGA "$GPGGA"
#define GPGSA "$GPGSA"
#define GPGSV "$GPGSV"
#define GPRMC "$GPRMC"
#define PRWIZCH "$PRWIZCH"

/* here are the available GPS drivers */
extern struct gps_type_t **gpsd_drivers;

/* GPS library internal prototypes */
extern int nmea_parse(char *sentence, struct gps_data_t *outdata);
extern void nmea_send(int fd, const char *fmt, ... );
extern int nmea_sane_satellites(struct gps_data_t *out);
extern void nmea_add_checksum(char *sentence);
extern int gpsd_open(int device_speed, int stopbits, struct gps_session_t *context);
extern void gpsd_close(struct gps_session_t *context);
extern int netlib_connectsock(char *host, char *service, char *protocol);

/* External interface */
extern struct gps_session_t * gpsd_init(char devtype, char *dgpsserver);
extern int gpsd_activate(struct gps_session_t *session);
extern void gpsd_deactivate(struct gps_session_t *session);
extern int gpsd_poll(struct gps_session_t *session);
extern void gpsd_wrap(struct gps_session_t *session);

/* caller should supply this */
void gpsd_report(int d, const char *fmt, ...);

#define DEFAULT_DEVICE_NAME	"/dev/gps"



