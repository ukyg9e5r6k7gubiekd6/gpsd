#include "config.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "nmea.h"

extern struct OUTDATA gNMEAdata;
static void do_lat_lon(char *sentence, int begin);
static char *field(char *sentence, short n);

static void update_field_i(char *sentence, int fld, int *dest, int mask);
#if 0
static void update_field_f(char *sentence, int fld, double *dest, int mask);
#endif
/* ----------------------------------------------------------------------- */

/*
   The time field in the GPRMC sentence is in the format hhmmss;
   the date field is in the format ddmmyy. The output will
   be in the format:

   mm/dd/yyyy hh:mm:ss
   01234567890123456789
 */

void processGPRMC(char *sentence)
{
    char s[20], d[10];
    int tmp;

    sscanf(field(sentence, 9), "%s", d);	/* Date: ddmmyy */

    strncpy(s, d + 2, 2);	/* copy month */

    strncpy(s + 3, d, 2);	/* copy date */

    sscanf((d+4), "%2d", &tmp);

    /* Tf.: Window the year from 1970 to 2069. This buys us some time. */
    if (tmp < 70) 
      strncpy(s + 6, "20", 2);	/* 21th century */
    else
      strncpy(s + 6, "19", 2);	/* 20th century */

    strncpy(s + 8, d + 4, 2);	/* copy year */

    sscanf(field(sentence, 1), "%s", d);	/* Time: hhmmss */

    strncpy(s + 11, d, 2);	/* copy hours */

    strncpy(s + 14, d + 2, 2);	/* copy minutes */

    strncpy(s + 17, d + 4, 2);	/* copy seconds */

    s[2] = s[5] = '/';		/* add the '/'s, ':'s, ' ' and string terminator */

    s[10] = ' ';
    s[13] = s[16] = ':';
    s[19] = '\0';

    strcpy(gNMEAdata.utc, s);

    /* A = valid, V = invalid */
    if (strcmp(field(sentence, 2), "V") == 0)
	gNMEAdata.status = 0;
#if 0    /* Let the GGA sentence do the update so we catch diff fixes */
    else
	gNMEAdata.status = 0;
#endif

    sscanf(field(sentence, 7), "%lf", &gNMEAdata.speed);
    sscanf(field(sentence, 8), "%lf", &gNMEAdata.track);

    do_lat_lon(sentence, 3);

}

/* ----------------------------------------------------------------------- */

void processGPGGA(char *sentence)
{
    do_lat_lon(sentence, 2);
    /* 0 = none, 1 = normal, 2 = diff */
    sscanf(field(sentence, 6), "%d", &gNMEAdata.status);
    sscanf(field(sentence, 7), "%d", &gNMEAdata.satellites);
    sscanf(field(sentence, 9), "%lf", &gNMEAdata.altitude);
}

/* ----------------------------------------------------------------------- */

void processGPGSA(char *sentence)
{

  /* 1 = none, 2 = 2d, 3 = 3d */
    sscanf(field(sentence, 2), "%d", &gNMEAdata.mode);
    sscanf(field(sentence, 15), "%lf", &gNMEAdata.pdop);
    sscanf(field(sentence, 16), "%lf", &gNMEAdata.hdop);
    sscanf(field(sentence, 17), "%lf", &gNMEAdata.vdop);
}

/* ----------------------------------------------------------------------- */

void processGPGSV(char *sentence)
{
    int n, m, f = 4;


    sscanf(field(sentence, 2), "%d", &n);
    update_field_i(sentence, 3, &gNMEAdata.in_view, C_SAT);

    n = (n - 1) * 4;
    m = n + 4;

    while (n < gNMEAdata.in_view && n < m) {
	update_field_i(sentence, f++, &gNMEAdata.PRN[n], C_SAT);
	update_field_i(sentence, f++, &gNMEAdata.elevation[n], C_SAT);
	update_field_i(sentence, f++, &gNMEAdata.azimuth[n], C_SAT);
	if (*(field(sentence, f)))
	    update_field_i(sentence, f, &gNMEAdata.ss[n], C_SAT);
	f++;
	n++;
    }
}

/* ----------------------------------------------------------------------- */

void processPRWIZCH(char *sentence)
{
    int i;

    for (i = 0; i < 12; i++) {
	update_field_i(sentence, 2 * i + 1, &gNMEAdata.Zs[i], C_ZCH);
	update_field_i(sentence, 2 * i + 2, &gNMEAdata.Zv[i], C_ZCH);
    }
    gNMEAdata.ZCHseen = 1;
}

/* ----------------------------------------------------------------------- */

static void do_lat_lon(char *sentence, int begin)
{
    double lat, lon, d, m;
    char str[20], *p;


    if (*(p = field(sentence, begin + 0)) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lat);
	m = 100.0 * modf(lat / 100.0, &d);
	lat = d + m / 60.0;
	p = field(sentence, begin + 1);
	if (*p == 'S')
	    lat = -lat;
	if (gNMEAdata.latitude != lat) {
	    gNMEAdata.latitude = lat;
	    gNMEAdata.cmask |= C_LATLON;
	}
    }
    if (*(p = field(sentence, begin + 2)) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lon);
	m = 100.0 * modf(lon / 100.0, &d);
	lon = d + m / 60.0;

	p = field(sentence, begin + 3);
	if (*p == 'W')
	    lon = -lon;
	if (gNMEAdata.longitude != lon) {
	    gNMEAdata.longitude = lon;
	    gNMEAdata.cmask |= C_LATLON;
	}
    }
}


static void update_field_i(char *sentence, int fld, int *dest, int mask)
{
    int tmp;

    sscanf(field(sentence, fld), "%d", &tmp);

    if (tmp != *dest) {
	*dest = tmp;
	gNMEAdata.cmask |= mask;
    }
}

#if 0
static void update_field_f(char *sentence, int fld, double *dest, int mask)
{
    double tmp;

    scanf(field(sentence, fld), "%lf", &tmp);

    if (tmp != *dest) {
	*dest = tmp;
	gNMEAdata.cmask |= mask;
    }
}
#endif

/* ----------------------------------------------------------------------- */

short checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence, csum[3];

    while ((c = *p++) != '*')
	sum ^= c;

    sprintf(csum, "%02X", sum);
    return (strncmp(csum, p, 2) == 0);
}

void add_checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    while ((c = *p++) != '*')
	sum ^= c;

    sprintf(p, "%02X\r\n", sum);
}

/* ----------------------------------------------------------------------- */

/* field() returns a string containing the nth comma delimited
   field from sentence string
 */

static char *field(char *sentence, short n)
{
    static char result[100];
    char *p = sentence;

    while (n-- > 0)
	while (*p++ != ',');
    strcpy(result, p);
    p = result;
    while (*p && *p != ',' && *p != '*' && *p != '\r')
	p++;

    *p = '\0';
    return result;
}
