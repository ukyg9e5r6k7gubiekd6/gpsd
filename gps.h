/* gps.h -- interface of the libgps library */

#ifndef gps_h
#  define gps_h 1

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <time.h>

#define MAXCHANNELS	12	/* maximum GPS channels (*not* satellites!) */
#define MAXNAMELEN	6	/* maximum length of NMEA tag name */

struct life_t {
/* lifetime structure to be associated with some piece of data */
    time_t	last_refresh;
    int		changed;
};
#define INIT(stamp, now)	stamp.last_refresh=now
#define REFRESH(stamp)	stamp.last_refresh = time(NULL)
#define SEEN(stamp) stamp.last_refresh

struct gps_data_t {
    int	online;			/* 1 if GPS is on line, 0 if not.
				 *
				 * Note: gpsd clears this flag when sentences
				 * fail to show up within the GPS's normal
				 * send cycle time. If the host-to-GPS 
				 * link is lossy enough to drop entire
				 * sentences, this flag will be
				 * prone to false negatives.
				 */
    struct life_t online_stamp;
    char utc[28];		/* UTC date/time as "yyy-mm-ddThh:mm:ss.sssZ".
				 *
				 * Updated on every valid fix (GGA, GLL or
				 * GPRMC). The hhmmss.ss part is reliable to
				 * within one GPS send cycle time (normally one
				 * second).  Altitude could be one send cycle
				 * older than the timestamp if the last
				 * sentence was GPRMC.
				 * 
				 * Within one GPS send cycle after midnight,
				 * if the last sentence was GGA or GLL and not
				 * GPRMC, the date could be off by one.
				 *
				 * The century part of the year is spliced in
				 * from host-machine time. 
				 */
    /*
     * Position/velocity fields are only valid when the last_refresh
     * field of the associated timestamp is nonzero, in which case it
     * tells when the data was collected.
     */
    double latitude;		/* Latitude in degrees */
    double longitude;		/* Longitude in degrees */
    struct life_t latlon_stamp;
    double altitude;		/* Altitude in meters (MSL, not WGS 84) */
    struct life_t altitude_stamp;

    /* velocity */
    double speed;		/* Speed over ground, knots */
    struct life_t speed_stamp;
    double track;		/* Course made good (relative to true north) */
    struct life_t track_stamp;

    /* status of fix */
    int    status;		/* Do we have a fix? */
#define STATUS_NO_FIX	0	/* no */
#define STATUS_FIX	1	/* yes, without DGPS */
#define STATUS_DGPS_FIX	2	/* yes, with DGPS */
    struct life_t status_stamp;
    int    mode;		/* Mode of fix */
#define MODE_NOT_SEEN	0	/* GSA not seen yet */
#define MODE_NO_FIX	1	/* none */
#define MODE_2D  	2	/* good for latitude/longitude */
#define MODE_3D  	3	/* good for altitude too */
    struct life_t mode_stamp;

    /* precision of fix */
    int satellites_used;	/* Number of satellites used in solution */
    int used[MAXCHANNELS];	/* Used in last fix? */
    double pdop, hdop, vdop;	/* Dilution of precision */
    struct life_t fix_quality_stamp;

    /* satellite status */
    int satellites;	/* # of satellites in view */
    int PRN[MAXCHANNELS];	/* PRNs of satellite */
    int elevation[MAXCHANNELS];	/* elevation of satellite */
    int azimuth[MAXCHANNELS];	/* azimuth */
    int ss[MAXCHANNELS];	/* signal strength */
    int part, await;		/* for tracking GSV parts */
    struct life_t satellite_stamp;

    /* what type gpsd thinks the device is */
    char	*gps_id;	/* only valid if non-null. */

#ifdef PROFILING
    /* profiling data for last sentence */
    int profiling;		/* profiling enabled? */
    char tag[MAXNAMELEN+1];	/* tag of last sentence processed */
    int sentence_length;	/* character count of last sentence */
    double gps_time;		/* GPS time (equivalent of utc field) */
    double d_xmit_time;		/* beginning of sentence transmission */
    double d_recv_time;		/* daemon receipt time (-> E1+T1) */
    double d_decode_time;	/* daemon end-of-decode time (-> D1) */
    double poll_time;		/* daemon poll time (-> W) */
    double emit_time;		/* emission time (-> E2) */
    double c_recv_time;		/* client receipt time (-> T2) */
    double c_decode_time;	/* client end-of-decode time (-> D2) */
#endif /* PROFILING */
    int baudrate, stopbits;	/* RS232 link paramters */
    int cycle;			/* refresh cycle time in seconds */

    /* these members are private */
    int gps_fd;			/* socket or file descriptor to GPS */
    void (*raw_hook)(char *buf);/* Raw-mode hook for GPS data. */
    int seen_sentences;		/* track which sentences have been seen */
#define GPRMC	0x01
#define GPGGA	0x02
#define GPGLL	0x04
#define GPVTG	0x08
#define GPGSA	0x10
#define GPGSV	0x20
};

struct map_t {
    const char *name; 
    const int value;
}; 
extern struct map_t *sentence_map;

struct gps_data_t *gps_open(const char *host, const char *port);
int gps_close(struct gps_data_t *);
int gps_query(struct gps_data_t *gpsdata, const char *requests);
int gps_poll(struct gps_data_t *gpsdata);
void gps_set_raw_hook(struct gps_data_t *gpsdata, void (*hook)(char *buf));

/* some multipliers for interpreting GPS output */
#define METERS_TO_FEET	3.2808399	/* Imperial (U.S./British) feet */
#define METERS_TO_MILES	0.00062137119	/* International miles */
#define KNOTS_TO_MPH	1.1507794	/* International miles and knots */
#define KNOTS_TO_KPH	1.852		/* International knots */
#define PI 3.1415926535897932384626433832795029L	/* for radians-to-degrees and vv. */
#define RAD_2_DEG  57.2957795130823208767981548141051703L /* degrees = 180/pi */
#define DEG_2_RAD  0.0174532925199432957692369076848861271L /* radians = pi/180 */

/* gps_open() error return values */
#define NL_NOSERVICE	-1	/* can't get service entry */
#define NL_NOHOST	-2	/* can't get host entry */
#define NL_NOPROTO	-3	/* can't get protocol entry */
#define NL_NOSOCK	-4	/* can't create socket */
#define NL_NOSOCKOPT	-5	/* error SETSOCKOPT SO_REUSEADDR */
#define NL_NOCONNECT	-6	/* can't connect to host */

#define DEFAULT_GPSD_PORT	"2947"	/* IANA assignment */

#ifdef __cplusplus
}  /* End of the 'extern "C"' block */
#endif

#endif /* gps_h */
/* gps.h ends here */

