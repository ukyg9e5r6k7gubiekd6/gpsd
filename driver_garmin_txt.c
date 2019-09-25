/*
 * Handle the Garmin simple text format supported by some Garmins.
 * Tested with the 'Garmin eTrex Legend' device working in 'Text Out' mode.
 *
 * Protocol info from:
 *	 http://www8.garmin.com/support/text_out.html
 *       http://www.garmin.com/support/commProtocol.html
 *
 * Code by: Petr Slansky <slansky@usa.net>
 * all rights abandoned, a thank would be nice if you use this code.
 *
 * -D 3 = packet trace
 * -D 4 = packet details
 * -D 5 = more packet details
 * -D 6 = very excessive details
 *
 * limitations:
 *  very simple protocol, only very basic information
 * TODO
 * do not have from garmin:
 *      pdop
 *      vdop
 *	magnetic variation
 *      satellite information
 *
 * This file is Copyright (c) 2010-2018 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 *
 */

/***************************************************
Garmin Simple Text Output Format:

The simple text (ASCII) output contains time, position, and velocity data in
the fixed width fields (not delimited) defined in the following table:

    FIELD DESCRIPTION:      WIDTH:  NOTES:
    ----------------------- ------- ------------------------
    Sentence start          1       Always '@'
    ----------------------- ------- ------------------------
   /Year                    2       Last two digits of UTC year
  | ----------------------- ------- ------------------------
  | Month                   2       UTC month, "01".."12"
T | ----------------------- ------- ------------------------
i | Day                     2       UTC day of month, "01".."31"
m | ----------------------- ------- ------------------------
e | Hour                    2       UTC hour, "00".."23"
  | ----------------------- ------- ------------------------
  | Minute                  2       UTC minute, "00".."59"
  | ----------------------- ------- ------------------------
   \Second                  2       UTC second, "00".."59"
    ----------------------- ------- ------------------------
   /Latitude hemisphere     1       'N' or 'S'
  | ----------------------- ------- ------------------------
  | Latitude position       7       WGS84 ddmmmmm, with an implied
  |                                 decimal after the 4th digit
  | ----------------------- ------- ------------------------
  | Longitude hemishpere    1       'E' or 'W'
  | ----------------------- ------- ------------------------
  | Longitude position      8       WGS84 dddmmmmm with an implied
P |                                 decimal after the 5th digit
o | ----------------------- ------- ------------------------
s | Position status         1       'd' if current 2D differential GPS position
i |                                 'D' if current 3D differential GPS position
t |                                 'g' if current 2D GPS position
i |                                 'G' if current 3D GPS position
o |                                 'S' if simulated position
n |                                 '_' if invalid position
  | ----------------------- ------- ------------------------
  | Horizontal posn error   3       EPH in meters
  | ----------------------- ------- ------------------------
  | Altitude sign           1       '+' or '-'
  | ----------------------- ------- ------------------------
  | Altitude                5       Height above or below mean
   \                                sea level in meters
    ----------------------- ------- ------------------------
   /East/West velocity      1       'E' or 'W'
  |     direction
  | ----------------------- ------- ------------------------
  | East/West velocity      4       Meters per second in tenths,
  |     magnitude                   ("1234" = 123.4 m/s)
V | ----------------------- ------- ------------------------
e | North/South velocity    1       'N' or 'S'
l |     direction
o | ----------------------- ------- ------------------------
c | North/South velocity    4       Meters per second in tenths,
i |     magnitude                   ("1234" = 123.4 m/s)
t | ----------------------- ------- ------------------------
y | Vertical velocity       1       'U' or 'D' (up/down)
  |     direction
  | ----------------------- ------- ------------------------
  | Vertical velocity       4       Meters per second in hundredths,
   \    magnitude                   ("1234" = 12.34 m/s)
    ----------------------- ------- ------------------------
    Sentence end            2       Carriage return, '0x0D', and
                                    line feed, '0x0A'
    ----------------------- ------- ------------------------

If a numeric value does not fill its entire field width, the field is padded
with leading '0's (eg. an altitude of 50 meters above MSL will be output as
"+00050").

Any or all of the data in the text sentence (except for the sentence start
and sentence end fields) may be replaced with underscores to indicate
invalid data.

***************************************************/


#include "gpsd_config.h"  /* must be before all includes */

#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <strings.h>

#include "gpsd.h"

#ifdef GARMINTXT_ENABLE

/* Simple text message is fixed length, 55 chars text data + 2 characters EOL */
/* buffer for text processing */
#define TXT_BUFFER_SIZE 13

/**************************************************************************
 * decode text string to double number, translate prefix to sign
 * return 0: OK
 *       -1: data error
 *       -2: data not valid
 *
 * examples with context->errout.debug == 0:

 *  gar_decode(context, cbuf, 9, "EW", 100000.0, &result);
 *  E01412345 -> +14.12345

 *  gar_decode(context, cbuf, 9, "EW", 100000.0, &result);
 *  W01412345 -> -14.12345

 *  gar_decode(context, cbuf, 3, "", 10.0, &result);
 *  123 -> +12.3

**************************************************************************/
static int gar_decode(const struct gps_context_t *context,
		      const char *data, const size_t length,
		      const char *prefix, const double dividor,
		      double *result)
{
    char buf[10];
    float sign = 1.0;
    int preflen = (int)strlen(prefix);
    int offset = 1;		/* assume one character prefix (E,W,S,N,U,D, etc) */
    long int intresult;

    if (length >= sizeof(buf)) {
	GPSD_LOG(LOG_ERROR, &context->errout, "internal buffer too small\n");
	return -1;
    }

    memset(buf, 0, (int)sizeof(buf));
    (void)strlcpy(buf, data, length);
    GPSD_LOG(LOG_RAW, &context->errout, "Decoded string: %s\n", buf);

    if (strchr(buf, '_') != NULL) {
	/* value is not valid, ignore it */
	return -2;
    }

    /* parse prefix */
    do {
	if (preflen == 0) {
	    offset = 0;		/* only number, no prefix */
	    break;
	}
	/* second character in prefix is flag for negative number */
	if (preflen >= 2) {
	    // cppcheck-suppress arrayIndexOutOfBounds
	    if (buf[0] == prefix[1]) {
		sign = -1.0;
		break;
	    }
	}
	/* first character in prefix is flag for positive number */
	if (preflen >= 1) {
	    if (buf[0] == prefix[0]) {
		sign = 1.0;
		break;
	    }
	}
	GPSD_LOG(LOG_WARN, &context->errout,
		 "Unexpected char \"%c\" in data \"%s\"\n",
		 buf[0], buf);
	return -1;
    } while (0);

    if (strspn(buf + offset, "0123456789") != length - offset) {
	GPSD_LOG(LOG_WARN, &context->errout, "Invalid value %s\n", buf);
	return -1;
    }

    intresult = atol(buf + offset);
    if (intresult == 0L)
	sign = 0.0;		/*  don't create negative zero */

    *result = (double)intresult / dividor * sign;

    return 0;			/* SUCCESS */
}

/**************************************************************************
 * decode integer from string, check if the result is in expected range
 * return 0: OK
 *       -1: data error
 *       -2: data not valid
**************************************************************************/
static int gar_int_decode(const struct gps_context_t *context,
			  const char *data, const size_t length,
			  const unsigned int min, const unsigned int max,
			  unsigned int *result)
{
    char buf[6];
    unsigned int res;

    if (length >= sizeof(buf)) {
	GPSD_LOG(LOG_ERROR, &context->errout, "internal buffer too small\n");
	return -1;
    }

    memset(buf, 0, (int)sizeof(buf));
    (void)strlcpy(buf, data, length);
    GPSD_LOG(LOG_RAW, &context->errout, "Decoded string: %s\n", buf);

    if (strchr(buf, '_') != NULL) {
	/* value is not valid, ignore it */
	return -2;
    }

    if (strspn(buf, "0123456789") != length) {
	GPSD_LOG(LOG_WARN, &context->errout, "Invalid value %s\n", buf);
	return -1;
    }

    res = (unsigned)atoi(buf);
    if ((res >= min) && (res <= max)) {
	*result = res;
	return 0;		/* SUCCESS */
    } else {
	GPSD_LOG(LOG_WARN, &context->errout,
		 "Value %u out of range <%u, %u>\n", res, min,
		 max);
	return -1;
    }
}


/**************************************************************************
 *
 * Entry points begin here
 *
 **************************************************************************/

gps_mask_t garmintxt_parse(struct gps_device_t * session)
{
/* parse GARMIN Simple Text sentence, unpack it into a session structure */

    gps_mask_t mask = 0;

    GPSD_LOG(LOG_PROG, &session->context->errout,
	     "Garmin Simple Text packet, len %zd: %s\n",
	     session->lexer.outbuflen, (char*)session->lexer.outbuffer);

    if (session->lexer.outbuflen < 54) {
	/* trailing CR and LF can be ignored; ('@' + 54x 'DATA' + '\r\n')
         * has length 57 */
	GPSD_LOG(LOG_WARN, &session->context->errout,
		 "Message is too short, rejected.\n");
	return ONLINE_SET;
    }

    session->lexer.type = GARMINTXT_PACKET;

    /* only one message, set cycle start */
    session->cycle_end_reliable = true;
    do {
	struct tm gdate;		/* date part of last sentence time */
	unsigned int result;
	char *buf = (char *)session->lexer.outbuffer + 1;
	GPSD_LOG(LOG_PROG, &session->context->errout,
                 "Timestamp: %.12s\n", buf);

	/* year */
	if (0 != gar_int_decode(session->context,
				buf + 0, 2, 0, 99, &result))
	    break;
	gdate.tm_year = (session->context->century + (int)result) - 1900;
	/* month */
	if (0 != gar_int_decode(session->context,
				buf + 2, 2, 1, 12, &result))
	    break;
	gdate.tm_mon = (int)result - 1;
	/* day */
	if (0 != gar_int_decode(session->context,
				buf + 4, 2, 1, 31, &result))
	    break;
	gdate.tm_mday = (int)result;
	/* hour */
	if (0 != gar_int_decode(session->context,
				buf + 6, 2, 0, 23, &result))
	    break;
        /* mday update?? */
	gdate.tm_hour = (int)result;
	/* minute */
	if (0 != gar_int_decode(session->context,
				buf + 8, 2, 0, 59, &result))
	    break;
	gdate.tm_min = (int)result;
	/* second */
	/* second value can be even 60, occasional leap second */
	if (0 != gar_int_decode(session->context,
				buf + 10, 2, 0, 60, &result))
	    break;
	gdate.tm_sec = (int)result;
	session->newdata.time.tv_sec = mkgmtime(&gdate);
	session->newdata.time.tv_nsec = 0;
	mask |= TIME_SET;
    } while (0);

    /* assume that position is unknown; if the position is known we
     * will fix status information later */
    session->newdata.mode = MODE_NO_FIX;
    session->gpsdata.status = STATUS_NO_FIX;
    mask |= MODE_SET | STATUS_SET | CLEAR_IS | REPORT_IS;

    /* process position */

    do {
	double lat, lon;
	unsigned int degfrag;
	char status;

	/* Latitude, [NS]ddmmmmm */
	/* decode degrees of Latitude */
	if (0 !=
	    gar_decode(session->context,
		(char *)session->lexer.outbuffer + 13, 3, "NS", 1.0,
		&lat))
	    break;
	/* decode minutes of Latitude */
	if (0 !=
	    gar_int_decode(session->context,
			   (char *)session->lexer.outbuffer + 16, 5, 0,
			   99999, &degfrag))
	    break;
	lat += degfrag * 100.0 / 60.0 / 100000.0;
	session->newdata.latitude = lat;

	/* Longitude, [EW]dddmmmmm */
	/* decode degrees of Longitude */
	if (0 !=
	    gar_decode(session->context,
		       (char *)session->lexer.outbuffer + 21, 4, "EW", 1.0,
		       &lon))
	    break;
	/* decode minutes of Longitude */
	if (0 !=
	    gar_int_decode(session->context,
			   (char *)session->lexer.outbuffer + 25, 5, 0,
			   99999, &degfrag))
	    break;
	lon += degfrag * 100.0 / 60.0 / 100000.0;
	session->newdata.longitude = lon;
        session->newdata.geoid_sep = wgs84_separation(lat, lon);

	/* fix mode, GPS status, [gGdDS_] */
	status = (char)session->lexer.outbuffer[30];

	switch (status) {
	case 'G':
	case 'S':		/* 'S' is DEMO mode, assume 3D position */
	    session->newdata.mode = MODE_3D;
	    session->gpsdata.status = STATUS_FIX;
	    break;
	case 'D':
	    session->newdata.mode = MODE_3D;
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    break;
	case 'g':
	    session->newdata.mode = MODE_2D;
	    session->gpsdata.status = STATUS_FIX;
	    break;
	case 'd':
	    session->newdata.mode = MODE_2D;
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    break;
	default:
	    session->newdata.mode = MODE_NO_FIX;
	    session->gpsdata.status = STATUS_NO_FIX;
	}
	mask |= MODE_SET | STATUS_SET | LATLON_SET;
    } while (0);

    /* EPH */
    do {
	double eph;
	if (0 !=
	    gar_decode(session->context,
		       (char *)session->lexer.outbuffer + 31, 3, "", 1.0,
		       &eph))
	    break;
        /* this conversion looks dodgy... */
	session->newdata.eph = eph * (GPSD_CONFIDENCE / CEP50_SIGMA);
	mask |= HERR_SET;
    } while (0);

    /* Altitude */
    do {
	double alt;
	if (0 !=
	    gar_decode(session->context,
		       (char *)session->lexer.outbuffer + 34, 6, "+-", 1.0,
		       &alt))
	    break;
        /* alt is MSL */
	session->newdata.altMSL = alt;
	/* Let gpsd_error_model() deal with altHAE */
	mask |= ALTITUDE_SET;
    } while (0);

    /* Velocities, meters per second */
    do {
	double ewvel, nsvel;
	double climb;

	if (0 != gar_decode(session->context,
		            (char *)session->lexer.outbuffer + 40, 5,
                            "EW", 10.0, &ewvel))
	    break;
	if (0 != gar_decode(session->context,
		            (char *)session->lexer.outbuffer + 45, 5,
                            "NS", 10.0, &nsvel))
	    break;
	if (0 != gar_decode(session->context,
		            (char *)session->lexer.outbuffer + 50, 5,
                            "UD", 100.0, &climb))
	    break;

        session->newdata.NED.velN = ewvel;
        session->newdata.NED.velE = nsvel;
        session->newdata.NED.velD = -climb;
	mask |= VNED_SET;
    } while (0);

    GPSD_LOG(LOG_DATA, &session->context->errout,
	     "GTXT: time=%lld, lat=%.2f lon=%.2f altMSL=%.2f "
             "climb=%.2f eph=%.2f mode=%d status=%d\n",
	     (long long)session->newdata.time.tv_sec, session->newdata.latitude,
	     session->newdata.longitude, session->newdata.altMSL,
	     session->newdata.climb, session->newdata.eph,
	     session->newdata.mode,
	     session->gpsdata.status);
    return mask;
}

#endif /* GARMINTXT_ENABLE */
