/* gpsd.h -- fundamental types and structures for the GPS daemon */

struct longlat_t
/* This structure is used to initialize some older GPS units */
{
    char *latitude;
    char *longitude;
    char latd;
    char lond;
};

struct gps_type_t
/* GPS method table, describes how to talk to a particular GPS type */
{
    char typekey, *typename;
    void (*initializer)(void);
    int (*handle_input)(int input, fd_set * afds, fd_set * nmea_fds);
    int (*rctm_writer)(char *rtcmbuf, int rtcmbytes);
    void (*wrapup)(void);
    int baudrate;
};

struct session_t
/* session object, encapsulates all global state */
{
    struct gps_type_t *device_type;
    struct longlat_t initpos;
    struct OUTDATA gNMEAdata;
    int fdin;
    int fdout;
    int debug;
};

/* here are the available drivers */
extern struct gps_type_t nmea;
extern struct gps_type_t tripmate;
extern struct gps_type_t earthmate_a;
extern struct gps_type_t earthmate_b;
extern struct gps_type_t logfile;

void report(int d, const char *fmt, ...);
void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf);
int serial_open(char *device_name, int device_speed);
void serial_close();
void errexit(char *s);
void errlog(char *s);
int passiveTCP(char *service, int qlen);
int connectTCP(char *host, char *service);
int connectsock(char *host, char *service, char *protocol);

/* gpsd.h ends here */
