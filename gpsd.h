

enum { DEVICE_GENERIC, DEVICE_TRIPMATE, DEVICE_EARTHMATE, DEVICE_EARTHMATEb };

extern int serial_open();
extern void serial_close();
extern void handle_message(char *sentence);
extern int errexit(char *s);
extern int passiveTCP(char *service, int qlen);
extern int connectTCP(char *host, char *service);
