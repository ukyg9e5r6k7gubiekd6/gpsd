/* gpsd.h -- internals for the gps library */

#define BUFSIZE	4096	/* longer than max-length NMEA sentence (82 chars) */

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
	      int timeout, char devtype, char *dgpsserver);
int gps_activate(struct gpsd_t *session);
void gps_deactivate(struct gpsd_t *session);
void gps_poll(struct gpsd_t *session);
void gps_wrap(struct gpsd_t *session);

/* caller must supply this */
void gpscli_report(int d, const char *fmt, ...);


