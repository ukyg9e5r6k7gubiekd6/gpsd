

enum { DEVICE_GENERIC, DEVICE_TRIPMATE, DEVICE_EARTHMATE, DEVICE_EARTHMATEb };

void send_nmea(fd_set *afds, fd_set *nmea_fds, char *buf);
int serial_open();
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
