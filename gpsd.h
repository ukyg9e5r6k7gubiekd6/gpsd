

enum { DEVICE_GENERIC, DEVICE_TRIPMATE, DEVICE_EARTHMATE, DEVICE_EARTHMATEb };

struct longlat
{
    char *latitude;
    char *longitude;
    char latd;
    char lond;
};

struct session_t
{
    int debug;
    struct longlat initpos;
    int device_type;
    struct OUTDATA gNMEAdata;
};

void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf);
int serial_open(char *device_name, int device_speed);
void serial_close();
void handle_message(char *sentence);
void errexit(char *s);
void errlog(char *s);
int passiveTCP(char *service, int qlen);
int connectTCP(char *host, char *service);
int em_send_rtcm(char *rtcmbuf, int rtcmbytes);
int connectsock(char *host, char *service, char *protocol);
void do_eminit();
int handle_EMinput(int input, fd_set * afds, fd_set * nmea_fds);
