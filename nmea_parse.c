/* AIX requires this to be the first thing in the file.  */
#ifndef __GNUC__
# if HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
char *alloca ();
#   endif
#  endif
# endif
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "gpsd.h"

/**************************************************************************
 *
 * Parser helpers begin here
 *
 **************************************************************************/

static void do_lat_lon(char *field[], struct gps_data_t *out)
/* process a pair of latitude/longitude fields starting at field index BEGIN */
{
    double lat, lon, d, m;
    char str[20], *p;
    int updated = 0;

    if (*(p = field[0]) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lat);
	m = 100.0 * modf(lat / 100.0, &d);
	lat = d + m / 60.0;
	p = field[1];
	if (*p == 'S')
	    lat = -lat;
	if (out->latitude != lat)
	    out->latitude = lat;
	updated++;
    }
    if (*(p = field[2]) != '\0') {
	strncpy(str, p, 20);
	sscanf(p, "%lf", &lon);
	m = 100.0 * modf(lon / 100.0, &d);
	lon = d + m / 60.0;

	p = field[3];
	if (*p == 'W')
	    lon = -lon;
	if (out->longitude != lon)
	    out->longitude = lon;
	updated++;
    }
    if (updated == 2)
	out->latlon_stamp.changed = 1;
    REFRESH(out->latlon_stamp);
}

static int update_field_i(char *field, int *dest)
/* update an integer-valued field */
{
    int tmp, changed;

    tmp = atoi(field);
    changed = (tmp != *dest);
    *dest = tmp;
    return changed;
}

static int update_field_f(char *field, double *dest)
/* update a float-valued field */
{
    int changed;
    double tmp;

    tmp = atof(field);
    changed = (tmp != *dest);
    *dest = tmp;
    return changed;
}

/**************************************************************************
 *
 * Scary timestamp fudging begins here
 *
 **************************************************************************/

/*
   Three sentences, GGA and GGL and RMC, contain timestamps. Timestamps 
   always look like hhmmss.ss, with the trailing .ss part optional.
   RMC alone has a date field, in the format ddmmyy.  But we want the 
   output to be in ISO 8601 format:

   yyyy-mm-ddThh:mm:ss.sssZ
   012345678901234567890123

   (where part or all of the decimal second suffix may be omitted).
   This means that for GPRMC we must supply a century and for GGA and
   GGL we must supply a century, year, and day.  We get the missing data 
   from the host machine's clock time.
 */

static void merge_ddmmyy(char *ddmmyy, struct gps_data_t *out)
/* sentence supplied ddmmyy, but no century part */
{
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);

    strftime(out->utc, 3, "%C", tm);
    strncpy(out->utc+2, ddmmyy + 4, 2);	/* copy year */
    out->utc[4] = '-';
    strncpy(out->utc+5, ddmmyy + 2, 2);	/* copy month */
    out->utc[7] = '-';
    strncpy(out->utc+8, ddmmyy, 2);	/* copy date */
    out->utc[10] = 'T';
}

static void fake_mmddyyyy(struct gps_data_t *out)
/* sentence didn't supply mm/dd/yyy, so we have to fake it */
{
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);

    strftime(out->utc, sizeof(out->utc), "%Y-%m-%dT", tm);
}

static void merge_hhmmss(char *hhmmss, struct gps_data_t *out)
/* update last-fix field from a UTC time */
{
    strncpy(out->utc+11, hhmmss, 2);	/* copy hours */
    out->utc[13] = ':';
    strncpy(out->utc+14, hhmmss+2, 2);	/* copy minutes */
    out->utc[16] = ':';
    strncpy(out->utc+17 , hhmmss+4, sizeof(out->utc)-17);	/* copy seconds */
    strcat(out->utc, "Z");
}

/**************************************************************************
 *
 * NMEA sentence handling begins here
 *
 **************************************************************************/

static void processGPRMC(int count, char *field[], struct gps_data_t *out)
/* Recommend Minimum Specific GPS/TRANSIT Data */
{
    /*
        RMC,225446.33,A,4916.45,N,12311.12,W,000.5,054.7,191194,020.3,E,A*68
           225446.33    Time of fix 22:54:46 UTC
           A            Navigation receiver warning A = OK, V = warning
           4916.45,N    Latitude 49 deg. 16.45 min North
           12311.12,W   Longitude 123 deg. 11.12 min West
           000.5        Speed over ground, Knots
           054.7        Course Made Good, True
           191194       Date of fix  19 November 1994
           020.3,E      Magnetic variation 20.3 deg East
	   A            FAA mode indicator (NMEA 2.3 and later)
                        A=autonomous, D=differential, E=Estimated,
                        N=not valid, S=Simulator
           *68          mandatory nmea_checksum

     * SiRF chipsets don't return either Mode Indicator or magnetic variation.
     */
    if (count > 9) {
	merge_ddmmyy(field[9], out);
	merge_hhmmss(field[1], out);
    }
    if (!strcmp(field[2], "A")) {
	do_lat_lon(&field[3], out);
	out->speed_stamp.changed = update_field_f(field[7], &out->speed);
	REFRESH(out->speed_stamp);
	out->track_stamp.changed = update_field_f(field[8], &out->track);
	REFRESH(out->track_stamp);
    }
}

static void processGPGLL(int count, char *field[], struct gps_data_t *out)
/* Geographic position - Latitude, Longitude */
{
    /* Introduced in NMEA 3.0.  Here are the fields:
     *
     * 1,2 Latitude, N (North) or S (South)
     * 3,4 Longitude, E (East) or W (West)
     * 5 UTC of position
     * 6 A=Active, V=Void
     * 7 Mode Indicator
     *   A = Autonomous mode
     *   D = Differential Mode
     *   E = Estimated (dead-reckoning) mode
     *   M = Manual Input Mode
     *   S = Simulated Mode
     *   N = Data Not Valid
     *
     * I found a note at <http://www.secoh.ru/windows/gps/nmfqexep.txt>
     * indicating that the Garmin 65 does not return time and status.
     * SiRF chipsets don't return the Mode Indicator.
     * This code copes gracefully with both quirks.
     */
    char *status = field[7];

    fake_mmddyyyy(out);
    merge_hhmmss(field[5], out);
    if (!strcmp(field[6], "A") && (count < 8 || *status != 'N')) {
	int newstatus = out->status;

	do_lat_lon(&field[1], out);
	if (count >= 8 && *status == 'D')
	    newstatus = STATUS_DGPS_FIX;	/* differential */
	else
	    newstatus = STATUS_FIX;
	out->status_stamp.changed = (out->status != newstatus);
	out->status = newstatus;
	REFRESH(out->status_stamp);
	gpsd_report(3, "GPGLL sets status %d\n", out->status);
    }
}

static void processGPVTG(int c UNUSED, char *field[], struct gps_data_t *out)
/* Track Made Good and Ground Speed */
{
    /* There are two variants of GPVTG.  One looks like this:

	(1) True course over ground (degrees) 000 to 359
	(2) Magnetic course over ground 000 to 359
	(3) Speed over ground (knots) 00.0 to 99.9
	(4) Speed over ground (kilometers) 00.0 to 99.9

     * Up to and including 1.10, gpsd assumed this and extracted field 3.
     * But I'm told the NMEA spec, version 3.01, dated 1/1/2002, gives this:

	1    = Track made good
	2    = Fixed text 'T' indicates that track made good is relative to 
	       true north
	3    = Magnetic course over ground
	4    = Fixed text 'M' indicates course is relative to magnetic north.
	5    = Speed over ground in knots
	6    = Fixed text 'N' indicates that speed over ground in in knots
	7    = Speed over ground in kilometers/hour
	8    = Fixed text 'K' indicates that speed over ground is in km/h.
	9    = Checksum

     * which means we want to extract field 5.  We cope with both.
     */
    out->track_stamp.changed = update_field_f(field[1], &out->track);;
    REFRESH(out->track_stamp);
    if (field[2][0] == 'T')
	out->speed_stamp.changed = update_field_f(field[5], &out->speed);
    else
	out->speed_stamp.changed = update_field_f(field[3], &out->speed);
    REFRESH(out->speed_stamp);
}

static void processGPGGA(int c UNUSED, char *field[], struct gps_data_t *out)
/* Global Positioning System Fix Data */
{
    /*
        GGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M, , *42
           123519       Fix taken at 12:35:19 UTC
           4807.038,N   Latitude 48 deg 07.038' N
           01131.324,E  Longitude 11 deg 31.324' E
           1            Fix quality: 0 = invalid, 1 = GPS fix, 2 = DGPS fix
           08           Number of satellites being tracked
           0.9          Horizontal dilution of position
           545.4,M      Altitude, Metres above mean sea level
           46.9,M       Height of geoid (mean sea level) above WGS84
                        ellipsoid, in Meters
           (empty field) time in seconds since last DGPS update
           (empty field) DGPS station ID number (0000-1023)
    */
    fake_mmddyyyy(out);
    merge_hhmmss(field[1], out);
    out->status_stamp.changed = update_field_i(field[6], &out->status);
    REFRESH(out->status_stamp);
    gpsd_report(3, "GPGGA sets status %d\n", out->status);
    if (out->status > STATUS_NO_FIX) {
	char	*altitude;

	do_lat_lon(&field[2], out);
        out->satellites_used = atoi(field[7]);
	altitude = field[9];
	/*
	 * SiRF chipsets up to version 2.2 report a null altitude field.
	 * See <http://www.sirf.com/Downloads/Technical/apnt0033.pdf>.
	 * If we see this, force mode to 2D at most.
	 */
	if (!altitude[0]) {
	    if (out->mode == MODE_3D) {
		out->mode = out->status ? MODE_2D : MODE_NO_FIX; 
		out->mode_stamp.changed = 1;
		REFRESH(out->mode_stamp);
	    }
	} else {
	    double newaltitude = atof(altitude);

	    out->altitude_stamp.changed = (newaltitude != out->altitude);
	    out->altitude = newaltitude;
	    REFRESH(out->altitude_stamp);
	}
    }
}

static void processGPGSA(int c UNUSED, char *field[], struct gps_data_t *out)
/* GPS DOP and Active Satellites */
{
    /*
	eg1. $GPGSA,A,3,,,,,,16,18,,22,24,,,3.6,2.1,2.2*3C
	eg2. $GPGSA,A,3,19,28,14,18,27,22,31,39,,,,,1.7,1.0,1.3*35
	1    = Mode:
	       M=Manual, forced to operate in 2D or 3D
	       A=Automatic, 3D/2D
	2    = Mode: 1=Fix not available, 2=2D, 3=3D
	3-14 = PRNs of satellites used in position fix (null for unused fields)
	15   = PDOP
	16   = HDOP
	17   = VDOP
     */
    int i, changed = 0;
    
    out->mode_stamp.changed = update_field_i(field[2], &out->mode);
    REFRESH(out->mode_stamp);
    gpsd_report(3, "GPGSA sets mode %d\n", out->mode);
    changed |= update_field_f(field[15], &out->pdop);
    changed |= update_field_f(field[16], &out->hdop);
    changed |= update_field_f(field[17], &out->vdop);
    for (i = 0; i < MAXCHANNELS; i++)
	out->used[i] = 0;
    out->satellites_used = 0;
    for (i = 0; i < MAXCHANNELS; i++) {
        int prn = atoi(field[i+3]);
        if (prn > 0) {
           out->used[out->satellites_used] = prn;
           out->satellites_used++;
       }
    }
    out->fix_quality_stamp.changed = changed;
    REFRESH(out->fix_quality_stamp);
}

int nmea_sane_satellites(struct gps_data_t *out)
{
    int n;

    /* data may be incomplete */
    if (out->part < out->await)
	return 0;

    /*
     * This sanity check catches an odd behavior SiRF-II based GPSes.
     * When they can't see any satellites at all (like, inside a
     * building) they sometimes cough up a hairball in the form of a
     * GSV packet with all the azimuth entries 0 (but nonzero
     * elevations).  This behavior was observed under SiRF firmware
     * revision 231.000.000_A2.
     */
    for (n = 0; n < out->satellites; n++)
	if (out->azimuth[n])
	    return 1;
    return 0;
}

static void processGPGSV(int count, char *field[], struct gps_data_t *out)
/* GPS Satellites in View */
{
    /*
        GSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75
           2            Number of sentences for full data
           1            sentence 1 of 2
           08           Total number of satellites in view
           01           Satellite PRN number
           40           Elevation, degrees
           083          Azimuth, degrees
           46           Signal-to-noise ratio in decibels
           <repeat for up to 4 satellites per sentence>
                There my be up to three GSV sentences in a data packet
     */
    int changed, fldnum;
    if (count <= 3)
        return;
    out->await = atoi(field[1]);
    if (sscanf(field[2], "%d", &out->part) < 1)
        return;
    else if (out->part == 1)
    {
	memset(out->PRN,       '\0', sizeof(out->PRN));
	memset(out->elevation, '\0', sizeof(out->elevation));
	memset(out->azimuth,   '\0', sizeof(out->azimuth));
	memset(out->ss,        '\0', sizeof(out->ss));
	out->satellites = 0;
    }

    changed = 0;
    for (fldnum = 4; fldnum < count; out->satellites++) {
	changed |= update_field_i(field[fldnum++], &out->PRN[out->satellites]);
	changed |= update_field_i(field[fldnum++], &out->elevation[out->satellites]);
	changed |= update_field_i(field[fldnum++], &out->azimuth[out->satellites]);
	changed |= update_field_i(field[fldnum++], &out->ss[out->satellites]);
    }

    /* not valid data until we've seen a complete set of parts */
    if (out->part < out->await)
	gpsd_report(3, "Partial satellite data (%d of %d).\n", out->part, out->await);
    else if (!nmea_sane_satellites(out))
	gpsd_report(3, "Satellite data no good.\n");
    else {
	gpsd_report(3, "Satellite data OK.\n");
	out->satellite_stamp.changed = changed;
	REFRESH(out->satellite_stamp);
    }
}

static short nmea_checksum(char *sentence, unsigned char *correct_sum)
/* is the checksum on the specified sentence good? */
{
    unsigned char sum = '\0';
    char c, *p = sentence, csum[3];

    while ((c = *p++) != '*' && c != '\0')
	sum ^= c;
    if (correct_sum)
        *correct_sum = sum;
    sprintf(csum, "%02X", sum);
    return(toupper(csum[0])==toupper(p[0]))&&(toupper(csum[1])==toupper(p[1]));
}

/**************************************************************************
 *
 * Entry points begin here
 *
 **************************************************************************/

void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$')
	p++;
    while ((c = *p++) != '*' && c != '\0')
	sum ^= c;
    if (c != '*')
	*p++ = '*';
    sprintf(p, "%02X\r\n", sum);
}

int nmea_parse(char *sentence, struct gps_data_t *outdata)
/* parse an NMEA sentence, unpack it into a session structure */
{
    typedef void (*nmea_decoder)(int count, char *f[], struct gps_data_t *out);
    static struct {
	char *name;
	nmea_decoder decoder;
    } nmea_phrase[] = {
	{"GPRMB", NULL},
	{"GPRMC", processGPRMC},
	{"GPGGA", processGPGGA},
	{"GPGLL", processGPGLL},
	{"GPVTG", processGPVTG},
	{"GPGSA", processGPGSA},
	{"GPGSV", processGPGSV},
	{"PRWIZCH", NULL},
    };

    int retval = -1;
    unsigned int i;
    int count;
    unsigned char sum;
    char *p, *s;
    char *field[80];

    if ( ! nmea_checksum(sentence+1, &sum)) {
        gpsd_report(1, "Bad NMEA checksum: '%s' should be %02X\n",
                   sentence, sum);
        return 0;
    }

    /* make an editable copy of the sentence */
#ifdef AC_FUNC_ALLOCA
    s = alloca(strlen(sentence)+1);
    strcpy(s, sentence);
#else
    s = strdup(sentence);
#endif
    /* discard the checksum part */
    for (p = s; (*p != '*') && (*p >= ' '); ) ++p;
    *p = '\0';
    /* split sentence copy on commas, filling the field array */
    for (count = 0, p = s; p != NULL && *p != 0; ++count, p = strchr(p, ',')) {
	*p = 0;
	field[count] = ++p;
    }
    /* dispatch on field zero, the sentence tag */
    for (i = 0; i < sizeof(nmea_phrase)/sizeof(nmea_phrase[0]); ++i) {
        if (!strcmp(nmea_phrase[i].name, field[0])) {
	    if (nmea_phrase[i].decoder)
		(nmea_phrase[i].decoder)(count, field, outdata);
	    retval = 0;
	    break;
	}
    }
#ifndef AC_FUNC_ALLOCA
    free(s);
#endif
    return retval;
}

int nmea_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
    unsigned int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    strcat(buf, "*");
    nmea_add_checksum(buf);
    status = write(fd, buf, strlen(buf));
    if (status == strlen(buf)) {
	gpsd_report(2, "=> GPS: %s\n", buf);
	return status;
    } else {
	gpsd_report(2, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

int nmea_validate_buffer(char *buf, size_t n)
/* dos this buffer look like it contains valid NMEA? */
{
    char	*sp;

    /*
     * It's OK to have leading garbage, but not trailing garbage.
     * Leading garbage may just mean the port hasn't settled yet
     * after a baud rate change; ignore it.
     */
    for (sp = buf; sp < buf + n && !isprint(*sp); sp++)
	continue;

    /* If no valid NMEA in the read buffer, crap out */
    if (!(sp = strstr(sp, "$GP"))) {
	gpsd_report(4, "no NMEA in the buffer\n");
	return 0;
    }

    /*
     * Trailing garbage means that the data accidentally looked like
     * NMEA or that old data that really was NMEA happened to be sitting
     * in the TTY buffer unread, but the new data we read is not
     * sentences.  Second case shouldn't happen, because we flush the
     * buffer after each speed change, but welcome to serial-programming hell.
     */
    for (sp += 3; sp < buf + n; sp++)
	if (!isascii(*sp)) {
	    gpsd_report(4, "trailing garbage in the buffer\n");
	    return 0;
	}
    return 1;
}
