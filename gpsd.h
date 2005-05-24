#ifndef _gpsd_h_
#define _gpsd_h_

/* gpsd.h -- fundamental types and structures for the GPS daemon */

#include "config.h"
#include "gps.h"
#include "gpsutils.h"

/* Some internal capabilities depend on which drivers we're compiling. */
#ifdef EARTHMATE_ENABLE
#define ZODIAC_ENABLE	
#endif
#if defined(ZODIAC_ENABLE) || defined(SIRFII_ENABLE) || defined(GARMIN_ENABLE)
#define BINARY_ENABLE	
#endif
#if defined(TRIPMATE_ENABLE) || defined(BINARY_ENABLE)
#define NON_NMEA_ENABLE	
#endif

#define NMEA_MAX	82		/* max length of NMEA sentence */
#define NMEA_BIG_BUF	(2*NMEA_MAX+1)	/* longer than longest NMEA sentence */

/* only used if the GPS doesn't report estimated position error itself */
#define UERE_NO_DGPS	8	/* meters */
#define UERE_WITH_DGPS	2	/* meters */
#define UERE(session)	((session->dsock==-1) ? UERE_NO_DGPS : UERE_WITH_DGPS)

#define NTPSHMSEGS	4		/* number of NTP SHM segments */

struct gps_context_t {
    int valid;
#define LEAP_SECOND_VALID	0x01	/* we have or don't need correction */
    int leap_seconds;
    int century;			/* for NMEA-only devices without ZDA */
#ifdef NTPSHM_ENABLE
    struct shmTime *shmTime[NTPSHMSEGS];
    int shmTimeInuse[NTPSHMSEGS];
#endif /* NTPSHM_ENABLE */
};

struct gps_device_t;

struct gps_type_t {
/* GPS method table, describes how to talk to a particular GPS type */
    char *typename, *trigger;
    int (*probe)(struct gps_device_t *session);
    void (*initializer)(struct gps_device_t *session);
    int (*get_packet)(struct gps_device_t *session, unsigned int waiting);
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
    int dsock;		/* socket to DGPS server */
    int sentdgps;	/* have we sent a DGPS correction? */
    int fixcnt;		/* count of good fixes seen */
    struct termios ttyset, ttyset_old;
    /* packet-getter internals */
    int packet_full;	/* do we presently see a full packet? */
    int	packet_type;
#define BAD_PACKET	-1
#define NMEA_PACKET	0
#define SIRF_PACKET	1
#define ZODIAC_PACKET	2
#define TSIP_PACKET	3
    unsigned int baudindex;
    unsigned int packet_state;
    unsigned int packet_length;
    unsigned char inbuffer[MAX_PACKET_LENGTH*2+1];
    unsigned short inbuflen;
    unsigned char *inbufptr;
    unsigned char outbuffer[MAX_PACKET_LENGTH+1];
    unsigned short outbuflen;
    unsigned long counter;
#ifdef BINARY_ENABLE
    struct gps_fix_t lastfix;	/* use to compute uncertainties */
    unsigned int driverstate;	/* for private use */
#define SIRF_LT_231	0x01		/* SiRF at firmware rev < 231 */
#define SIRF_EQ_231     0x02            /* SiRF at firmware rev == 231 */
#define SIRF_GE_232     0x04            /* SiRF at firmware rev >= 232 */
#define UBLOX   	0x08		/* uBlox firmware with packet 0x62 */
    double mag_var;		/* Magnetic variation in degrees */  
#ifdef SIRFII_ENABLE
    unsigned long satcounter;
#endif /* SIRFII_ENABLE */
#ifdef GARMIN_ENABLE	/* private housekeeping stuff for the Garmin driver */
    void *GarminBuffer; /* Pointer Garmin packet buffer 
                           void *, to keep the packet details out of the 
                           global context and save spave */
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
    unsigned int time_seen;
#define TIME_SEEN_GPS_1	0x01	/* Seen GPS time variant 1? */
#define TIME_SEEN_GPS_2	0x02	/* Seen GPS time variant 2? */
#define TIME_SEEN_UTC_1	0x04	/* Seen UTC time variant 1? */
#define TIME_SEEN_UTC_2	0x08	/* Seen UTC time variant 2? */
#endif /* NTPSHM_ENABLE */
    double poll_times[FD_SETSIZE];	/* last daemon poll time */

    struct gps_context_t	*context;
#ifdef NTPSHM_ENABLE
    int shmTime;
# ifdef PPS_ENABLE
    int shmTimeP;
# endif /* NTPSHM_ENABLE */
#endif /* NTPSHM_ENABLE */
};

#define IS_HIGHEST_BIT(v,m)	!(v & ~((m<<1)-1))

/* here are the available GPS drivers */
extern struct gps_type_t **gpsd_drivers;

/* GPS library internal prototypes */
extern int nmea_parse(char *, struct gps_data_t *);
extern int nmea_send(int, const char *, ... );
extern void nmea_add_checksum(char *);

extern int sirf_parse(struct gps_device_t *, unsigned char *, int);

extern void packet_reset(struct gps_device_t *session);
extern void packet_pushback(struct gps_device_t *session);
extern int packet_get(struct gps_device_t *, unsigned int);
extern int packet_sniff(struct gps_device_t *);

extern int gpsd_open(struct gps_device_t *);
extern int gpsd_next_hunt_setting(struct gps_device_t *);
extern int gpsd_switch_driver(struct gps_device_t *, char *);
extern void gpsd_set_speed(struct gps_device_t *, unsigned int, unsigned int, unsigned int);
extern int gpsd_get_speed(struct termios *);
extern void gpsd_close(struct gps_device_t *);

extern void gpsd_raw_hook(struct gps_device_t *, char *, int level);
extern void gpsd_zero_satellites(struct gps_data_t *);
extern void gpsd_binary_fix_dump(struct gps_device_t *, char *);
extern void gpsd_binary_satellite_dump(struct gps_device_t *, char *);
extern void gpsd_binary_quality_dump(struct gps_device_t *, char *);

extern int netlib_connectsock(const char *, const char *, const char *);

extern int ntpshm_init(struct gps_context_t *);
extern int ntpshm_alloc(struct gps_context_t *context);
extern int ntpshm_free(struct gps_context_t *context, int segment);
extern int ntpshm_put(struct gps_device_t *, double);
extern int ntpshm_pps(struct gps_device_t *,struct timeval *);

extern void ecef_to_wgs84fix(struct gps_fix_t *,
			     double, double, double, 
			     double, double, double);
extern void dop(int, struct gps_data_t *);

/* External interface */
extern int gpsd_open_dgps(char *);
extern struct gps_device_t * gpsd_init(struct gps_context_t *, char *device);
extern int gpsd_activate(struct gps_device_t *);
extern void gpsd_deactivate(struct gps_device_t *);
extern int gpsd_poll(struct gps_device_t *);
extern void gpsd_wrap(struct gps_device_t *);

/* caller should supply this */
void gpsd_report(int, const char *, ...);



#endif /* _gpsd_h_ */
