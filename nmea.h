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
extern int gps_process_NMEA_message(char *sentence, struct OUTDATA *outdata);
extern void gps_NMEA_handle_message(char *sentence);
extern void gps_add_checksum(char *sentence);
extern short gps_checksum(char *sentence);
