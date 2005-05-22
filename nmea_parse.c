#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#include "gpsd.h"
#include "timebase.h"

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
	if (out->fix.latitude != lat)
	    out->fix.latitude = lat;
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
	if (out->fix.longitude != lon)
	    out->fix.longitude = lon;
	updated++;
    }
}

/**************************************************************************
 *
 * Scary timestamp fudging begins here
 *
 * Four sentences, GGA and GLL and RMC and ZDA, contain timestamps.
 * Timestamps always look like hhmmss.ss, with the trailing .ss part
 * optional.  RMC has a date field, in the format ddmmyy.  ZDA has
 * separate fields for day/month/year, with a 4-digit year.  This
 * means that for RMC we must supply a century and for GGA and GGL we
 * must supply a century, year, and day.  We get the missing data from
 * a previous RMC or ZDA; century in RMC is supplied by the host
 * machine's clock time if there has been no previous RMC.
 *
 **************************************************************************/

#define DD(s)	((s)[0]-'0')*10+((s)[1]-'0')

static void merge_ddmmyy(char *ddmmyy, struct gps_data_t *out)
/* sentence supplied ddmmyy, but no century part */
{
    if (!out->nmea_date.tm_year)
	out->nmea_date.tm_year = (CENTURY_BASE + DD(ddmmyy+4)) - 1900;
    out->nmea_date.tm_mon = DD(ddmmyy+2)-1;
    out->nmea_date.tm_mday = DD(ddmmyy);
}

static void merge_hhmmss(char *hhmmss, struct gps_data_t *out)
/* update from a UTC time */
{
    int old_hour = out->nmea_date.tm_hour;

    out->nmea_date.tm_hour = DD(hhmmss);
	if (out->nmea_date.tm_hour < old_hour)	/* midnight wrap */ 
	out->nmea_date.tm_mday++;
    out->nmea_date.tm_min = DD(hhmmss+2);
    out->nmea_date.tm_sec = DD(hhmmss+4);
    out->subseconds = atof(hhmmss+4) - out->nmea_date.tm_sec;
}

#undef DD

/**************************************************************************
 *
 * NMEA sentence handling begins here
 *
 **************************************************************************/

static int processGPRMC(int count, char *field[], struct gps_data_t *out)
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
    int mask = ERROR_SET;

    if (!strcmp(field[2], "V")) {
	/* copes with Magellan EC-10X, see below */
	if (out->status != STATUS_NO_FIX) {
	    out->status = STATUS_NO_FIX;
	    mask |= STATUS_SET;
	}
	if (out->fix.mode >= MODE_2D) {
	    out->fix.mode = MODE_NO_FIX;
	    mask |= MODE_SET;
	}
    } else if (!strcmp(field[2], "A")) {
	if (count > 9) {
	    merge_ddmmyy(field[9], out);
	    merge_hhmmss(field[1], out);
	    out->fix.time = out->sentence_time = mkgmtime(&out->nmea_date) + out->subseconds;
	}
	mask = TIME_SET;
	do_lat_lon(&field[3], out);
	mask |= LATLON_SET;
	out->fix.speed = atof(field[7]) * KNOTS_TO_MPS;
	out->fix.track = atof(field[8]);
	mask |= (TRACK_SET | SPEED_SET);
	/*
	 * This copes with GPSes like the Magellan EC-10X that *only* emit
	 * GPRMC. In this case we set mode and status here so the client
	 * code that relies on them won't mistakenly believe it has never
	 * received a fix.
	 */
	if (out->status == STATUS_NO_FIX) {
	    out->status = STATUS_FIX;	/* could be DGPS_FIX, we can't tell */
	    mask |= STATUS_SET;
	}
	if (out->fix.mode < MODE_2D) {
	    out->fix.mode = MODE_2D;
	    mask |= MODE_SET;
	}
    }

    return mask;
}

static int processGPGLL(int count, char *field[], struct gps_data_t *out)
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
     * 
     * Unless you care about the FAA indicator, this sentence supplies nothing
     * that GPRMC doesn't already.  But at least one Garmin GPS -- the 48
     * actually ships updates in GPLL that aren't redundant.
     */
    char *status = field[7];
    int mask = ERROR_SET;

    if (!strcmp(field[6], "A") && (count < 8 || *status != 'N')) {
	int newstatus = out->status;

	mask = 0;
	merge_hhmmss(field[5], out);
	if (out->nmea_date.tm_year) {
	    out->fix.time = out->sentence_time = mkgmtime(&out->nmea_date) + out->subseconds;
	    mask = TIME_SET;
	}
	do_lat_lon(&field[1], out);
	mask |= LATLON_SET;
	if (count >= 8 && *status == 'D')
	    newstatus = STATUS_DGPS_FIX;	/* differential */
	else
	    newstatus = STATUS_FIX;
	out->status = newstatus;
	mask |= STATUS_SET;
	gpsd_report(3, "GPGLL sets status %d\n", out->status);
    }

    return mask;
}

static int processGPGGA(int c UNUSED, char *field[], struct gps_data_t *out)
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
    int mask;

    out->status = atoi(field[6]);
    gpsd_report(3, "GPGGA sets status %d\n", out->status);
    mask = STATUS_SET;
    if (out->status > STATUS_NO_FIX) {
	char *altitude;
	double oldfixtime = out->fix.time;

	merge_hhmmss(field[1], out);
	if (out->nmea_date.tm_year) {
	    out->fix.time = out->sentence_time = mkgmtime(&out->nmea_date) + out->subseconds;
	    mask |= TIME_SET;
	}
	do_lat_lon(&field[2], out);
	mask |= LATLON_SET;
        out->satellites_used = atoi(field[7]);
	altitude = field[9];
	/*
	 * SiRF chipsets up to version 2.2 report a null altitude field.
	 * See <http://www.sirf.com/Downloads/Technical/apnt0033.pdf>.
	 * If we see this, force mode to 2D at most.
	 */
	if (!altitude[0]) {
	    if (out->fix.mode == MODE_3D) {
		out->fix.mode = out->status ? MODE_2D : MODE_NO_FIX; 
		mask |= MODE_SET;
	    }
	} else {
	    double oldaltitude = out->fix.altitude;

	    out->fix.altitude = atof(altitude);
	    mask |= ALTITUDE_SET;


	    /*
	     * Compute climb/sink in the simplest possible way.
	     * This substitutes for the climb report provided by
	     * SiRF and Garmin chips, which might have some smoothing
	     * going on.
	     */
	    if (oldaltitude == ALTITUDE_NOT_VALID || out->fix.time==oldfixtime)
		out->fix.climb = 0;
	    else {
		out->fix.climb = (out->fix.altitude-oldaltitude)/(out->fix.time-oldfixtime);
	    }
	    mask |= CLIMB_SET;
	}
	if ( strlen( field[11] ) ) {
	   out->fix.separation = atof(field[11]);
	} else {
	   out->fix.separation = wgs84_separation(out->fix.latitude,out->fix.longitude);
	}
    }
    return mask;
}

static int processGPGSA(int c UNUSED, char *field[], struct gps_data_t *out)
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
    int i, mask;
    
    out->fix.mode = atoi(field[2]);
    mask = MODE_SET;
    gpsd_report(3, "GPGSA sets mode %d\n", out->fix.mode);
    out->pdop = atof(field[15]);
    out->hdop = atof(field[16]);
    out->vdop = atof(field[17]);
    out->satellites_used = 0;
    memset(out->used,0,sizeof(out->used));
    for (i = 0; i < MAXCHANNELS; i++) {
        int prn = atoi(field[i+3]);
        if (prn > 0)
	    out->used[out->satellites_used++] = prn;
    }
    mask |= HDOP_SET | VDOP_SET | PDOP_SET;

    return mask;
}

static int processGPGSV(int count, char *field[], struct gps_data_t *out)
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
    int n, fldnum;
    if (count <= 3) {
	gpsd_zero_satellites(out);
        return ERROR_SET;
    }
    out->await = atoi(field[1]);
    if (sscanf(field[2], "%d", &out->part) < 1) {
	gpsd_zero_satellites(out);
        return ERROR_SET;
    } else if (out->part == 1)
	gpsd_zero_satellites(out);

    for (fldnum = 4; fldnum < count; ) {
	if (out->satellites >= MAXCHANNELS) {
	    gpsd_report(0, "internal error - too many satellites!\n");
	    gpsd_zero_satellites(out);
	    break;
	}
	out->PRN[out->satellites]       = atoi(field[fldnum++]);
	out->elevation[out->satellites] = atoi(field[fldnum++]);
	out->azimuth[out->satellites]   = atoi(field[fldnum++]);
	out->ss[out->satellites]        = atoi(field[fldnum++]);
	/*
	 * Incrementing this unconditionally falls afoul of chipsets like 
	 * the Motorola Oncore GT+ that emit empty fields at the end of the
	 * last sentence in a GPGSV set if the number of satellites is not
	 * a multiiple of 4.
	 */
	if (out->PRN[out->satellites])
	    out->satellites++;
    }
    if (out->part == out->await && atoi(field[3]) != out->satellites)
	gpsd_report(0, "GPGSV field 3 value of %d != actual count %d\n",
		    atoi(field[3]), out->satellites);

    /* not valid data until we've seen a complete set of parts */
    if (out->part < out->await) {
	gpsd_report(3, "Partial satellite data (%d of %d).\n", out->part, out->await);
	return ERROR_SET;
    }
    /*
     * This sanity check catches an odd behavior of SiRF-II based GPSes.
     * When they can't see any satellites at all (like, inside a
     * building) they sometimes cough up a hairball in the form of a
     * GSV packet with all the azimuth entries 0 (but nonzero
     * elevations).  This behavior was observed under SiRF firmware
     * revision 231.000.000_A2.
     */
    for (n = 0; n < out->satellites; n++)
	if (out->azimuth[n])
	    goto sane;
    gpsd_report(3, "Satellite data no good.\n");
    gpsd_zero_satellites(out);
    return ERROR_SET;
  sane:
    gpsd_report(3, "Satellite data OK.\n");
    return SATELLITE_SET;
    }

static int processPGRME(int c UNUSED, char *field[], struct gps_data_t *out)
/* Garmin Estimated Position Error */
{
    /*
       $PGRME,15.0,M,45.0,M,25.0,M*22
	1    = horizontal error estimate
        2    = units
	3    = vertical error estimate
        4    = units
	5    = spherical error estimate
        6    = units
     *
     * Garmin won't say, but the general belief is that these are 1-sigma.
     * See <http://gpsinformation.net/main/epenew.txt>.
     */
    out->fix.eph = atof(field[1]);
    out->fix.epv = atof(field[3]);
    out->epe = atof(field[5]);
    return HERR_SET | VERR_SET | PERR_SET;
}

static int processGPZDA(int c UNUSED, char *field[], struct gps_data_t *out)
/* Time & Date */
{
    /*
      $GPZDA,160012.71,11,03,2004,-1,00*7D
      1) UTC time (hours, minutes, seconds, may have fractional subsecond)
      2) Day, 01 to 31
      3) Month, 01 to 12
      4) Year (4 digits)
      5) Local zone description, 00 to +- 13 hours
      6) Local zone minutes description, apply same sign as local hours
      7) Checksum
     */
    merge_hhmmss(field[1], out);
    out->nmea_date.tm_year = atoi(field[4]) - 1900;
    out->nmea_date.tm_mon = atoi(field[3]);
    out->nmea_date.tm_mday = atoi(field[2]);
    out->fix.time = out->sentence_time = mkgmtime(&out->nmea_date) + out->subseconds;
    return TIME_SET;
}

#ifdef __UNUSED__
static short nmea_checksum(char *sentence, unsigned char *correct_sum)
/* is the checksum on the specified sentence good? */
{
    unsigned char sum = '\0';
    char c, *p = sentence, csum[3];

    while ((c = *p++) != '*' && c != '\0')
	sum ^= c;
    if (correct_sum)
        *correct_sum = sum;
    snprintf(csum, sizeof(csum), "%02X", sum);
    return(toupper(csum[0])==toupper(p[0]))&&(toupper(csum[1])==toupper(p[1]));
}
#endif /* __ UNUSED__ */

/**************************************************************************
 *
 * Entry points begin here
 *
 **************************************************************************/

void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$') {
	p++;
    } else {
        gpsd_report(1, "Bad NMEA sentence: '%s'\n", sentence);
    }
    while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    sprintf(p, "%02X\r\n", sum);
}

int nmea_parse(char *sentence, struct gps_data_t *outdata)
/* parse an NMEA sentence, unpack it into a session structure */
{
    typedef int (*nmea_decoder)(int count, char *f[], struct gps_data_t *out);
    static struct {
	char *name;
	int mask;
	nmea_decoder decoder;
    } nmea_phrase[] = {
	{"RMC", 	GPRMC,	processGPRMC},
	{"GGA", 	GPGGA,	processGPGGA},
	{"GLL", 	GPGLL,	processGPGLL},
	{"GSA", 	GPGSA,	processGPGSA},
	{"GSV", 	GPGSV,	processGPGSV},
	{"ZDA", 	GPZDA,	processGPZDA},
	{"PGRME",	PGRME,	processPGRME},
    };
    unsigned char buf[NMEA_MAX+1];

    int count, retval = 0;
    unsigned int i;
    char *p, *field[80], *s;
#ifdef __UNUSED__
    unsigned char sum;

    if (!nmea_checksum(sentence+1, &sum)) {
        gpsd_report(1, "Bad NMEA checksum: '%s' should be %02X\n",
                   sentence, sum);
        return 0;
    }
#endif /* __ UNUSED__ */

    /* make an editable copy of the sentence */
    strncpy(buf, sentence, NMEA_MAX);
    /* discard the checksum part */
    for (p = buf; (*p != '*') && (*p >= ' '); ) ++p;
    *p = '\0';
    /* split sentence copy on commas, filling the field array */
    for (count = 0, p = buf; p != NULL && *p; ++count, p = strchr(p, ',')) {
	*p = 0;
	field[count] = ++p;
    }
    /* dispatch on field zero, the sentence tag */
    for (i = 0; i < sizeof(nmea_phrase)/sizeof(nmea_phrase[0]); ++i) {
	s = field[0];
	if (strlen(nmea_phrase[i].name) == 3)
	    s += 2;	/* skip talker ID */
        if (!strcmp(nmea_phrase[i].name, s)) {
	    if (nmea_phrase[i].decoder) {
		retval = (nmea_phrase[i].decoder)(count, field, outdata);
		strncpy(outdata->tag, nmea_phrase[i].name, MAXTAGLEN);
		outdata->sentence_length = strlen(sentence);
	    }
	    if (nmea_phrase[i].mask)
		outdata->seen_sentences |= nmea_phrase[i].mask;
	    break;
	}
    }
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
