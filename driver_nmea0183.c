/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "gpsd.h"
#include "strfuncs.h"

#ifdef NMEA0183_ENABLE
/**************************************************************************
 *
 * Parser helpers begin here
 *
 **************************************************************************/

static void do_lat_lon(char *field[], struct gps_fix_t *out)
/* process a pair of latitude/longitude fields starting at field index BEGIN */
{
    double d, m;
    char str[20], *p;

    if (*(p = field[0]) != '\0') {
	double lat;
	(void)strlcpy(str, p, sizeof(str));
	lat = atof(str);
	m = 100.0 * modf(lat / 100.0, &d);
	lat = d + m / 60.0;
	p = field[1];
	if (*p == 'S')
	    lat = -lat;
	out->latitude = lat;
    }
    if (*(p = field[2]) != '\0') {
	double lon;
	(void)strlcpy(str, p, sizeof(str));
	lon = atof(str);
	m = 100.0 * modf(lon / 100.0, &d);
	lon = d + m / 60.0;

	p = field[3];
	if (*p == 'W')
	    lon = -lon;
	out->longitude = lon;
    }
}

/**************************************************************************
 *
 * Scary timestamp fudging begins here
 *
 * Four sentences, GGA and GLL and RMC and ZDA, contain timestamps.
 * GGA/GLL/RMC timestamps look like hhmmss.ss, with the trailing .ss
 * part optional.  RMC has a date field, in the format ddmmyy.  ZDA
 * has separate fields for day/month/year, with a 4-digit year.  This
 * means that for RMC we must supply a century and for GGA and GLL we
 * must supply a century, year, and day.  We get the missing data from
 * a previous RMC or ZDA; century in RMC is supplied from the daemon's
 * context (initialized at startup time) if there has been no previous
 * ZDA.
 *
 **************************************************************************/

#define DD(s)	((int)((s)[0]-'0')*10+(int)((s)[1]-'0'))

static void merge_ddmmyy(char *ddmmyy, struct gps_device_t *session)
/* sentence supplied ddmmyy, but no century part */
{
    int yy = DD(ddmmyy + 4);
    int mon = DD(ddmmyy + 2);
    int mday = DD(ddmmyy);
    int year;

    /* check for century wrap */
    if (session->nmea.date.tm_year % 100 == 99 && yy == 0)
	gpsd_century_update(session, session->context->century + 100);
    year = (session->context->century + yy);

    if ( (1 > mon ) || (12 < mon ) ) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "merge_ddmmyy(%s), malformed month\n",  ddmmyy);
    } else if ( (1 > mday ) || (31 < mday ) ) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "merge_ddmmyy(%s), malformed day\n",  ddmmyy);
    } else {
	gpsd_log(&session->context->errout, LOG_DATA,
		 "merge_ddmmyy(%s) sets year %d\n",
		 ddmmyy, year);
	session->nmea.date.tm_year = year - 1900;
	session->nmea.date.tm_mon = mon - 1;
	session->nmea.date.tm_mday = mday;
    }
}

static void merge_hhmmss(char *hhmmss, struct gps_device_t *session)
/* update from a UTC time */
{
    int old_hour = session->nmea.date.tm_hour;

    session->nmea.date.tm_hour = DD(hhmmss);
    if (session->nmea.date.tm_hour < old_hour)	/* midnight wrap */
	session->nmea.date.tm_mday++;
    session->nmea.date.tm_min = DD(hhmmss + 2);
    session->nmea.date.tm_sec = DD(hhmmss + 4);
    session->nmea.subseconds =
	safe_atof(hhmmss + 4) - session->nmea.date.tm_sec;
}

static void register_fractional_time(const char *tag, const char *fld,
				     struct gps_device_t *session)
{
    if (fld[0] != '\0') {
	session->nmea.last_frac_time =
	    session->nmea.this_frac_time;
	session->nmea.this_frac_time = safe_atof(fld);
	session->nmea.latch_frac_time = true;
	gpsd_log(&session->context->errout, LOG_DATA,
		 "%s: registers fractional time %.2f\n",
		 tag, session->nmea.this_frac_time);
    }
}

/**************************************************************************
 *
 * Compare GPS timestamps for equality.  Depends on the fact that the
 * timestamp granularity of GPS is 1/100th of a second.  Use this to avoid
 * naive float comparisons.
 *
 **************************************************************************/

#define GPS_TIME_EQUAL(a, b) (fabs((a) - (b)) < 0.01)

/**************************************************************************
 *
 * NMEA sentence handling begins here
 *
 **************************************************************************/

static gps_mask_t processRMC(int count, char *field[],
			       struct gps_device_t *session)
/* Recommend Minimum Course Specific GPS/TRANSIT Data */
{
    /*
     * RMC,225446.33,A,4916.45,N,12311.12,W,000.5,054.7,191194,020.3,E,A*68
     * 1     225446.33    Time of fix 22:54:46 UTC
     * 2     A          Status of Fix: A = Autonomous, valid;
     * D = Differential, valid; V = invalid
     * 3,4   4916.45,N    Latitude 49 deg. 16.45 min North
     * 5,6   12311.12,W   Longitude 123 deg. 11.12 min West
     * 7     000.5      Speed over ground, Knots
     * 8     054.7      Course Made Good, True north
     * 9     181194       Date of fix  18 November 1994
     * 10,11 020.3,E      Magnetic variation 20.3 deg East
     * 12    A      FAA mode indicator (NMEA 2.3 and later)
     * A=autonomous, D=differential, E=Estimated,
     * N=not valid, S=Simulator, M=Manual input mode
     * *68        mandatory nmea_checksum
     *
     * * SiRF chipsets don't return either Mode Indicator or magnetic variation.
     */
    gps_mask_t mask = 0;

    if (strcmp(field[2], "V") == 0) {
	/* copes with Magellan EC-10X, see below */
	if (session->gpsdata.status != STATUS_NO_FIX) {
	    session->gpsdata.status = STATUS_NO_FIX;
	    mask |= STATUS_SET;
	}
	if (session->newdata.mode >= MODE_2D) {
	    session->newdata.mode = MODE_NO_FIX;
	    mask |= MODE_SET;
	}
	/* set something nz, so it won't look like an unknown sentence */
	mask |= ONLINE_SET;
    } else if (strcmp(field[2], "A") == 0) {
	/*
	 * The MTK3301, Royaltek RGM-3800, and possibly other
	 * devices deliver bogus time values when the navigation
	 * warning bit is set.
	 */
	if (count > 9 && field[1][0] != '\0' && field[9][0] != '\0') {
	    merge_hhmmss(field[1], session);
	    merge_ddmmyy(field[9], session);
	    mask |= TIME_SET;
	    register_fractional_time(field[0], field[1], session);
	}
	do_lat_lon(&field[3], &session->newdata);
	mask |= LATLON_SET;
	session->newdata.speed = safe_atof(field[7]) * KNOTS_TO_MPS;
	session->newdata.track = safe_atof(field[8]);
	mask |= (TRACK_SET | SPEED_SET);
	/*
	 * This copes with GPSes like the Magellan EC-10X that *only* emit
	 * GPRMC. In this case we set mode and status here so the client
	 * code that relies on them won't mistakenly believe it has never
	 * received a fix.
	 */
	if (session->gpsdata.status == STATUS_NO_FIX) {
	    session->gpsdata.status = STATUS_FIX;	/* could be DGPS_FIX, we can't tell */
	    mask |= STATUS_SET;
	}
	if (session->newdata.mode < MODE_2D) {
	    session->newdata.mode = MODE_2D;
	    mask |= MODE_SET;
	}
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "RMC: ddmmyy=%s hhmmss=%s lat=%.2f lon=%.2f "
	     "speed=%.2f track=%.2f mode=%d status=%d\n",
	     field[9], field[1],
	     session->newdata.latitude,
	     session->newdata.longitude,
	     session->newdata.speed,
	     session->newdata.track,
	     session->newdata.mode,
	     session->gpsdata.status);
    return mask;
}

static gps_mask_t processGLL(int count, char *field[],
			       struct gps_device_t *session)
/* Geographic position - Latitude, Longitude */
{
    /* Introduced in NMEA 3.0.
     *
     * $GPGLL,4916.45,N,12311.12,W,225444,A,A*5C
     *
     * 1,2: 4916.46,N    Latitude 49 deg. 16.45 min. North
     * 3,4: 12311.12,W   Longitude 123 deg. 11.12 min. West
     * 5:   225444       Fix taken at 22:54:44 UTC
     * 6:   A            Data valid
     * 7:   A            Autonomous mode
     * 8:   *5C          Mandatory NMEA checksum
     *
     * 1,2 Latitude, N (North) or S (South)
     * 3,4 Longitude, E (East) or W (West)
     * 5 UTC of position
     * 6 A=Active, V=Void
     * 7 Mode Indicator
     * A = Autonomous mode
     * D = Differential Mode
     * E = Estimated (dead-reckoning) mode
     * M = Manual Input Mode
     * S = Simulated Mode
     * N = Data Not Valid
     *
     * I found a note at <http://www.secoh.ru/windows/gps/nmfqexep.txt>
     * indicating that the Garmin 65 does not return time and status.
     * SiRF chipsets don't return the Mode Indicator.
     * This code copes gracefully with both quirks.
     *
     * Unless you care about the FAA indicator, this sentence supplies nothing
     * that GPRMC doesn't already.  But at least one Garmin GPS -- the 48
     * actually ships updates in GLL that aren't redundant.
     */
    char *status = field[7];
    gps_mask_t mask = 0;

    if (field[5][0] != '\0') {
	merge_hhmmss(field[5], session);
	register_fractional_time(field[0], field[5], session);
	if (session->nmea.date.tm_year == 0)
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "can't use GLL time until after ZDA or RMC has supplied a year.\n");
	else {
	    mask = TIME_SET;
	}
    }
    if (strcmp(field[6], "A") == 0 && (count < 8 || *status != 'N')) {
	int newstatus;

	do_lat_lon(&field[1], &session->newdata);
	mask |= LATLON_SET;
	if (count >= 8 && *status == 'D')
	    newstatus = STATUS_DGPS_FIX;	/* differential */
	else
	    newstatus = STATUS_FIX;
	/*
	 * This is a bit dodgy.  Technically we shouldn't set the mode
	 * bit until we see GSA.  But it may be later in the cycle,
	 * some devices like the FV-18 don't send it by default, and
	 * elsewhere in the code we want to be able to test for the
	 * presence of a valid fix with mode > MODE_NO_FIX.
	 */
	if (session->newdata.mode < MODE_2D) {
	    session->newdata.mode = MODE_2D;
	    mask |= MODE_SET;
	}
	session->gpsdata.status = newstatus;
	mask |= STATUS_SET;
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GLL: hhmmss=%s lat=%.2f lon=%.2f mode=%d status=%d\n",
	     field[5],
	     session->newdata.latitude,
	     session->newdata.longitude,
	     session->newdata.mode,
	     session->gpsdata.status);
    return mask;
}

static gps_mask_t processGGA(int c UNUSED, char *field[],
			       struct gps_device_t *session)
/* Global Positioning System Fix Data */
{
    /*
     * GGA,123519,4807.038,N,01131.324,E,1,08,0.9,545.4,M,46.9,M, , *42
     * 1     123519       Fix taken at 12:35:19 UTC
     * 2,3   4807.038,N   Latitude 48 deg 07.038' N
     * 4,5   01131.324,E  Longitude 11 deg 31.324' E
     * 6         1            Fix quality: 0 = invalid, 1 = GPS, 2 = DGPS,
     * 3=PPS (Precise Position Service),
     * 4=RTK (Real Time Kinematic) with fixed integers,
     * 5=Float RTK, 6=Estimated, 7=Manual, 8=Simulator
     * 7     08       Number of satellites being tracked
     * 8     0.9              Horizontal dilution of position
     * 9,10  545.4,M      Altitude, Metres above mean sea level
     * 11,12 46.9,M       Height of geoid (mean sea level) above WGS84
     * ellipsoid, in Meters
     * (empty field) time in seconds since last DGPS update
     * (empty field) DGPS station ID number (0000-1023)
     */
    gps_mask_t mask;

    session->gpsdata.status = atoi(field[6]);
    mask = STATUS_SET;
    /*
     * There are some receivers (the Trimble Placer 450 is an example) that
     * don't ship a GSA with mode 1 when they lose satellite lock. Instead
     * they just keep reporting GGA and GSA on subsequent cycles with the
     * timestamp not advancing and a bogus mode.  On the assumption that GGA
     * is only issued once per cycle we can detect this here (it would be
     * nicer to do it on GSA but GSA has no timestamp).
     */
    session->nmea.latch_mode = strncmp(field[1],
					      session->nmea.last_gga_timestamp,
					      sizeof(session->nmea.last_gga_timestamp))==0;
    if (session->nmea.latch_mode) {
	session->gpsdata.status = STATUS_NO_FIX;
	session->newdata.mode = MODE_NO_FIX;
    } else
	(void)strlcpy(session->nmea.last_gga_timestamp,
		       field[1],
		       sizeof(session->nmea.last_gga_timestamp));
    /* if we have a fix and the mode latch is off, go... */
    if (session->gpsdata.status > STATUS_NO_FIX) {
	char *altitude;

	merge_hhmmss(field[1], session);
	register_fractional_time(field[0], field[1], session);
	if (session->nmea.date.tm_year == 0)
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "can't use GGA time until after ZDA or RMC has supplied a year.\n");
	else {
	    mask |= TIME_SET;
	}
	do_lat_lon(&field[2], &session->newdata);
	mask |= LATLON_SET;
	session->gpsdata.satellites_used = atoi(field[7]);
	altitude = field[9];
	/*
	 * SiRF chipsets up to version 2.2 report a null altitude field.
	 * See <http://www.sirf.com/Downloads/Technical/apnt0033.pdf>.
	 * If we see this, force mode to 2D at most.
	 */
	if (altitude[0] == '\0') {
	    if (session->newdata.mode > MODE_2D) {
		session->newdata.mode = MODE_2D;
                mask |= MODE_SET;
	    }
	} else {
	    session->newdata.altitude = safe_atof(altitude);
	    mask |= ALTITUDE_SET;
	    /*
	     * This is a bit dodgy.  Technically we shouldn't set the mode
	     * bit until we see GSA.  But it may be later in the cycle,
	     * some devices like the FV-18 don't send it by default, and
	     * elsewhere in the code we want to be able to test for the
	     * presence of a valid fix with mode > MODE_NO_FIX.
	     */
	    if (session->newdata.mode < MODE_3D) {
		session->newdata.mode = MODE_3D;
		mask |= MODE_SET;
	    }
	}
	if (strlen(field[11]) > 0) {
	    session->gpsdata.separation = safe_atof(field[11]);
	} else {
	    session->gpsdata.separation =
		wgs84_separation(session->newdata.latitude,
				 session->newdata.longitude);
	}
    }
    gpsd_log(&session->context->errout, LOG_DATA,
	     "GGA: hhmmss=%s lat=%.2f lon=%.2f alt=%.2f mode=%d status=%d\n",
	     field[1],
	     session->newdata.latitude,
	     session->newdata.longitude,
	     session->newdata.altitude,
	     session->newdata.mode,
	     session->gpsdata.status);
    return mask;
}


static gps_mask_t processGST(int count, char *field[], struct gps_device_t *session)
/* GST - GPS Pseudorange Noise Statistics */
{
    /*
     * GST,hhmmss.ss,x,x,x,x,x,x,x,*hh
     * 1 TC time of associated GGA fix
     * 2 Total RMS standard deviation of ranges inputs to the navigation solution
     * 3 Standard deviation (meters) of semi-major axis of error ellipse
     * 4 Standard deviation (meters) of semi-minor axis of error ellipse
     * 5 Orientation of semi-major axis of error ellipse (true north degrees)
     * 6 Standard deviation (meters) of latitude error
     * 7 Standard deviation (meters) of longitude error
     * 8 Standard deviation (meters) of altitude error
     * 9 Checksum
*/
    if (count < 8) {
      return 0;
    }

#define PARSE_FIELD(n) (*field[n]!='\0' ? safe_atof(field[n]) : NAN)
    session->gpsdata.gst.utctime             = PARSE_FIELD(1);
    session->gpsdata.gst.rms_deviation       = PARSE_FIELD(2);
    session->gpsdata.gst.smajor_deviation    = PARSE_FIELD(3);
    session->gpsdata.gst.sminor_deviation    = PARSE_FIELD(4);
    session->gpsdata.gst.smajor_orientation  = PARSE_FIELD(5);
    session->gpsdata.gst.lat_err_deviation   = PARSE_FIELD(6);
    session->gpsdata.gst.lon_err_deviation   = PARSE_FIELD(7);
    session->gpsdata.gst.alt_err_deviation   = PARSE_FIELD(8);
#undef PARSE_FIELD
    register_fractional_time(field[0], field[1], session);

    gpsd_log(&session->context->errout, LOG_DATA,
	     "GST: utc = %.2f, rms = %.2f, maj = %.2f, min = %.2f, ori = %.2f, lat = %.2f, lon = %.2f, alt = %.2f\n",
	     session->gpsdata.gst.utctime,
	     session->gpsdata.gst.rms_deviation,
	     session->gpsdata.gst.smajor_deviation,
	     session->gpsdata.gst.sminor_deviation,
	     session->gpsdata.gst.smajor_orientation,
	     session->gpsdata.gst.lat_err_deviation,
	     session->gpsdata.gst.lon_err_deviation,
	     session->gpsdata.gst.alt_err_deviation);

    return GST_SET | ONLINE_SET;
}

static int nmeaid_to_prn(char *talker, int satnum)
/* deal with range-mapping attempts to to use IDs 1-32 by Beidou, etc. */
{
    /*
     * According to https://github.com/mvglasow/satstat/wiki/NMEA-IDs
     * NMEA IDs can be roughly divided into the following ranges:
     *
     *   1..32:  GPS
     *   33..54: Various SBAS systems (EGNOS, WAAS, SDCM, GAGAN, MSAS)
     *           ... some IDs still unused
     *   55..64: not used (might be assigned to further SBAS systems)
     *   65..88: GLONASS
     *   89..96: GLONASS (future extensions?)
     *   97..192: not used (SBAS PRNs 120-151 fall in here)
     *   193..195: QZSS
     *   196..200: QZSS (future extensions?)
     *   201..235: Beidou
     *
     * The issue is what to do when GPSes from these different systems
     * fight for IDs in the  1-32 range, as in this pair of Beidou sentences
     *
     * $BDGSV,2,1,07,01,00,000,45,02,13,089,35,03,00,000,37,04,00,000,42*6E
     * $BDGSV,2,2,07,05,27,090,,13,19,016,,11,07,147,*5E
     *
     * Because the PRNs are only used for generating a satellite
     * chart, mistakes here aren't dangerous.  The code will record
     * and use multiple sats with the same ID in one skyview; in
     * effect, they're recorded by the order in which they occur
     * rather than by PRN.
     */
    // NMEA-ID (33..64) to SBAS PRN 120-151.
    if (satnum >= 33 && satnum <= 64)
	satnum += 87;
    if (satnum < 32) {
	/* map Beidou IDs */
	if (talker[0] == 'B' && talker[1] == 'D')
	    satnum += 200;
	else if (talker[0] == 'G' && talker[1] == 'B')
	    satnum += 200;
	/* GLONASS GL doesn't seem to do this, but better safe than sorry */
	if (talker[0] == 'G' && (talker[1] == 'L' || talker[1] == 'N'))
	    satnum += 37;
	/* QZSS */
	if (talker[0] == 'Q' && talker[1] == 'Z')
	    satnum += 193;
    }

    return satnum;
}

static gps_mask_t processGSA(int count, char *field[],
			       struct gps_device_t *session)
/* GPS DOP and Active Satellites */
{
    /*
     * eg1. $GPGSA,A,3,,,,,,16,18,,22,24,,,3.6,2.1,2.2*3C
     * eg2. $GPGSA,A,3,19,28,14,18,27,22,31,39,,,,,1.7,1.0,1.3*35
     * 1    = Mode:
     * M=Manual, forced to operate in 2D or 3D
     * A=Automatic, 3D/2D
     * 2    = Mode: 1=Fix not available, 2=2D, 3=3D
     * 3-14 = PRNs of satellites used in position fix (null for unused fields)
     * 15   = PDOP
     * 16   = HDOP
     * 17   = VDOP
     */
    gps_mask_t mask;

    /*
     * One chipset called the i.Trek M3 issues GPGSA lines that look like
     * this: "$GPGSA,A,1,,,,*32" when it has no fix.  This is broken
     * in at least two ways: it's got the wrong number of fields, and
     * it claims to be a valid sentence (A flag) when it isn't.
     * Alarmingly, it's possible this error may be generic to SiRFstarIII.
     */
    if (count < 17) {
	gpsd_log(&session->context->errout, LOG_DATA,
		 "GPGSA: malformed, setting ONLINE_SET only.\n");
	mask = ONLINE_SET;
    } else if (session->nmea.latch_mode) {
	/* last GGA had a non-advancing timestamp; don't trust this GSA */
	mask = ONLINE_SET;
    } else {
	int i;
	session->newdata.mode = atoi(field[2]);
	/*
	 * The first arm of this conditional ignores dead-reckoning
	 * fixes from an Antaris chipset. which returns E in field 2
	 * for a dead-reckoning estimate.  Fix by Andreas Stricker.
	 */
	if (session->newdata.mode == 0 && field[2][0] == 'E')
	    mask = 0;
	else
	    mask = MODE_SET;
	gpsd_log(&session->context->errout, LOG_PROG,
		 "GPGSA sets mode %d\n", session->newdata.mode);
	if (field[15][0] != '\0')
	    session->gpsdata.dop.pdop = safe_atof(field[15]);
	if (field[16][0] != '\0')
	    session->gpsdata.dop.hdop = safe_atof(field[16]);
	if (field[17][0] != '\0')
	    session->gpsdata.dop.vdop = safe_atof(field[17]);
	session->gpsdata.satellites_used = 0;
	memset(session->nmea.sats_used, 0, sizeof(session->nmea.sats_used));
	/* the magic 6 here counts the tag, two mode fields, and the DOP fields */
	for (i = 0; i < count - 6; i++) {
	    int prn = nmeaid_to_prn(field[0], atoi(field[i + 3]));
	    if (prn > 0)
		session->nmea.sats_used[session->gpsdata.satellites_used++] =
		    (unsigned short)prn;
	}
	mask |= DOP_SET | USED_IS;
	gpsd_log(&session->context->errout, LOG_DATA,
		 "GPGSA: mode=%d used=%d pdop=%.2f hdop=%.2f vdop=%.2f\n",
		 session->newdata.mode,
		 session->gpsdata.satellites_used,
		 session->gpsdata.dop.pdop,
		 session->gpsdata.dop.hdop,
		 session->gpsdata.dop.vdop);
    }
    return mask;
}

static gps_mask_t processGSV(int count, char *field[],
			       struct gps_device_t *session)
/* GPS Satellites in View */
{
#define GSV_TALKER	field[0][1]
    /*
     * GSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75
     * 2           Number of sentences for full data
     * 1           Sentence 1 of 2
     * 08          Total number of satellites in view
     * 01          Satellite PRN number
     * 40          Elevation, degrees
     * 083         Azimuth, degrees
     * 46          Signal-to-noise ratio in decibels
     * <repeat for up to 4 satellites per sentence>
     * There my be up to three GSV sentences in a data packet
     *
     * Can occur with talker IDs:
     *   BD (Beidou),
     *   GA (Galileo),
     *   GB (Beidou),
     *   GL (GLONASS),
     *   GN (GLONASS, any combination GNSS),
     *   GP (GPS, SBAS, QZSS),
     *   QZ (QZSS).
     *
     * GL may be (incorrectly) used when GSVs are mixed containing
     * GLONASS, GN may be (incorrectly) used when GSVs contain GLONASS
     * only.  Usage is inconsistent.
     *
     * In the GLONASS version sat IDs run from 65-96 (NMEA0183 standardizes
     * this). At least two GPS, the BU-353 GLONASS and the u-blox NEO-M8N,
     * emit a GPGSV set followed by a GLGSV set.  We have also seen a
     * SiRF-IV variant that emits GPGSV followed by BDGSV. We need to
     * combine these.
     *
     * NMEA 4.1 adds a signal-ID field just before the checksum. First
     * seen in May 2015 on a u-blox M8,
     */

    int n, fldnum;
    if (count <= 3) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "malformed GPGSV - fieldcount %d <= 3\n",
		 count);
	gpsd_zero_satellites(&session->gpsdata);
	session->gpsdata.satellites_visible = 0;
	return ONLINE_SET;
    }
    /*
     * This check used to be !=0, but we have loosen it a little to let by
     * NMEA 4.1 GSVs with an extra signal-ID field at the end.  
     */
    if (count % 4 > 1) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "malformed GPGSV - fieldcount %d %% 4 != 0\n",
		 count);
	gpsd_zero_satellites(&session->gpsdata);
	session->gpsdata.satellites_visible = 0;
	return ONLINE_SET;
    }

    session->nmea.await = atoi(field[1]);
    if ((session->nmea.part = atoi(field[2])) < 1) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "malformed GPGSV - bad part\n");
	gpsd_zero_satellites(&session->gpsdata);
	return ONLINE_SET;
    } else if (session->nmea.part == 1) {
	/*
	 * might have gone from GPGSV to GLGSV/BDGSV/QZGSV,
	 * in which case accumulate
	 */
	if (session->nmea.last_gsv_talker == '\0' || GSV_TALKER == session->nmea.last_gsv_talker) {
	    gpsd_zero_satellites(&session->gpsdata);
	}
	session->nmea.last_gsv_talker = GSV_TALKER;
	if (session->nmea.last_gsv_talker == 'L')
	    session->nmea.seen_glgsv = true;
	if (session->nmea.last_gsv_talker == 'D')
	    session->nmea.seen_bdgsv = true;
	if (session->nmea.last_gsv_talker == 'Z')
	    session->nmea.seen_qzss = true;
    }

    for (fldnum = 4; fldnum < count;) {
	struct satellite_t *sp;
	if (session->gpsdata.satellites_visible >= MAXCHANNELS) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "internal error - too many satellites [%d]!\n",
		     session->gpsdata.satellites_visible);
	    gpsd_zero_satellites(&session->gpsdata);
	    break;
	}
	sp = &session->gpsdata.skyview[session->gpsdata.satellites_visible];
	sp->PRN = (short)nmeaid_to_prn(field[0], atoi(field[fldnum++]));
	sp->elevation = (short)atoi(field[fldnum++]);
	sp->azimuth = (short)atoi(field[fldnum++]);
	sp->ss = (float)atoi(field[fldnum++]);
	sp->used = false;
	if (sp->PRN > 0)
	    for (n = 0; n < MAXCHANNELS; n++)
		if (session->nmea.sats_used[n] == (unsigned short)sp->PRN) {
		    sp->used = true;
		    break;
		}
	/*
	 * Incrementing this unconditionally falls afoul of chipsets like
	 * the Motorola Oncore GT+ that emit empty fields at the end of the
	 * last sentence in a GPGSV set if the number of satellites is not
	 * a multiple of 4.
	 */
	if (sp->PRN != 0)
	    session->gpsdata.satellites_visible++;
    }

    /*
     * Alas, we can't sanity check field counts when there are multiple sat 
     * pictures, because the visible member counts *all* satellites - you 
     * get a bad result on the second and later SV spans.  Note, this code
     * assumes that if any of the special sat pics occur they come right
     * after a stock GPGSV one.
     */
    if (session->nmea.seen_glgsv || session->nmea.seen_bdgsv || session->nmea.seen_qzss)
	if (session->nmea.part == session->nmea.await
		&& atoi(field[3]) != session->gpsdata.satellites_visible)
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "GPGSV field 3 value of %d != actual count %d\n",
		     atoi(field[3]), session->gpsdata.satellites_visible);

    /* not valid data until we've seen a complete set of parts */
    if (session->nmea.part < session->nmea.await) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "Partial satellite data (%d of %d).\n",
		 session->nmea.part, session->nmea.await);
	return ONLINE_SET;
    }
    /*
     * This sanity check catches an odd behavior of SiRFstarII receivers.
     * When they can't see any satellites at all (like, inside a
     * building) they sometimes cough up a hairball in the form of a
     * GSV packet with all the azimuth entries 0 (but nonzero
     * elevations).  This behavior was observed under SiRF firmware
     * revision 231.000.000_A2.
     */
    for (n = 0; n < session->gpsdata.satellites_visible; n++)
	if (session->gpsdata.skyview[n].azimuth != 0)
	    goto sane;
    gpsd_log(&session->context->errout, LOG_WARN,
	     "Satellite data no good (%d of %d).\n",
	     session->nmea.part, session->nmea.await);
    gpsd_zero_satellites(&session->gpsdata);
    return ONLINE_SET;
  sane:
    session->gpsdata.skyview_time = NAN;
    gpsd_log(&session->context->errout, LOG_DATA,
	     "GSV: Satellite data OK (%d of %d).\n",
	     session->nmea.part, session->nmea.await);

    /* assumes GLGSV or BDGSV group, if present, is emitted after the GPGSV */
    if ((session->nmea.seen_glgsv || session->nmea.seen_bdgsv || session->nmea.seen_qzss) && GSV_TALKER == 'P')
	return ONLINE_SET;
    return SATELLITE_SET;
#undef GSV_TALKER
}

static gps_mask_t processPGRME(int c UNUSED, char *field[],
			       struct gps_device_t *session)
/* Garmin Estimated Position Error */
{
    /*
     * $PGRME,15.0,M,45.0,M,25.0,M*22
     * 1    = horizontal error estimate
     * 2    = units
     * 3    = vertical error estimate
     * 4    = units
     * 5    = spherical error estimate
     * 6    = units
     * *
     * * Garmin won't say, but the general belief is that these are 50% CEP.
     * * We follow the advice at <http://gpsinformation.net/main/errors.htm>.
     * * If this assumption changes here, it should also change in garmin.c
     * * where we scale error estimates from Garmin binary packets, and
     * * in libgpsd_core.c where we generate $PGRME.
     */
    gps_mask_t mask;
    if ((strcmp(field[2], "M") != 0) ||
	(strcmp(field[4], "M") != 0) || (strcmp(field[6], "M") != 0)) {
	session->newdata.epx =
	    session->newdata.epy =
	    session->newdata.epv = session->gpsdata.epe = 100;
	mask = 0;
    } else {
	session->newdata.epx = session->newdata.epy =
	    safe_atof(field[1]) * (1 / sqrt(2)) * (GPSD_CONFIDENCE / CEP50_SIGMA);
	session->newdata.epv =
	    safe_atof(field[3]) * (GPSD_CONFIDENCE / CEP50_SIGMA);
	session->gpsdata.epe =
	    safe_atof(field[5]) * (GPSD_CONFIDENCE / CEP50_SIGMA);
	mask = HERR_SET | VERR_SET | PERR_IS;
    }

    gpsd_log(&session->context->errout, LOG_DATA,
	     "PGRME: epx=%.2f epy=%.2f epv=%.2f\n",
	     session->newdata.epx,
	     session->newdata.epy,
	     session->newdata.epv);
    return mask;
}

static gps_mask_t processGBS(int c UNUSED, char *field[],
			       struct gps_device_t *session)
/* NMEA 3.0 Estimated Position Error */
{
    /*
     * $GPGBS,082941.00,2.4,1.5,3.9,25,,-43.7,27.5*65
     * 1) UTC time of the fix associated with this sentence (hhmmss.ss)
     * 2) Expected error in latitude (meters)
     * 3) Expected error in longitude (meters)
     * 4) Expected error in altitude (meters)
     * 5) PRN of most likely failed satellite
     * 6) Probability of missed detection for most likely failed satellite
     * 7) Estimate of bias in meters on most likely failed satellite
     * 8) Standard deviation of bias estimate
     * 9) Checksum
     */

    /* register fractional time for end-of-cycle detection */
    register_fractional_time(field[0], field[1], session);

    /* check that we're associated with the current fix */
    if (session->nmea.date.tm_hour == DD(field[1])
	&& session->nmea.date.tm_min == DD(field[1] + 2)
	&& session->nmea.date.tm_sec == DD(field[1] + 4)) {
	session->newdata.epy = safe_atof(field[2]);
	session->newdata.epx = safe_atof(field[3]);
	session->newdata.epv = safe_atof(field[4]);
	gpsd_log(&session->context->errout, LOG_DATA,
		 "GBS: epx=%.2f epy=%.2f epv=%.2f\n",
		 session->newdata.epx,
		 session->newdata.epy,
		 session->newdata.epv);
	return HERR_SET | VERR_SET;
    } else {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "second in $GPGBS error estimates doesn't match.\n");
	return 0;
    }
}

static gps_mask_t processZDA(int c UNUSED, char *field[],
			       struct gps_device_t *session)
/* Time & Date */
{
    /*
     * $GPZDA,160012.71,11,03,2004,-1,00*7D
     * 1) UTC time (hours, minutes, seconds, may have fractional subsecond)
     * 2) Day, 01 to 31
     * 3) Month, 01 to 12
     * 4) Year (4 digits)
     * 5) Local zone description, 00 to +- 13 hours
     * 6) Local zone minutes description, apply same sign as local hours
     * 7) Checksum
     *
     * Note: some devices, like the u-blox ANTARIS 4h, are known to ship ZDAs
     * with some fields blank under poorly-understood circumstances (probably
     * when they don't have satellite lock yet).
     */
    gps_mask_t mask = 0;

    if (field[1][0] == '\0' || field[2][0] == '\0' || field[3][0] == '\0'
	|| field[4][0] == '\0') {
	gpsd_log(&session->context->errout, LOG_WARN, "ZDA fields are empty\n");
    } else {
    	int year, mon, mday, century;

	merge_hhmmss(field[1], session);
	/*
	 * We don't register fractional time here because want to leave
	 * ZDA out of end-of-cycle detection. Some devices sensibly emit it only
	 * when they have a fix, so watching for it can make them look
	 * like they have a variable fix reporting cycle.
	 */
	year = atoi(field[4]);
	mon = atoi(field[3]);
	mday = atoi(field[2]);
	century = year - year % 100;
	if ( (1900 > year ) || (2200 < year ) ) {
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "malformed ZDA year: %s\n",  field[4]);
	} else if ( (1 > mon ) || (12 < mon ) ) {
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "malformed ZDA month: %s\n",  field[3]);
	} else if ( (1 > mday ) || (31 < mday ) ) {
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "malformed ZDA day: %s\n",  field[2]);
	} else {
	    gpsd_century_update(session, century);
	    session->nmea.date.tm_year = year - 1900;
	    session->nmea.date.tm_mon = mon - 1;
	    session->nmea.date.tm_mday = mday;
	    mask = TIME_SET;
	}
    };
    return mask;
}

static gps_mask_t processHDT(int c UNUSED, char *field[],
				struct gps_device_t *session)
{
    /*
     * $HEHDT,341.8,T*21
     *
     * HDT,x.x*hh<cr><lf>
     *
     * The only data field is true heading in degrees.
     * The following field is required to be 'T' indicating a true heading.
     * It is followed by a mandatory nmea_checksum.
     */
    gps_mask_t mask;
    mask = ONLINE_SET;

    session->gpsdata.attitude.heading = safe_atof(field[1]);
    session->gpsdata.attitude.mag_st = '\0';
    session->gpsdata.attitude.pitch = NAN;
    session->gpsdata.attitude.pitch_st = '\0';
    session->gpsdata.attitude.roll = NAN;
    session->gpsdata.attitude.roll_st = '\0';
    session->gpsdata.attitude.yaw = NAN;
    session->gpsdata.attitude.yaw_st = '\0';
    session->gpsdata.attitude.dip = NAN;
    session->gpsdata.attitude.mag_len = NAN;
    session->gpsdata.attitude.mag_x = NAN;
    session->gpsdata.attitude.mag_y = NAN;
    session->gpsdata.attitude.mag_z = NAN;
    session->gpsdata.attitude.acc_len = NAN;
    session->gpsdata.attitude.acc_x = NAN;
    session->gpsdata.attitude.acc_y = NAN;
    session->gpsdata.attitude.acc_z = NAN;
    session->gpsdata.attitude.gyro_x = NAN;
    session->gpsdata.attitude.gyro_y = NAN;
    session->gpsdata.attitude.temp = NAN;
    session->gpsdata.attitude.depth = NAN;
    mask |= (ATTITUDE_SET);

    gpsd_log(&session->context->errout, LOG_RAW,
	     "time %.3f, heading %lf.\n",
	     session->newdata.time,
	     session->gpsdata.attitude.heading);
    return mask;
}

static gps_mask_t processDBT(int c UNUSED, char *field[],
				struct gps_device_t *session)
{
    /*
     * $SDDBT,7.7,f,2.3,M,1.3,F*05
     * 1) Depth below sounder in feet
     * 2) Fixed value 'f' indicating feet
     * 3) Depth below sounder in meters
     * 4) Fixed value 'M' indicating meters
     * 5) Depth below sounder in fathoms
     * 6) Fixed value 'F' indicating fathoms
     * 7) Checksum.
     *
     * In real-world sensors, sometimes not all three conversions are reported.
     */
    gps_mask_t mask;
    mask = ONLINE_SET;

    if (field[3][0] != '\0') {
	session->newdata.altitude = -safe_atof(field[3]);
	mask |= (ALTITUDE_SET);
    } else if (field[1][0] != '\0') {
	session->newdata.altitude = -safe_atof(field[1]) / METERS_TO_FEET;
	mask |= (ALTITUDE_SET);
    } else if (field[5][0] != '\0') {
	session->newdata.altitude = -safe_atof(field[5]) / METERS_TO_FATHOMS;
	mask |= (ALTITUDE_SET);
    }

    if ((mask & ALTITUDE_SET) != 0) {
	if (session->newdata.mode < MODE_3D) {
	    session->newdata.mode = MODE_3D;
	    mask |= MODE_SET;
	}
    }

    /*
     * Hack: We report depth below keep as negative altitude because there's
     * no better place to put it.  Should work in practice as nobody is
     * likely to be operating a depth sounder at varying altitudes.
     */
    gpsd_log(&session->context->errout, LOG_RAW,
	     "mode %d, depth %lf.\n",
	     session->newdata.mode,
	     session->newdata.altitude);
    return mask;
}

static gps_mask_t processTXT(int count, char *field[],
			       struct gps_device_t *session)
/* GPS Text message */
{
    /*
     * $GNTXT,01,01,01,PGRM inv format*2A
     * 1                   Number of sentences for full data
     * 1                   Sentence 1 of 1
     * 01                  Message type
     *       00 - error
     *       01 - warning
     *       02 - notice
     *       07 - user
     * PGRM inv format     ASCII text
     *
     * Can occur with talker IDs:
     *   BD (Beidou),
     *   GA (Galileo),
     *   GB (Beidou),
     *   GL (GLONASS),
     *   GN (GLONASS, any combination GNSS),
     *   GP (GPS, SBAS, QZSS),
     *   QZ (QZSS).
     */
    gps_mask_t mask = 0;
    int msgType = 0;
    char *msgType_txt = "Unknown";

    if ( 5 != count) {
      return 0;
    }

    /* set something, so it won't look like an unknown sentence */
    mask |= ONLINE_SET;

    msgType = atoi(field[3]);

    switch ( msgType ) {
    case 0:
	msgType_txt = "Error";
        break;
    case 1:
	msgType_txt = "Warning";
        break;
    case 2:
	msgType_txt = "Notice";
        break;
    case 7:
	msgType_txt = "User";
        break;
    }

    /* maximum text lenght unknown, guess 80 */
    gpsd_log(&session->context->errout, LOG_WARN,
	     "TXT: %.10s: %.80s\n",
             msgType_txt, field[4]);
    return mask;
}

#ifdef TNT_ENABLE
static gps_mask_t processTNTHTM(int c UNUSED, char *field[],
				struct gps_device_t *session)
{
    /*
     * Proprietary sentence for True North Technologies Magnetic Compass.
     * This may also apply to some Honeywell units since they may have been
     * designed by True North.

     $PTNTHTM,14223,N,169,N,-43,N,13641,2454*15

     HTM,x.x,a,x.x,a,x.x,a,x.x,x.x*hh<cr><lf>
     Fields in order:
     1. True heading (compass measurement + deviation + variation)
     2. magnetometer status character:
     C = magnetometer calibration alarm
     L = low alarm
     M = low warning
     N = normal
     O = high warning
     P = high alarm
     V = magnetometer voltage level alarm
     3. pitch angle
     4. pitch status character - see field 2 above
     5. roll angle
     6. roll status character - see field 2 above
     7. dip angle
     8. relative magnitude horizontal component of earth's magnetic field
     *hh          mandatory nmea_checksum

     By default, angles are reported as 26-bit integers: weirdly, the
     technical manual says either 0 to 65535 or -32768 to 32767 can
     occur as a range.
     */
    gps_mask_t mask;
    mask = ONLINE_SET;

    session->gpsdata.attitude.heading = safe_atof(field[1]);
    session->gpsdata.attitude.mag_st = *field[2];
    session->gpsdata.attitude.pitch = safe_atof(field[3]);
    session->gpsdata.attitude.pitch_st = *field[4];
    session->gpsdata.attitude.roll = safe_atof(field[5]);
    session->gpsdata.attitude.roll_st = *field[6];
    session->gpsdata.attitude.yaw = NAN;
    session->gpsdata.attitude.yaw_st = '\0';
    session->gpsdata.attitude.dip = safe_atof(field[7]);
    session->gpsdata.attitude.mag_len = NAN;
    session->gpsdata.attitude.mag_x = safe_atof(field[8]);
    session->gpsdata.attitude.mag_y = NAN;
    session->gpsdata.attitude.mag_z = NAN;
    session->gpsdata.attitude.acc_len = NAN;
    session->gpsdata.attitude.acc_x = NAN;
    session->gpsdata.attitude.acc_y = NAN;
    session->gpsdata.attitude.acc_z = NAN;
    session->gpsdata.attitude.gyro_x = NAN;
    session->gpsdata.attitude.gyro_y = NAN;
    session->gpsdata.attitude.temp = NAN;
    session->gpsdata.attitude.depth = NAN;
    mask |= (ATTITUDE_SET);

    gpsd_log(&session->context->errout, LOG_RAW,
	     "time %.3f, heading %lf (%c).\n",
	     session->newdata.time,
	     session->gpsdata.attitude.heading,
	     session->gpsdata.attitude.mag_st);
    return mask;
}

static gps_mask_t processTNTA(int c UNUSED, char *field[],
			      struct gps_device_t *session)
{
    /*
     * Proprietary sentence for iSync GRClok/LNRClok.

     $PTNTA,20000102173852,1,T4,,,6,1,0*32

     1. Date/time in format year, month, day, hour, minute, second
     2. Oscillator quality 0:warming up, 1:freerun, 2:disciplined.
     3. Always T4. Format indicator.
     4. Interval ppsref-ppsout in [ns]. Blank if no ppsref.
     5. Fine phase comparator in approx. [ns]. Always close to -500 or
        +500 if not disciplined. Blank if no ppsref.
     6. iSync Status.  0:warming up or no light, 1:tracking set-up,
        2:track to PPSREF, 3:synch to PPSREF, 4:Free Run. Track OFF,
        5:FR. PPSREF unstable, 6:FR. No PPSREF, 7:FREEZE, 8:factory
        used, 9:searching Rb line
     7. GPS messages indicator. 0:do not take account, 1:take account,
        but no message, 2:take account, partially ok, 3:take account,
        totally ok.
     8. Transfer quality of date/time. 0:no, 1:manual, 2:GPS, older
        than x hours, 3:GPS, fresh.

     */
    gps_mask_t mask;
    mask = ONLINE_SET;

    if (strcmp(field[3], "T4") == 0) {
	struct oscillator_t *osc = &session->gpsdata.osc;
	unsigned int quality = atoi(field[2]);
	unsigned int delta = atoi(field[4]);
	unsigned int fine = atoi(field[5]);
	unsigned int status = atoi(field[6]);
	char deltachar = field[4][0];

	osc->running = (quality > 0);
	osc->reference = (deltachar && (deltachar != '?'));
	if (osc->reference) {
	    if (abs(delta) < 500) {
		osc->delta = fine;
	    } else {
		osc->delta = ((delta < 500000000) ? delta : 1000000000 - delta);
	    }
	} else {
	    osc->delta = 0;
	}
	osc->disciplined = ((quality == 2) && (status == 3));
	mask |= OSCILLATOR_SET;

	gpsd_log(&session->context->errout, LOG_DATA,
		 "PTNTA,T4: quality=%s, delta=%s, fine=%s, status=%s\n",
		 field[2], field[4], field[5], field[6]);
    }
    return mask;
}
#endif /* TNT_ENABLE */

#ifdef OCEANSERVER_ENABLE
static gps_mask_t processOHPR(int c UNUSED, char *field[],
			      struct gps_device_t *session)
{
    /*
     * Proprietary sentence for OceanServer Magnetic Compass.

     OHPR,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x,x.x*hh<cr><lf>
     Fields in order:
     1. Azimuth
     2. Pitch Angle
     3. Roll Angle
     4. Sensor temp, degrees centigrade
     5. Depth (feet)
     6. Magnetic Vector Length
     7-9. 3 axis Magnetic Field readings x,y,z
     10. Acceleration Vector Length
     11-13. 3 axis Acceleration Readings x,y,z
     14. Reserved
     15-16. 2 axis Gyro Output, X,y
     17. Reserved
     18. Reserved
     *hh          mandatory nmea_checksum
     */
    gps_mask_t mask;
    mask = ONLINE_SET;

    session->gpsdata.attitude.heading = safe_atof(field[1]);
    session->gpsdata.attitude.mag_st = '\0';
    session->gpsdata.attitude.pitch = safe_atof(field[2]);
    session->gpsdata.attitude.pitch_st = '\0';
    session->gpsdata.attitude.roll = safe_atof(field[3]);
    session->gpsdata.attitude.roll_st = '\0';
    session->gpsdata.attitude.yaw = NAN;
    session->gpsdata.attitude.yaw_st = '\0';
    session->gpsdata.attitude.dip = NAN;
    session->gpsdata.attitude.temp = safe_atof(field[4]);
    session->gpsdata.attitude.depth = safe_atof(field[5]) / METERS_TO_FEET;
    session->gpsdata.attitude.mag_len = safe_atof(field[6]);
    session->gpsdata.attitude.mag_x = safe_atof(field[7]);
    session->gpsdata.attitude.mag_y = safe_atof(field[8]);
    session->gpsdata.attitude.mag_z = safe_atof(field[9]);
    session->gpsdata.attitude.acc_len = safe_atof(field[10]);
    session->gpsdata.attitude.acc_x = safe_atof(field[11]);
    session->gpsdata.attitude.acc_y = safe_atof(field[12]);
    session->gpsdata.attitude.acc_z = safe_atof(field[13]);
    session->gpsdata.attitude.gyro_x = safe_atof(field[15]);
    session->gpsdata.attitude.gyro_y = safe_atof(field[16]);
    mask |= (ATTITUDE_SET);

    gpsd_log(&session->context->errout, LOG_RAW,
	     "Heading %lf.\n", session->gpsdata.attitude.heading);
    return mask;
}
#endif /* OCEANSERVER_ENABLE */

#ifdef ASHTECH_ENABLE
/* Ashtech sentences take this format:
 * $PASHDR,type[,val[,val]]*CS
 * type is an alphabetic subsentence type
 *
 * Oxford Technical Solutions (OXTS) also uses the $PASHR sentence,
 * but with a very different sentence contents:
 * $PASHR,HHMMSS.SSS,HHH.HH,T,RRR.RR,PPP.PP,aaa.aa,r.rrr,p.ppp,h.hhh,Q1,Q2*CS
 *
 * so field 1 in ASHTECH is always alphabetic and numeric in OXTS
 * FIXME: decode OXTS $PASHDR
 *
 */
static gps_mask_t processPASHR(int c UNUSED, char *field[],
			       struct gps_device_t *session)
{
    gps_mask_t mask;
    mask = 0;

    if (0 == strcmp("RID", field[1])) {	/* Receiver ID */
	(void)snprintf(session->subtype, sizeof(session->subtype) - 1,
		       "%s ver %s", field[2], field[3]);
	gpsd_log(&session->context->errout, LOG_DATA,
		 "PASHR,RID: subtype=%s mask={}\n",
		 session->subtype);
	return mask;
    } else if (0 == strcmp("POS", field[1])) {	/* 3D Position */
	mask |= MODE_SET | STATUS_SET | CLEAR_IS;
	if (0 == strlen(field[2])) {
	    /* empty first field means no 3D fix is available */
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->newdata.mode = MODE_NO_FIX;
	} else {
	    /* if we make it this far, we at least have a 3D fix */
	    session->newdata.mode = MODE_3D;
	    if (1 == atoi(field[2]))
		session->gpsdata.status = STATUS_DGPS_FIX;
	    else
		session->gpsdata.status = STATUS_FIX;

	    session->gpsdata.satellites_used = atoi(field[3]);
	    merge_hhmmss(field[4], session);
	    register_fractional_time(field[0], field[4], session);
	    do_lat_lon(&field[5], &session->newdata);
	    session->newdata.altitude = safe_atof(field[9]);
	    session->newdata.track = safe_atof(field[11]);
	    session->newdata.speed = safe_atof(field[12]) / MPS_TO_KPH;
	    session->newdata.climb = safe_atof(field[13]);
	    session->gpsdata.dop.pdop = safe_atof(field[14]);
	    session->gpsdata.dop.hdop = safe_atof(field[15]);
	    session->gpsdata.dop.vdop = safe_atof(field[16]);
	    session->gpsdata.dop.tdop = safe_atof(field[17]);
	    mask |= (TIME_SET | LATLON_SET | ALTITUDE_SET);
	    mask |= (SPEED_SET | TRACK_SET | CLIMB_SET);
	    mask |= DOP_SET;
	    gpsd_log(&session->context->errout, LOG_DATA,
		     "PASHR,POS: hhmmss=%s lat=%.2f lon=%.2f alt=%.f speed=%.2f track=%.2f climb=%.2f mode=%d status=%d pdop=%.2f hdop=%.2f vdop=%.2f tdop=%.2f\n",
		     field[4], session->newdata.latitude,
		     session->newdata.longitude, session->newdata.altitude,
		     session->newdata.speed, session->newdata.track,
		     session->newdata.climb, session->newdata.mode,
		     session->gpsdata.status, session->gpsdata.dop.pdop,
		     session->gpsdata.dop.hdop, session->gpsdata.dop.vdop,
		     session->gpsdata.dop.tdop);
	}
    } else if (0 == strcmp("SAT", field[1])) {	/* Satellite Status */
	struct satellite_t *sp;
	int i, n = session->gpsdata.satellites_visible = atoi(field[2]);
	session->gpsdata.satellites_used = 0;
	for (i = 0, sp = session->gpsdata.skyview; sp < session->gpsdata.skyview + n; sp++, i++) {
	    sp->PRN = (short)atoi(field[3 + i * 5 + 0]);
	    sp->azimuth = (short)atoi(field[3 + i * 5 + 1]);
	    sp->elevation = (short)atoi(field[3 + i * 5 + 2]);
	    sp->ss = safe_atof(field[3 + i * 5 + 3]);
	    sp->used = false;
	    if (field[3 + i * 5 + 4][0] == 'U') {
		sp->used = true;
		session->gpsdata.satellites_used++;
	    }
	}
	gpsd_log(&session->context->errout, LOG_DATA,
		 "PASHR,SAT: used=%d\n",
		 session->gpsdata.satellites_used);
	session->gpsdata.skyview_time = NAN;
	mask |= SATELLITE_SET | USED_IS;
    }
    return mask;
}
#endif /* ASHTECH_ENABLE */

#ifdef MTK3301_ENABLE
static gps_mask_t processMTK3301(int c UNUSED, char *field[],
			       struct gps_device_t *session)
{
    int msg, reason;

    msg = atoi(&(session->nmea.field[0])[4]);
    switch (msg) {
    case 001:			/* ACK / NACK */
	reason = atoi(field[2]);
	if (atoi(field[1]) == -1)
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "MTK NACK: unknown sentence\n");
	else if (reason < 3) {
	    const char *mtk_reasons[] = {
		"Invalid",
		"Unsupported",
		"Valid but Failed",
		"Valid success"
	    };
	    gpsd_log(&session->context->errout, LOG_WARN,
		     "MTK NACK: %s, reason: %s\n",
		     field[1], mtk_reasons[reason]);
	}
	else
	    gpsd_log(&session->context->errout, LOG_DATA,
		     "MTK ACK: %s\n", field[1]);
	return ONLINE_SET;
    case 424:			/* PPS pulse width response */
	/*
	 * Response will look something like: $PMTK424,0,0,1,0,69*12
	 * The pulse width is in field 5 (69 in this example).  This
	 * sentence is poorly documented at:
	 * http://www.trimble.com/embeddedsystems/condor-gps-module.aspx?dtID=documentation
	 *
	 * Packet Type: 324 PMTK_API_SET_OUTPUT_CTL
	 * Packet meaning
	 * Write the TSIP / antenna / PPS configuration data to the Flash memory.
	 * DataField [Data0]:TSIP Packet[on/off]
         * 0 - Disable TSIP output (Default).
         * 1 - Enable TSIP output.
         * [Data1]:Antenna Detect[on/off]
         * 0 - Disable antenna detect function (Default).
         * 1 - Enable antenna detect function.
         * [Data2]:PPS on/off
         * 0 - Disable PPS function.
         * 1 - Enable PPS function (Default).
         * [Data3]:PPS output timing
         * 0 - Always output PPS (Default).
         * 1 - Only output PPS when GPS position is fixed.
         * [Data4]:PPS pulse width
         * 1~16367999: 61 ns~(61x 16367999) ns (Default = 69)
	 *
	 * The documentation does not give the units of the data field.
	 * Andy Walls <andy@silverblocksystems.net> says:
	 *
	 * "The best I can figure using an oscilloscope, is that it is
	 * in units of 16.368000 MHz clock cycles.  It may be
	 * different for any other unit other than the Trimble
	 * Condor. 69 cycles / 16368000 cycles/sec = 4.216 microseconds
	 * [which is the pulse width I have observed]"
	 *
	 * Support for this theory comes from the fact that crystal
	 * TXCOs with a 16.368MHZ period are commonly available from
	 * multiple vendors. Furthermore, 61*69 = 4209, which is
	 * close to the observed cycle time and suggests that the
	 * documentation is trying to indicate 61ns units.
	 *
	 * He continues:
	 *
	 * "I chose [127875] because to divides 16368000 nicely and the
	 * pulse width is close to 1/100th of a second.  Any number
	 * the user wants to use would be fine.  127875 cycles /
	 * 16368000 cycles/second = 1/128 seconds = 7.8125
	 * milliseconds"
	 */

	/* too short?  Make it longer */
	if (atoi(field[5]) < 127875)
	    (void)nmea_send(session, "$PMTK324,0,0,1,0,127875");
	return ONLINE_SET;
    case 705:			/* return device subtype */
	(void)strlcat(session->subtype, field[1], sizeof(session->subtype));
	(void)strlcat(session->subtype, "-", sizeof(session->subtype));
	(void)strlcat(session->subtype, field[2], sizeof(session->subtype));
	return ONLINE_SET;
    default:
	gpsd_log(&session->context->errout, LOG_PROG,
	     "MTK: unknown msg: %d\n", msg);
	return ONLINE_SET;		/* ignore */
    }
}
#endif /* MTK3301_ENABLE */

/**************************************************************************
 *
 * Entry points begin here
 *
 **************************************************************************/

gps_mask_t nmea_parse(char *sentence, struct gps_device_t * session)
/* parse an NMEA sentence, unpack it into a session structure */
{
    typedef gps_mask_t(*nmea_decoder) (int count, char *f[],
				       struct gps_device_t * session);
    static struct
    {
	char *name;
	int nf;			/* minimum number of fields required to parse */
	bool cycle_continue;	/* cycle continuer? */
	nmea_decoder decoder;
    } nmea_phrase[] = {
	{"PGRMC", 0, false, NULL},	/* ignore Garmin Sensor Config */
	{"PGRME", 7, false, processPGRME},
	{"PGRMI", 0, false, NULL},	/* ignore Garmin Sensor Init */
	{"PGRMO", 0, false, NULL},	/* ignore Garmin Sentence Enable */
	    /*
	     * Basic sentences must come after the PG* ones, otherwise
	     * Garmins can get stuck in a loop that looks like this:
	     *
	     * 1. A Garmin GPS in NMEA mode is detected.
	     *
	     * 2. PGRMC is sent to reconfigure to Garmin binary mode.
	     *    If successful, the GPS echoes the phrase.
	     *
	     * 3. nmea_parse() sees the echo as RMC because the talker
	     *    ID is ignored, and fails to recognize the echo as
	     *    PGRMC and ignore it.
	     *
	     * 4. The mode is changed back to NMEA, resulting in an
	     *    infinite loop.
	     */
	{"DBT", 7,  true,  processDBT},
	{"GBS", 7,  false, processGBS},
	{"GGA", 13, false, processGGA},
	{"GLL", 7,  false, processGLL},
	{"GSA", 17, false, processGSA},
	{"GST", 8,  false, processGST},
	{"GSV", 0,  false, processGSV},
        {"HDT", 1,  false, processHDT},
#ifdef OCEANSERVER_ENABLE
	{"OHPR", 18, false, processOHPR},
#endif /* OCEANSERVER_ENABLE */
#ifdef ASHTECH_ENABLE
	{"PASHR", 3, false, processPASHR},	/* general handler for Ashtech */
#endif /* ASHTECH_ENABLE */
#ifdef MTK3301_ENABLE
	{"PMTK", 3,  false, processMTK3301},
        /* for some reason thhe parser no longer triggering on leading chars */
	{"PMTK001", 3,  false, processMTK3301},
	{"PMTK424", 3,  false, processMTK3301},
	{"PMTK705", 3,  false, processMTK3301},
#endif /* MTK3301_ENABLE */
#ifdef TNT_ENABLE
	{"PTNTHTM", 9, false, processTNTHTM},
	{"PTNTA", 8, false, processTNTA},
#endif /* TNT_ENABLE */
	{"RMC", 8,  false, processRMC},
	{"TXT", 5,  false, processTXT},
	{"ZDA", 4,  false, processZDA},
	{"VTG", 0,  false, NULL},	/* ignore Velocity Track made Good */
    };

    int count;
    gps_mask_t retval = 0;
    unsigned int i, thistag;
    char *p, *s, *e;
    volatile char *t;

    /*
     * We've had reports that on the Garmin GPS-10 the device sometimes
     * (1:1000 or so) sends garbage packets that have a valid checksum
     * but are like 2 successive NMEA packets merged together in one
     * with some fields lost.  Usually these are much longer than the
     * legal limit for NMEA, so we can cope by just tossing out overlong
     * packets.  This may be a generic bug of all Garmin chipsets.
     */
    if (strlen(sentence) > NMEA_MAX) {
	gpsd_log(&session->context->errout, LOG_WARN,
		 "Overlong packet of %zd chars rejected.\n",
		 strlen(sentence));
	return ONLINE_SET;
    }

    /* make an editable copy of the sentence */
    (void)strlcpy((char *)session->nmea.fieldcopy, sentence, sizeof(session->nmea.fieldcopy) - 1);
    /* discard the checksum part */
    for (p = (char *)session->nmea.fieldcopy;
	 (*p != '*') && (*p >= ' ');)
	++p;
    if (*p == '*')
	*p++ = ',';		/* otherwise we drop the last field */
    *p = '\0';
    e = p;

    /* split sentence copy on commas, filling the field array */
    count = 0;
    t = p;			/* end of sentence */
    p = (char *)session->nmea.fieldcopy + 1;	/* beginning of tag, 'G' not '$' */
    /* while there is a search string and we haven't run off the buffer... */
    while ((p != NULL) && (p <= t)) {
	session->nmea.field[count] = p;	/* we have a field. record it */
	if ((p = strchr(p, ',')) != NULL) {	/* search for the next delimiter */
	    *p = '\0';		/* replace it with a NUL */
	    count++;		/* bump the counters and continue */
	    p++;
	}
    }

    /* point remaining fields at empty string, just in case */
    for (i = (unsigned int)count;
	 i <
	 (unsigned)(sizeof(session->nmea.field) /
		    sizeof(session->nmea.field[0])); i++)
	session->nmea.field[i] = e;

    /* sentences handlers will tell us when they have fractional time */
    session->nmea.latch_frac_time = false;

    /* dispatch on field zero, the sentence tag */
    for (thistag = i = 0;
	 i < (unsigned)(sizeof(nmea_phrase) / sizeof(nmea_phrase[0])); ++i) {
	s = session->nmea.field[0];
	if (strlen(nmea_phrase[i].name) == 3)
	    s += 2;		/* skip talker ID */
	if (strcmp(nmea_phrase[i].name, s) == 0) {
	    if (nmea_phrase[i].decoder != NULL
		&& (count >= nmea_phrase[i].nf)) {
		retval =
		    (nmea_phrase[i].decoder) (count,
					      session->nmea.field,
					      session);
		if (nmea_phrase[i].cycle_continue)
		    session->nmea.cycle_continue = true;
		/*
		 * Must force this to be nz, as we're going to rely on a zero
		 * value to mean "no previous tag" later.
		 */
		thistag = i + 1;
	    } else
		retval = ONLINE_SET;	/* unknown sentence */
	    break;
	}
    }

    /* prevent overaccumulation of sat reports */
    if (!str_starts_with(session->nmea.field[0] + 2, "GSV"))
	session->nmea.last_gsv_talker = '\0';

    /* timestamp recording for fixes happens here */
    if ((retval & TIME_SET) != 0) {
	session->newdata.time = gpsd_utc_resolve(session);
	/*
	 * WARNING: This assumes time is always field 0, and that field 0
	 * is a timestamp whenever TIME_SET is set.
	 */
	gpsd_log(&session->context->errout, LOG_DATA,
		 "%s time is %2f = %d-%02d-%02dT%02d:%02d:%02.2fZ\n",
		 session->nmea.field[0], session->newdata.time,
		 1900 + session->nmea.date.tm_year,
		 session->nmea.date.tm_mon + 1,
		 session->nmea.date.tm_mday,
		 session->nmea.date.tm_hour,
		 session->nmea.date.tm_min,
		 session->nmea.date.tm_sec + session->nmea.subseconds);
	/*
	 * If we have time and PPS is available, assume we have good time.
	 * Because this is a generic driver we don't really have enough
	 * information for a sharper test, so we'll leave it up to the
	 * PPS code to do its own sanity filtering.
	 */
	retval |= PPSTIME_IS;
    }

    /*
     * The end-of-cycle detector.  This code depends on just one
     * assumption: if a sentence with a timestamp occurs just before
     * start of cycle, then it is always good to trigger a report on
     * that sentence in the future.  For devices with a fixed cycle
     * this should work perfectly, locking in detection after one
     * cycle.  Most split-cycle devices (Garmin 48, for example) will
     * work fine.  Problems will only arise if a a sentence that
     * occurs just befiore timestamp increments also occurs in
     * mid-cycle, as in the Garmin eXplorist 210; those might jitter.
     */
    if (session->nmea.latch_frac_time) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "%s sentence timestamped %.2f.\n",
		 session->nmea.field[0],
		 session->nmea.this_frac_time);
	if (!GPS_TIME_EQUAL
	    (session->nmea.this_frac_time,
	     session->nmea.last_frac_time)) {
	    uint lasttag = session->nmea.lasttag;
	    retval |= CLEAR_IS;
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "%s starts a reporting cycle.\n",
		     session->nmea.field[0]);
	    /*
	     * Have we seen a previously timestamped NMEA tag?
	     * If so, designate as end-of-cycle marker.
	     * But not if there are continuation sentences;
	     * those get sorted after the last timestamped sentence
	     */
	    if (lasttag > 0
		&& (session->nmea.cycle_enders & (1 << lasttag)) == 0
		&& !session->nmea.cycle_continue) {
		session->nmea.cycle_enders |= (1 << lasttag);
		gpsd_log(&session->context->errout, LOG_PROG,
			 "tagged %s as a cycle ender.\n",
			 nmea_phrase[lasttag - 1].name);
	    }
	}
    } else {
	/* extend the cycle to an un-timestamped sentence? */
	if ((session->nmea.lasttag & session->nmea.cycle_enders) != 0)
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "%s is just after a cycle ender.\n",
		     session->nmea.field[0]);
	if (session->nmea.cycle_continue) {
	    gpsd_log(&session->context->errout, LOG_PROG,
		     "%s extends the reporting cycle.\n",
		     session->nmea.field[0]);
	    session->nmea.cycle_enders &=~ (1 << session->nmea.lasttag);
	    session->nmea.cycle_enders |= (1 << thistag);
	}
    }
    /* here's where we check for end-of-cycle */
    if ((session->nmea.latch_frac_time || session->nmea.cycle_continue)
	&& (session->nmea.cycle_enders & (1 << thistag))!=0) {
	gpsd_log(&session->context->errout, LOG_PROG,
		 "%s ends a reporting cycle.\n",
		 session->nmea.field[0]);
	retval |= REPORT_IS;
    }
    if (session->nmea.latch_frac_time)
	session->nmea.lasttag = thistag;

    /* we might have a reliable end-of-cycle */
    if (session->nmea.cycle_enders != 0)
	session->cycle_end_reliable = true;

    return retval;
}

#endif /* NMEA0183_ENABLE */

void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$' || *p == '!') {
	p++;
    }
    while (((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    (void)snprintf(p, 5, "%02X\r\n", (unsigned)sum);
}

ssize_t nmea_write(struct gps_device_t *session, char *buf, size_t len UNUSED)
/* ship a command to the GPS, adding * and correct checksum */
{
    (void)strlcpy(session->msgbuf, buf, sizeof(session->msgbuf));
    if (session->msgbuf[0] == '$') {
	(void)strlcat(session->msgbuf, "*", sizeof(session->msgbuf));
	nmea_add_checksum(session->msgbuf);
    } else
	(void)strlcat(session->msgbuf, "\r\n", sizeof(session->msgbuf));
    session->msgbuflen = strlen(session->msgbuf);
    return gpsd_write(session, session->msgbuf, session->msgbuflen);
}

ssize_t nmea_send(struct gps_device_t * session, const char *fmt, ...)
{
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf) - 5, fmt, ap);
    va_end(ap);
    return nmea_write(session, buf, strlen(buf));
}
