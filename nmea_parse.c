#include "config.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "outdata.h"
#include "nmea.h"

static void do_lat_lon(char *sentence, int begin, struct OUTDATA *out);
static char *field(char *sentence, short n);

static void update_field_i(char *sentence, int fld, int *dest, int mask, struct OUTDATA *out);
#if 0
static void update_field_f(char *sentence, int fld, double *dest, int mask, struct OUTDATA *out);
#endif
/* ----------------------------------------------------------------------- */

/*
   The time field in the GPRMC sentence is in the format hhmmss;
   the date field is in the format ddmmyy. The output will
   be in the format:

   mm/dd/yyyy hh:mm:ss
   01234567890123456789
 */

static void processGPRMC(char *sentence, struct OUTDATA *out)
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

    strcpy(out->utc, s);

    /* A = valid, V = invalid */
    if (strcmp(field(sentence, 2), "V") == 0)
	out->status = 0;

    sscanf(field(sentence, 7), "%lf", &out->speed);

#if GPRMC_TRACK
    sscanf(field(sentence, 8), "%lf", &out->track);
#endif

    do_lat_lon(sentence, 3, out);
}

/*
  $PMGNST,02.12,3,T,534,05.0,+03327,00*40 

where:
      ST      status information
      02.12   Version number?
      3       2D or 3D
      T       True if we have a fix False otherwise
      534     numbers change - unknown
      05.0    time left on the gps battery in hours
      +03327  numbers change (freq. compensation?)
      00      PRN number receiving current focus
      *40    checksum
 */

void processPMGNST(char *sentence, struct OUTDATA *out)
{
    int tmp1;
    char foo;

    /* using this for mode and status seems a bit desperate */
    /* only use it if we don't have better info */
    sscanf(field(sentence, 2), "%d", &tmp1);	
    sscanf(field(sentence, 3), "%c", &foo);	
    
    if (!(out->cmask&C_STATUS)) {
	if (foo == 'T') {
	    out->status = 1;
	    out->mode = tmp1;
	}
	else {
	    out->status = 0;
	    out->mode = 1;
	}
	out->ts_status = out->last_update;
	out->cmask |= C_STATUS;
	out->ts_mode = out->last_update;
	out->cmask |= C_MODE;
    }
}

/* ----------------------------------------------------------------------- */

void processGPVTG(char *sentence, struct OUTDATA *out)
{
    sscanf(field(sentence, 3), "%lf", &out->speed);
#if GPRMC_TRACK
    sscanf(field(sentence, 1), "%lf", &out->track);
#endif
}

/* ----------------------------------------------------------------------- */

void processGPGGA(char *sentence, struct OUTDATA *out)
{
    do_lat_lon(sentence, 2, out);
    /* 0 = none, 1 = normal, 2 = diff */
    sscanf(field(sentence, 6), "%d", &out->status);
    out->ts_status = out->last_update;
    out->cmask |= C_STATUS;
    sscanf(field(sentence, 7), "%d", &out->satellites);
    sscanf(field(sentence, 9), "%lf", &out->altitude);
    out->ts_alt = out->last_update;
}

/* ----------------------------------------------------------------------- */

void processGPGSA(char *sentence, struct OUTDATA *out)
{

  /* 1 = none, 2 = 2d, 3 = 3d */
    sscanf(field(sentence, 2), "%d", &out->mode);
    out->ts_mode = out->last_update;
    out->cmask |= C_MODE;
    sscanf(field(sentence, 15), "%lf", &out->pdop);
    sscanf(field(sentence, 16), "%lf", &out->hdop);
    sscanf(field(sentence, 17), "%lf", &out->vdop);
}

/* ----------------------------------------------------------------------- */

void processGPGSV(char *sentence, struct OUTDATA *out)
{
    int n, m, f = 4;


    if (sscanf(field(sentence, 2), "%d", &n) < 1)
        return;
    update_field_i(sentence, 3, &out->in_view, C_SAT, out);

    n = (n - 1) * 4;
    m = n + 4;

    while (n < out->in_view && n < m) {
	update_field_i(sentence, f++, &out->PRN[n], C_SAT, out);
	update_field_i(sentence, f++, &out->elevation[n], C_SAT, out);
	update_field_i(sentence, f++, &out->azimuth[n], C_SAT, out);
	if (*(field(sentence, f)))
	    update_field_i(sentence, f, &out->ss[n], C_SAT, out);
	f++;
	n++;
    }
}

/* ----------------------------------------------------------------------- */

static void processPRWIZCH(char *sentence, struct OUTDATA *out)
{
    int i;

    for (i = 0; i < 12; i++) {
	update_field_i(sentence, 2 * i + 1, &out->Zs[i], C_ZCH, out);
	update_field_i(sentence, 2 * i + 2, &out->Zv[i], C_ZCH, out);
    }
}

/* ----------------------------------------------------------------------- */

static void do_lat_lon(char *sentence, int begin, struct OUTDATA *out)
{
    double lat, lon, d, m;
    char str[20], *p;
    int updated = 0;


    if (*(p = field(sentence, begin + 0)) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lat);
	m = 100.0 * modf(lat / 100.0, &d);
	lat = d + m / 60.0;
	p = field(sentence, begin + 1);
	if (*p == 'S')
	    lat = -lat;
	if (out->latitude != lat) {
	    out->latitude = lat;
	    out->cmask |= C_LATLON;
	}
	updated++;
    }
    if (*(p = field(sentence, begin + 2)) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lon);
	m = 100.0 * modf(lon / 100.0, &d);
	lon = d + m / 60.0;

	p = field(sentence, begin + 3);
	if (*p == 'W')
	    lon = -lon;
	if (out->longitude != lon) {
	    out->longitude = lon;
	    out->cmask |= C_LATLON;
	}
	updated++;
    }
    if (updated == 2)
	out->ts_latlon = time(NULL);
}

/* ----------------------------------------------------------------------- */

static void update_field_i(char *sentence, int fld, int *dest, int mask, struct OUTDATA *out)
{
    int tmp;

    sscanf(field(sentence, fld), "%d", &tmp);

    if (tmp != *dest) {
	*dest = tmp;
	out->cmask |= mask;
    }
}

#if 0
static void update_field_f(char *sentence, int fld, double *dest, int mask, struct OUTDATA *out)
{
    double tmp;

    scanf(field(sentence, fld), "%lf", &tmp);

    if (tmp != *dest) {
	*dest = tmp;
	out->cmask |= mask;
    }
}
#endif

/* ----------------------------------------------------------------------- */

short checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence, csum[3];

    while ((c = *p++) != '*' && c != '\0')
	sum ^= c;

    sprintf(csum, "%02X", sum);
    return (strncmp(csum, p, 2) == 0);
}

void add_checksum(char *sentence)
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    while ((c = *p++) != '*' && c != '\0')
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
    char c, *p = sentence;
    int i;

    while (n-- > 0)
        while ((c = *p++) != ',' && c != '\0');
    strncpy(result, p, 100);
    p = result;
    i = 0;
    while (*p && *p != ',' && *p != '*' && *p != '\r' && ++i<100)
	p++;

    *p = '\0';
    return result;
}

int process_NMEA_message(char *sentence, struct OUTDATA *outdata)
{
    if (checksum(sentence)) {
	if (strncmp(GPRMC, sentence, 5) == 0) {
	    processGPRMC(sentence, outdata);
	} else if (strncmp(GPGGA, sentence, 5) == 0) {
	    processGPGGA(sentence, outdata);
	} else if (strncmp(GPVTG, sentence, 5) == 0) {
	    processGPVTG(sentence, outdata);
	} else if (strncmp(GPGSA, sentence, 5) == 0) {
	    processGPGSA(sentence, outdata);
	} else if (strncmp(GPGSV, sentence, 5) == 0) {
	    processGPGSV(sentence, outdata);
	} else if (strncmp(PRWIZCH, sentence, 7) == 0) {
	    processPRWIZCH(sentence, outdata);
	} else {
	    return -1;
	}
    }
    return 0;
}

