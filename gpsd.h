/* gpsd.h -- fundamental types and structures for the GPS daemon */

#include "config.h"
#include "gps.h"
#include <setjmp.h>

/* Some internal capabilities depend on which drivers we're compiling. */
#if  TRIPMATE_ENABLE || EARTHMATE_ENABLE || GARMIN_ENABLE || LOGFILE_ENABLE
#define NON_NMEA_ENABLE
#endif /* TRIPMATE_ENABLE || EARTHMATE_ENABLE || GARMIN_ENABLE || LOGFILE_ENABLE */
#if EARTHMATE_ENABLE
#define ZODIAC_ENABLE
#endif /* EARTHMATE_ENABLE */
#if defined(ZODIAC_ENABLE) || defined(GARMIN_ENABLE)
#define BINARY_ENABLE
#endif /* defined(ZODIAC_ENABLE) || defined(GARMIN_ENABLE) */

#define NMEA_MAX	82		/* max length of NMEA sentence */
#define NMEA_BIG_BUF	(2*NMEA_MAX+1)	/* longer than longest NMEA sentence */

/*
 * User Equivalent Range Error
 * UERE is the square root of the sum of the squares of individual
 * errors.  We compute based on the following error budget for
 * satellite range measurements.  Note: this is only used if the
 * GPS doesn't report estimated position error itself.
 *
 * From R.B Langley's 1997 "The GPS error budget". 
 * GPS World , Vol. 8, No. 3, pp. 51-56
 *
 * Atmospheric error -- ionosphere                 7.0m
 * Atmospheric error -- troposphere                0.7m
 * Clock and ephemeris error                       3.6m
 * Receiver noise                                  1.5m
 * Multipath effect                                1.2m
 *
 * From Hoffmann-Wellenhof et al. (1997), "GPS: Theory and Practice", 4th
 * Ed., Springer.
 *
 * Code range noise (C/A)                          0.3m
 * Code range noise (P-code)                       0.03m
 * Phase range                                     0.005m
 *
 * Taking the square root of the sum of aa squares...
 * UERE=sqrt(7.0^2 + 0.7^2 + 3.6^2 + 1.5^2 + 1.2^2 + 0.3^2 + 0.03^2 + 0.005^2)
 *
 * See http://www.seismo.berkeley.edu/~battag/GAMITwrkshp/lecturenotes/unit1/
 * for discussion.
 *
 * DGPS corrects for atmospheric distortion, ephemeris error, and satellite/
 * receiver clock error.  Thus:
 * UERE =  sqrt(1.5^2 + 1.2^2 + 0.3^2 + 0.03^2 + 0.005^2)
 */
#define UERE_NO_DGPS	8.1382
#define UERE_WITH_DGPS	1.9444
#define UERE(session)	((session->dsock==-1) ? UERE_NO_DGPS : UERE_WITH_DGPS)

struct gps_session_t;

struct gps_type_t {
/* GPS method table, describes how to talk to a particular GPS type */
    char typekey, *typename, *trigger;
    void (*initializer)(struct gps_session_t *session);
    void (*handle_input)(struct gps_session_t *session);
    int (*rtcm_writer)(struct gps_session_t *session, char *rtcmbuf, int rtcmbytes);
    int (*speed_switcher)(struct gps_session_t *session, int speed);
    void (*wrapup)(struct gps_session_t *session);
    int cycle;
};

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

#define MAX_PACKET_LENGTH	256

struct gps_session_t {
/* session object, encapsulates all global state */
    struct gps_data_t gNMEAdata;
    struct gps_type_t *device_type;
    char *gpsd_device;	/* where to find the GPS */
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    struct termios ttyset, ttyset_old;
    /* packet-getter internals */
    int	packet_type;
#define BAD_PACKET	-1
#define NMEA_PACKET	0
#define SIRF_PACKET	1
#define ZODIAC_PACKET	2
    unsigned char inbuffer[MAX_PACKET_LENGTH+1];
    unsigned short inbuflen;
    unsigned char *inbufptr;
    unsigned char outbuffer[MAX_PACKET_LENGTH+1];
    unsigned short outbuflen;
    jmp_buf packet_error;
#ifdef PROFILING
    double poll_times[__FD_SETSIZE];	/* last daemon poll time */
#endif /* PROFILING */
#if TRIPMATE_ENABLE || defined(ZODIAC_ENABLE)	/* public; set by -i option */
    char *latitude, *longitude;
    char latd, lond;
#endif /* TRIPMATE_ENABLE || defined(ZODIAC_ENABLE) */
#ifdef BINARY_ENABLE
    double separation;		/* Geoidal separation */
#define NO_SEPARATION	-99999	/* must be out of band */
    int year, month, day;
    int hours, minutes; 
    double seconds;
#endif /* BINARY_ENABLE */
#ifdef ZODIAC_ENABLE	/* private housekeeping stuff for the Zodiac driver */
    unsigned short sn;		/* packet sequence number */
    double mag_var;		/* Magnetic variation in degrees */  
    /*
     * Zodiac chipset channel status from PRWIZCH. Keep it so raw-mode 
     * translation of Zodiac binary protocol can send it up to the client.
     */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
#endif /* ZODIAC_ENABLE */
};

#define PREFIX(pref, sentence)	!strncmp(pref, sentence, sizeof(pref)-1)
#define TIME2DOUBLE(tv)	(tv.tv_sec + tv.tv_usec/1e6)

/* here are the available GPS drivers */
extern struct gps_type_t **gpsd_drivers;

/* GPS library internal prototypes */
extern int nmea_parse(char *sentence, struct gps_data_t *outdata);
extern int nmea_send(int fd, const char *fmt, ... );
extern int nmea_sane_satellites(struct gps_data_t *out);
extern void nmea_add_checksum(char *sentence);
extern int packet_sniff(struct gps_session_t *pstate);
extern int packet_get_nmea(struct gps_session_t *pstate);
extern int packet_get_sirf(struct gps_session_t *pstate);
extern void packet_accept(struct gps_session_t *pstate);
extern int gpsd_open(struct gps_session_t *context);
extern int gpsd_switch_driver(struct gps_session_t *session, char type);
extern int gpsd_set_speed(struct gps_session_t *session, 
			  unsigned int speed, unsigned int stopbits);
extern int gpsd_get_speed(struct termios *);
extern void gpsd_close(struct gps_session_t *context);
extern void gpsd_zero_satellites(struct gps_data_t *out);
extern void gpsd_binary_fix_dump(struct gps_session_t *session, char *buf);
extern void gpsd_binary_satellite_dump(struct gps_session_t *session, char *buf);
extern void gpsd_binary_quality_dump(struct gps_session_t *session, char *bufp);

extern int netlib_connectsock(const char *host, const char *service, const char *protocol);

/* External interface */
extern struct gps_session_t * gpsd_init(char devtype, char *dgpsserver);
extern int gpsd_activate(struct gps_session_t *session);
extern void gpsd_deactivate(struct gps_session_t *session);
extern int gpsd_poll(struct gps_session_t *session);
extern void gpsd_wrap(struct gps_session_t *session);

/* caller should supply this */
void gpsd_report(int d, const char *fmt, ...);

#define DEFAULT_DEVICE_NAME	"/dev/gps"



