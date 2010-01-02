#include <stdlib.h>
#include "gpsd_config.h"
#include <sys/time.h>
#ifdef HAVE_SYS_IOCTL_H
 #include <sys/ioctl.h>
#endif /* HAVE_SYS_IOCTL_H */
#ifndef S_SPLINT_S
 #ifdef HAVE_SYS_SOCKET_H
  #include <sys/socket.h>
 #endif /* HAVE_SYS_SOCKET_H */
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <sys/time.h>
#include <stdio.h>
#include <math.h>
#ifndef S_SPLINT_S
 #ifdef HAVE_NETDB_H
  #include <netdb.h>
 #endif /* HAVE_NETDB_H */
#endif /* S_SPLINT_S */
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "gpsd.h"

/*
 * Support for generic binary drivers.  These functions dump NMEA for passing
 * to the client in raw mode.  They assume that (a) the public gps.h structure 
 * members are in a valid state, (b) that the private members hours, minutes, 
 * and seconds have also been filled in, (c) that if the private member
 * mag_var is not NAN it is a magnetic variation in degrees that should be
 * passed on, and (d) if the private member separation does not have the
 * value NAN, it is a valid WGS84 geoidal separation in meters for the fix.
 */

static double degtodm(double angle)
/* decimal degrees to GPS-style, degrees first followed by minutes */
{
    double fraction, integer;
    fraction = modf(angle, &integer);
    return floor(angle) * 100 + fraction * 60;
}

/*@ -mustdefine @*/
void gpsd_position_fix_dump(struct gps_device_t *session,
			    /*@out@*/char bufp[], size_t len)
{
    struct tm tm;
    time_t intfixtime;

    intfixtime = (time_t)session->gpsdata.fix.time;
    (void)gmtime_r(&intfixtime, &tm);
    if (session->gpsdata.fix.mode > 1) {
	(void)snprintf(bufp, len,
		"$GPGGA,%02d%02d%02d,%09.4f,%c,%010.4f,%c,%d,%02d,",
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		degtodm(fabs(session->gpsdata.fix.latitude)),
		((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
		degtodm(fabs(session->gpsdata.fix.longitude)),
		((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
		session->gpsdata.status,
		session->gpsdata.satellites_used);
	if (isnan(session->gpsdata.dop.hdop))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.2f,",session->gpsdata.dop.hdop);
	if (isnan(session->gpsdata.fix.altitude))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.2f,M,", session->gpsdata.fix.altitude);
	if (isnan(session->gpsdata.separation))
	    (void)strlcat(bufp, ",", len);
	else
	    (void)snprintf(bufp+strlen(bufp), len-strlen(bufp),
			   "%.3f,M,", session->gpsdata.separation);
	if (isnan(session->mag_var))
	    (void)strlcat(bufp, ",", len);
	else {
	    (void)snprintf(bufp+strlen(bufp),
			   len-strlen(bufp),
			   "%3.2f,", fabs(session->mag_var));
	    (void)strlcat(bufp, (session->mag_var > 0) ? "E": "W", len);
	}
	nmea_add_checksum(bufp);
    }
}
/*@ +mustdefine @*/

static void gpsd_transit_fix_dump(struct gps_device_t *session,
				  char bufp[], size_t len)
{
    struct tm tm;
    time_t intfixtime;

    tm.tm_mday = tm.tm_mon = tm.tm_year = tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    if (isnan(session->gpsdata.fix.time)==0) {
	intfixtime = (time_t)session->gpsdata.fix.time;
	(void)gmtime_r(&intfixtime, &tm);
	tm.tm_mon++;
	tm.tm_year %= 100;
    }
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
    /*@ -usedef @*/
    (void)snprintf(bufp, len,
	    "$GPRMC,%02d%02d%02d,%c,%09.4f,%c,%010.4f,%c,%.4f,%.3f,%02d%02d%02d,,",
	    tm.tm_hour,
	    tm.tm_min,
	    tm.tm_sec,
	    session->gpsdata.status ? 'A' : 'V',
	    ZEROIZE(degtodm(fabs(session->gpsdata.fix.latitude))),
	    ((session->gpsdata.fix.latitude > 0) ? 'N' : 'S'),
	    ZEROIZE(degtodm(fabs(session->gpsdata.fix.longitude))),
	    ((session->gpsdata.fix.longitude > 0) ? 'E' : 'W'),
	    ZEROIZE(session->gpsdata.fix.speed * MPS_TO_KNOTS),
	    ZEROIZE(session->gpsdata.fix.track),
	    tm.tm_mday,
	    tm.tm_mon,
	    tm.tm_year);
    /*@ +usedef @*/
#undef ZEROIZE
    nmea_add_checksum(bufp);
}

static void gpsd_binary_satellite_dump(struct gps_device_t *session,
				char bufp[], size_t len)
{
    int i;
    char *bufp2 = bufp;
    bufp[0] = '\0';

    for( i = 0 ; i < session->gpsdata.satellites_visible; i++ ) {
	if (i % 4 == 0) {
	    bufp += strlen(bufp);
	    bufp2 = bufp;
	    len -= snprintf(bufp, len,
		    "$GPGSV,%d,%d,%02d",
		    ((session->gpsdata.satellites_visible-1) / 4) + 1,
		    (i / 4) + 1,
		    session->gpsdata.satellites_visible);
	}
	bufp += strlen(bufp);
	if (i < session->gpsdata.satellites_visible)
	    len -= snprintf(bufp, len,
		    ",%02d,%02d,%03d,%02.0f",
		    session->gpsdata.PRN[i],
		    session->gpsdata.elevation[i],
		    session->gpsdata.azimuth[i],
		    session->gpsdata.ss[i]);
	if (i % 4 == 3 || i == session->gpsdata.satellites_visible-1) {
	    nmea_add_checksum(bufp2);
	    len -= 5;
	}
    }

#ifdef ZODIAC_ENABLE
    if (session->packet.type == ZODIAC_PACKET && session->driver.zodiac.Zs[0] != 0) {
	bufp += strlen(bufp);
	bufp2 = bufp;
	(void)strlcpy(bufp, "$PRWIZCH", len);
	for (i = 0; i < ZODIAC_CHANNELS; i++) {
	    len -= snprintf(bufp+strlen(bufp), len,
			  ",%02u,%X",
			    session->driver.zodiac.Zs[i],
			    session->driver.zodiac.Zv[i] & 0x0f);
	}
	nmea_add_checksum(bufp2);
    }
#endif /* ZODIAC_ENABLE */
}

static void gpsd_binary_quality_dump(struct gps_device_t *session,
			      char bufp[], size_t len)
{
    int	i, j;
    char *bufp2 = bufp;
    bool used_valid = (session->gpsdata.set & USED_SET)!= 0;

    if (session->device_type!=NULL && (session->gpsdata.set & MODE_SET) != 0) {
	(void)snprintf(bufp, len-strlen(bufp),
		       "$GPGSA,%c,%d,", 'A', session->gpsdata.fix.mode);
	j = 0;
	for (i = 0; i < session->device_type->channels; i++) {
	    if (session->gpsdata.used[i]) {
		bufp += strlen(bufp);
		(void)snprintf(bufp, len-strlen(bufp),
			       "%02d,", used_valid ? session->gpsdata.used[i] : 0);
		j++;
	    }
	}
	for (i = j; i < session->device_type->channels; i++) {
	    bufp += strlen(bufp);
	    (void)strlcpy(bufp, ",", len);
	}
	bufp += strlen(bufp);
#define ZEROIZE(x)	(isnan(x)!=0 ? 0.0 : x)
	if (session->gpsdata.fix.mode == MODE_NO_FIX)
	    (void)strlcat(bufp, ",,,", len);
	else
	    (void)snprintf(bufp, len-strlen(bufp),
			   "%.1f,%.1f,%.1f*",
			   ZEROIZE(session->gpsdata.dop.pdop),
			   ZEROIZE(session->gpsdata.dop.hdop),
			   ZEROIZE(session->gpsdata.dop.vdop));
	nmea_add_checksum(bufp2);
	bufp += strlen(bufp);
    }
    if (finite(session->gpsdata.fix.epx)
	&& finite(session->gpsdata.fix.epy)
	&& finite(session->gpsdata.fix.epv)
	&& finite(session->gpsdata.epe)) {
	struct tm tm;
	time_t intfixtime;

	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	if (isnan(session->gpsdata.fix.time)==0) {
	    intfixtime = (time_t)session->gpsdata.fix.time;
	    (void)gmtime_r(&intfixtime, &tm);
	}
	(void)snprintf(bufp, len-strlen(bufp),
		       "$GPGBS,%02d%02d%02d,%.2f,M,%.2f,M,%.2f,M",
		       tm.tm_hour, tm.tm_min, tm.tm_sec,
		       ZEROIZE(session->gpsdata.fix.epx),
		       ZEROIZE(session->gpsdata.fix.epy),
		       ZEROIZE(session->gpsdata.fix.epv));
	nmea_add_checksum(bufp);
    }
#undef ZEROIZE
}

/*@-compdef -mustdefine@*/
void nmea_tpv_dump(struct gps_device_t *session,
			      /*@out@*/char bufp[], size_t len)
{
    bufp[0] = '\0';
    if ((session->gpsdata.set & LATLON_SET) != 0) {
	gpsd_position_fix_dump(session, bufp, len);
	gpsd_transit_fix_dump(session, bufp + strlen(bufp), len - strlen(bufp));
    }
    if ((session->gpsdata.set & (MODE_SET | DOP_SET | USED_SET | HERR_SET | VERR_SET)) != 0)
	gpsd_binary_quality_dump(session, bufp+strlen(bufp), len-strlen(bufp));
}

void nmea_sky_dump(struct gps_device_t *session,
			      /*@out@*/char bufp[], size_t len)
{
    bufp[0] = '\0';
    if ((session->gpsdata.set & SATELLITE_SET) != 0)
	gpsd_binary_satellite_dump(session,bufp+strlen(bufp),len-strlen(bufp));
}
/*@+compdef +mustdefine@*/

/* pseudonmea.c ends here */


