#include <time.h>

#define GPGLL "GPGLL"
#define GPVTG "GPVTG"
#define GPGGA "GPGGA"
#define GPGSA "GPGSA"
#define GPGSV "GPGSV"
#define GPRMC "GPRMC"
#define PRWIZCH "PRWIZCH"
#define PMGNST "PMGNST"

/* prototypes */
extern void doNMEA(short refNum);
extern int process_NMEA_message(char *sentence, struct OUTDATA *outdata);
extern void nmea_handle_message(char *sentence);
extern void add_checksum(char *sentence);
extern short checksum(char *sentence);
