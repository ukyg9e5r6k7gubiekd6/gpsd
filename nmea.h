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
extern int nmea_parse(char *sentence, struct gps_data *outdata);
extern void gps_NMEA_handle_message(char *sentence);
extern void nmea_add_checksum(char *sentence);
extern short nmea_checksum(char *sentence);
