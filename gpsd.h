/* gpsd.h -- fundamental types and structures for the GPS daemon */

#include "config.h"
#include "gps.h"
#include "gpsutils.h"

/* Some internal capabilities depend on which drivers we're compiling. */
#ifdef EARTHMATE_ENABLE
#define ZODIAC_ENABLE	
#endif
#if defined(ZODIAC_ENABLE) || defined(SIRF_ENABLE) || defined(GARMIN_ENABLE)
#define BINARY_ENABLE	
#endif
#if defined(TRIPMATE_ENABLE) || defined(BINARY_ENABLE)
#define NON_NMEA_ENABLE	
#endif

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
 * Note: we're assume these are 1-sigma error ranges. This needs to
 * be checked in the sources.
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

struct gps_device_t;

struct gps_type_t {
/* GPS method table, describes how to talk to a particular GPS type */
    char *typename, *trigger;
    int (*probe)(struct gps_device_t *session);
    void (*initializer)(struct gps_device_t *session);
    int (*get_packet)(struct gps_device_t *session, int waiting);
    int (*parse_packet)(struct gps_device_t *session);
    int (*rtcm_writer)(struct gps_device_t *session, char *rtcmbuf, int rtcmbytes);
    int (*speed_switcher)(struct gps_device_t *session, int speed);
    void (*mode_switcher)(struct gps_device_t *session, int mode);
    void (*wrapup)(struct gps_device_t *session);
    int cycle;
};

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

/*
 * The packet buffers need to be as long than the longest packet we
 * expect to see in any protocol, because we have to be able to hold
 * an entire packet for checksumming.  Thus, in particular, they need
 * to be as long as a SiRF MID 4 packet, 188 bytes payload plus eight bytes 
 * of header/length/checksum/trailer. 
 */
#define MAX_PACKET_LENGTH	196	/* 188 + 8 */

struct gps_device_t {
/* session object, encapsulates all global state */
    struct gps_data_t gpsdata;
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
    unsigned int packet_state;
    unsigned int packet_length;
    unsigned char inbuffer[MAX_PACKET_LENGTH*2+1];
    unsigned short inbuflen;
    unsigned char *inbufptr;
    unsigned char outbuffer[MAX_PACKET_LENGTH+1];
    unsigned short outbuflen;
    double poll_times[FD_SETSIZE];	/* last daemon poll time */
    unsigned long counter;
#ifdef BINARY_ENABLE
    struct gps_fix_t lastfix;	/* use to compute uncertainties */
    unsigned int driverstate;	/* for private use */
#define SIRF_LT_231	0x01		/* SiRF at firmware rev < 231 */
#define SIRF_EQ_231     0x02            /* SiRF at firmware rev == 231 */
#define SIRF_GE_232     0x04            /* SiRF at firmware rev >= 232 */
#define SIRF_SEEN_41    0x08            /* Seen Geodetic Navigation Data? */
#define FULL_PACKET	0x10		/* Full packet has been seen */
    double separation;		/* Geoidal separation */
#define NO_SEPARATION	-99999	/* must be out of band */
    double mag_var;		/* Magnetic variation in degrees */  
#ifdef GARMIN_ENABLE	/* private housekeeping stuff for the Garmin driver */
    unsigned char GarminBuffer[4096 + 12]; /* Garmin packet buffer */
    long GarminBufferLen;                  /* current GarminBuffer Length */
#endif /* GARMIN_ENABLE */
#ifdef ZODIAC_ENABLE	/* private housekeeping stuff for the Zodiac driver */
    unsigned short sn;		/* packet sequence number */
    /*
     * Zodiac chipset channel status from PRWIZCH. Keep it so raw-mode 
     * translation of Zodiac binary protocol can send it up to the client.
     */
    int Zs[MAXCHANNELS];	/* satellite PRNs */
    int Zv[MAXCHANNELS];	/* signal values (0-7) */
#endif /* ZODIAC_ENABLE */
#endif /* BINARY_ENABLE */
#ifdef NTPSHM_ENABLE
    struct shmTime *shmTime;
#endif /* defined(SHM_H) && defined(IPC_H) */
};

/* here are the available GPS drivers */
extern struct gps_type_t **gpsd_drivers;

/* GPS library internal prototypes */
extern int nmea_parse(char *, struct gps_data_t *);
extern int nmea_send(int, const char *, ... );
extern void nmea_add_checksum(char *);

extern int sirf_parse(struct gps_device_t *, unsigned char *, int);

extern int packet_get(struct gps_device_t *, int);
extern int packet_sniff(struct gps_device_t *);

extern int gpsd_open(struct gps_device_t *);
extern int gpsd_switch_driver(struct gps_device_t *, char *);
extern int gpsd_set_speed(struct gps_device_t *, unsigned int, unsigned int);
extern int gpsd_get_speed(struct termios *);
extern void gpsd_close(struct gps_device_t *);

extern void gpsd_raw_hook(struct gps_device_t *, char *);
extern void gpsd_zero_satellites(struct gps_data_t *);
extern void gpsd_binary_fix_dump(struct gps_device_t *, char *);
extern void gpsd_binary_satellite_dump(struct gps_device_t *, char *);
extern void gpsd_binary_quality_dump(struct gps_device_t *, char *);

extern int netlib_connectsock(const char *, const char *, const char *);

extern int ntpshm_init(struct gps_device_t *);
extern int ntpshm_put(struct gps_device_t *, double);

/* External interface */
extern int gpsd_open_dgps(char *);
extern struct gps_device_t * gpsd_init(char *device);
extern int gpsd_activate(struct gps_device_t *);
extern void gpsd_deactivate(struct gps_device_t *);
extern int gpsd_poll(struct gps_device_t *);
extern void gpsd_wrap(struct gps_device_t *);

/* caller should supply this */
void gpsd_report(int, const char *, ...);



