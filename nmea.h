
#define GPGGA "GPGGA"
#define GPGSA "GPGSA"
#define GPGSV "GPGSV"
#define GPRMC "GPRMC"
#define PRWIZCH "PRWIZCH"

struct OUTDATA {
    int fdin;
    int fdout;

    long cmask;
    char utc[20];		/* UTC date / time in format "mm/dd/yy hh:mm:ss" */

    double latitude;		/* Latitude and longitude in format "d.ddddd" */

    double longitude;

    double altitude;		/* Altitude in meters */

    double speed;		/* Speed over ground, knots */

    double track;		/* Track made good, degress True */

    int satellites;		/* Number of satellites used in solution */

    int status;			/* 0 = no fix, 1 = fix, 2 = dgps fix */

    int mode;			/* 1 = no fix, 2 = 2D, 3 = 3D */

    double pdop;		/* Position dilution of precision */

    double hdop;		/* Horizontal dilution of precision */

    double vdop;		/* Vertical dilution of precision */

    int in_view;		/* # of satellites in view */

    int PRN[12];		/* PRN of satellite */

    int elevation[12];		/* elevation of satellite */

    int azimuth[12];		/* azimuth */

    int ss[12];			/* signal strength */

    int used[12];		/* used in solution */

    int ZCHseen;		/* flag */

    int Zs[12];			/* for the rockwell PRWIZCH */

    int Zv[12];			/*                  value */

    int year;

    int month;

    int day;

    int hours;

    int minutes;

    int seconds;

    double separation;

    double mag_var;

    double course;

    int seen[12];

    int valid[12];		/* signal valid */
};

#define C_LATLON	1
#define C_SAT		2
#define C_ZCH		4

/* prototypes */
extern void doNMEA(short refNum);
extern void processGPRMC(char *sentence);
extern void processGPGGA(char *sentence);
extern void processGPGSV(char *sentence);
extern void processPRWIZCH(char *sentence);
extern void processGPGSA(char *sentence);
extern void add_checksum(char *sentence);
extern short checksum(char *sentence);
extern struct OUTDATA gNMEAdata;
