#include <time.h>

#define GPVTG "GPVTG"
#define GPGGA "GPGGA"
#define GPGSA "GPGSA"
#define GPGSV "GPGSV"
#define GPRMC "GPRMC"
#define PRWIZCH "PRWIZCH"
#define PMGNST "PMGNST"

/* prototypes */
extern void doNMEA(short refNum);
extern void processGPVTG(char *sentence);
extern void processGPRMC(char *sentence);
extern void processGPGGA(char *sentence);
extern void processGPGSV(char *sentence);
extern void processGPGSA(char *sentence);
extern void processPRWIZCH(char *sentence);
extern void processPMGNST(char *sentence);
extern void add_checksum(char *sentence);
extern short checksum(char *sentence);
extern struct OUTDATA gNMEAdata;
