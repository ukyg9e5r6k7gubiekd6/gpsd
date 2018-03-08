/*****************************************************************************

All of gpsd's assumptions about time and GPS time reporting live in this file.

This is a work in progress.  Currently (3.11) GPSD requires that the host
system clock be accurate to within one second.  It would be nice to relax
this to "accurate within one GPS rollover period" for receivers reporting
GPS week+TOW, but this isn't possible in general.

= Begin Sidebar: Why Leap Seconds =

Read this carefully, and if there are errors, please correct.  An
understanding of the following terms is critical to make sense of the
situation, which would be farcical if it were not serious.

We discuss four timescales:

 1. TAI, International Atomic Time, which ticks smoothly
    at the rate of the SI second.  TAI has no concept of a day, year, etc.
    TAI does not define "days" or large units, and is hence difficult
    for humans to parse.  Also, TAI is not broadcast or generally available.
 2. GPS Time, which ticks at at the rate of TAI, but has a constant offset
    from it.  For other GNSS systems, the offset is different.  The
    offset is of purely historical interest, being chosen by each
    GNSS operator for convenience when the systems were inaugurated.
    In other words, only the "epoch" differs between GPS Time and TAI.
 3. UT1, a smoothed earth rotation angle, which MUST return to zero
    once a day, (why?  Because you want the sun to be overhead *each*
    day at the same time on your watch, no?), and ticks SI seconds
    (a non-integeral number of seconds will occur in a UT1 day,
    obviously).  For those of you who still say "GMT", UT1 is the
    closest modern timescale.
 4. UTC, Coordinated Universal Time, which ticks SI seconds.  An attempt is
    made to keep UTC aligned with the rate of flow of seconds (TAI), and
    the rate of flow of days (UT1).

The reason UTC has to struggle has little to do with the fact that the earth's
rotation is slowing down.  Although the length of the day, as measured by UT1,
is lengthening in terms of the SI second, this is a very long term slowdown,
and since 1980, the earth has actually speeded up.

The issue simply is that the term "second" is defined in two incompatible ways:

  Def 1. As a fixed number (9,192,631,770) of cycles of an atomic standard.
    We believe this is a constant, and evidence to the contrary may
    involve GPSD code review, and Nobel Prizes.  This is the SI second.
  Def 2. As 1/86400 of a "day".  The number 86400 arises from
    1 day == 24 * 60 * 60 secs.  This is what we learn in school.

Both of these have been defined separately, and the issue of leap seconds,
rubber seconds, Smoothed Leap Seconds, etc, arises because we are
unwilling to change the definition of either to be a derived unit of the
other.

At the time the SI second was defined, it was believed that Def 2 was correct,
and the number in Def 1 was derived.  Because of ease of measurement, Def 1 was
codified, and the problem was ignored for some time.  Prior to 1972,
complicated formulae were used to scale the SI second, with the attendant
confusion and fear when the formula would be revised.

Since 1972, the start of UTC, the decision to have leap seconds means that UTC
ticks SI seconds.  Every 86400 SI seconds, we declare a new day, and we let the
error (UT1 - UTC) build up. This is of the order of a few ms each midnight, not
always the same way (think earthquakes that move the earth's crust).

Once the error has built up substantially, every few years, we (and by
"we", I mean M. Daniel Gambis at the IERS) declare that a future
day will have 86401 secs.  This is the Leap Second.  Note that this
often overcorrects, but if we wait a few months, the error will disappear.

An animation of this process is available at:
http://space-geodesy.nasa.gov/multimedia/EarthOrientationAnimations/UT1.html

Clear?

Two last things:
 1. Again, the earth slowing down is NOT the cause of leap seconds,
    except very indirectly.  It is the conflict between the two
    definitions above that causes leap seconds
 2. POSIX declares that there is no conflict, there are always 86400 SI
    secs in a day, and hence no leap seconds.  The fact that ostriches
    survive in the wild indicates that this is not as mind-crushingly
    wrong as it may seem.

= End Sidebar =

Date and time in GPS is represented as number of weeks mod 1024 from
1980-01-06T00:00.00Z, and number of SI seconds into the week.  GPS
time is not leap-second corrected, and has a constant offset from TAI,
but not from UTC.

There are hence two issues with converting GPS Time to UTC:

1. We need to recover the epoch difference between TAI and GPS Time,
   which rolls over to 0 every 1024 weeks (approx 20 years).  Think
   of this as analogous to the Y2K problem; we do not know if we are
   off by 1024 weeks.  This is the "rollover" issue below.
2. Once we have the epoch right, we need to adjust for Leap Seconds
   that have been issued.

(Complicating the issue is that most consumer devices may not apply
the corrections when rollover occurs, as this may not be adequately
tested.  We hence have to accept the UTC time reported by the device,
while checking it on the sly).

Satellites also broadcast a current leap-second correction which is
updated on (theoretically) three-month boundaries according to
rotational bulletins issued by the International Earth Rotation and
Reference Systems Service (IERS).  Historically all corrections have
been made on six-month boundaries.

The leap-second correction is only included in the satellite subframe
broadcast, roughly once ever 20 minutes.  While the satellites do
notify GPSes of upcoming leap-seconds, this notification is not
necessarily processed correctly on consumer-grade devices, and will
not be available at all when a GPS receiver has just
cold-booted.  Thus, the time reported from GPS devices, although
supposed to be UTC, may be offset by an integer number of seconds
between a cold boot or leap second and the following
subframe broadcast.

It might be best not to trust time for 20 minutes after GPSD startup
if it is more than 500ms from current system time (that is long enough
for an ephemeris to load) but this isn't actually implemented as the
divergence will normally be only one second or less.

GPS date and time are subject to a rollover problem in the 10-bit week
number counter, which will re-zero every 1024 weeks (roughly every 20
years). The last rollover (and the first since GPS went live in 1980)
was 1999-08-22T00:00:00Z; the next would fall in 2019, but plans are
afoot to upgrade the satellite counters to 13 bits; this will delay
the next rollover until 2173.

For accurate time reporting, therefore, a GPS requires a supplemental
time reference sufficient to identify the current rollover period,
e.g. accurate to within 512 weeks.  Many GPSes have a wired-in
assumption about the UTC time of the last rollover and will thus report
incorrect times outside the rollover period they were designed in.

These conditions leave gpsd in a serious hole.  Actually there are several
interrelated problems:

1) Every device has some assumption about base epoch (date of
last rollover) that we don't have access to.  Thus, there's no way to
check whether a rollover the device wasn't prepared for has occurred
before gpsd startup time (making the reported UTC date invalid)
without some other time source.  (Some devices may keep a
rollover count in NVRAM and avoid the problem; we can't tell when that's
happening, either.)

2) Many NMEA devices - in fact, all that don't report ZDA - never tell
us what century they think it is. Those that do report century are
still subject to rollover problems. We need an external time reference
for this, too.

3) Supposing we're looking at a binary protocol that returns week/tow,
we can't know which rollover period we're in without an external time
source.

4) Only one external time source, the host system clock, is reliably
available, although it may not be accurate.

5) Another source *may* be available - the GPS leap second count, if we can
get the device to report it. The latter is not a given; SiRFs before
firmware rev 2.3.2 don't report it unless special subframe data reporting
is enabled, which requires 38400bps. Evermore GPSes can't be made to
report it at all. Furthermore, before the almanac load the GPS may report
a fixed (and possibly out of date) offset.

Conclusion: if the system clock isn't accurate enough that we can
deduce what rollover period we're in, we're utterly
hosed. Furthermore, if it's not accurate to within a second and only
NMEA devices that don't emit ZDA are reporting, we don't even know
what century it is!

Therefore, we must assume the system clock is reliable to within a second.

However, none of these caveats affect the usefulness of PPS, which
tells us top of second to theoretical 50ns accuracy (actually about 1
microsecond over RS232 and roughly one poll interval over USB) and can
be made to condition a local NTP instance that does *not* rely on the
system clock. The combination of PPS with NTP time should be reliable
regardless of what the local system clock gets up to. That is, unless
NTP clock skew goes over 1 second, but this is unlikely to ever happen
- and if it does the reasons will have nothing to do with GPS
idiosyncracies.

This file is Copyright (c) 2010 -- 2013 by the GPSD project
SPDX-License-Identifier: BSD-2-clause

*****************************************************************************/

/* putenv() needs _XOPEN_SOURCE, 500 means X/Open 1995 */
#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpsd.h"
#include "timebase.h"

void gpsd_time_init(struct gps_context_t *context, time_t starttime)
/* initialize the GPS context's time fields */
{
    /*
     * gpsd can't work with 'right' timezones (leapseconds inserted in
     * the timezone offset).  Avoid this and all manner of other local
     * time issues by telling the system we want times returned in UTC.
     */
    (void)putenv("TZ=UTC");

    /*
     * Provides a start time for getting the century.  Do this, just
     * in case one of our embedded deployments is still in place in
     * the year 2.1K.  Still likely to fail if we bring up the daemon
     * just before a century mark, but that case is probably doomed
     * anyhow because of 2-digit years.
     */
    context->leap_seconds = BUILD_LEAPSECONDS;
    context->century = BUILD_CENTURY;
    context->start_time = starttime;

    context->rollovers = (int)((context->start_time-GPS_EPOCH) / GPS_ROLLOVER);

    if (context->start_time < GPS_EPOCH)
	gpsd_log(&context->errout, LOG_ERROR,
		 "system time looks bogus, dates may not be reliable.\n");
    else {
	/* we've forced the UTC timezone, so this is actually UTC */
	struct tm *now = localtime(&context->start_time);
	char scr[128];
	/*
	 * This is going to break our regression-test suite once a century.
	 * I think we can live with that consequence.
	 */
	now->tm_year += 1900;
	context->century = now->tm_year - (now->tm_year % 100);
	(void)unix_to_iso8601((timestamp_t)context->start_time, scr, sizeof(scr));
	gpsd_log(&context->errout, LOG_INF,
		 "startup at %s (%d)\n",
		 scr, (int)context->start_time);
    }
}

void gpsd_set_century(struct gps_device_t *session)
/*
 * Interpret "Date: yyyy-mm-dd", setting the session context
 * century from the year.  We do this so the behavior of the
 * regression tests won't depend on what century the daemon
 * started up in.
 */
{
    char *end;
    if (strstr((char *)session->lexer.outbuffer, "Date:") != NULL) {
	int year;
	unsigned char *cp = session->lexer.outbuffer + 5;
	while (isspace(*cp))
	    ++cp;
	year = (int)strtol((char *)cp, &end, 10);
	session->context->century = year - (year % 100);
    }
}

#ifdef NMEA0183_ENABLE
timestamp_t gpsd_utc_resolve(struct gps_device_t *session)
/* resolve a UTC date, checking for rollovers */
{
    /*
     * We'd like to *correct* for rollover the way we do for GPS week.
     * In theory, comparing extracted UTC against present time should
     * allow us to compute the device's epoch assumption.  In practice,
     * this will be hairy and risky.
     */
    timestamp_t t;

    t = (timestamp_t)mkgmtime(&session->nmea.date) +
	session->nmea.subseconds;
    session->context->valid &=~ GPS_TIME_VALID;

    /*
     * If the system clock is zero or has a small-integer value,
     * no further sanity-checking is possible.
     */
    if (session->context->start_time < GPS_EPOCH)
	return t;

    /*
     * If the GPS is reporting a time from before the daemon started, we've
     * had a rollover event while the daemon was running.
     */
    if (session->newdata.time < (timestamp_t)session->context->start_time) {
	char scr[128];
	(void)unix_to_iso8601(session->newdata.time, scr, sizeof(scr));
	gpsd_log(&session->context->errout, LOG_WARN,
		 "GPS week rollover makes time %s (%f) invalid\n",
		 scr, session->newdata.time);
    }

    return t;
}

void gpsd_century_update(struct gps_device_t *session, int century)
{
    session->context->valid |= CENTURY_VALID;
    if (century > session->context->century) {
	/*
	 * This mismatch is almost certainly not due to a GPS week
	 * rollover, because that would throw the ZDA report backward
	 * into the last rollover period instead of forward.  Almost
	 * certainly it means that a century mark has passed while
	 * gpsd was running, and we should trust the new ZDA year.
	 */
	gpsd_log(&session->context->errout, LOG_WARN,
		 "century rollover detected.\n");
	session->context->century = century;
    } else if (session->context->start_time >= GPS_EPOCH && century < session->context->century) {
	/*
	 * This looks like a GPS week-counter rollover.
	 */
	gpsd_log(&session->context->errout, LOG_WARN,
		 "ZDA year less than clock year, "
		 "probable GPS week rollover lossage\n");
	session->context->valid &=~ CENTURY_VALID;
    }
}
#endif /* NMEA0183_ENABLE */

timestamp_t gpsd_gpstime_resolve(struct gps_device_t *session,
			 unsigned short week, double tow)
{
    timestamp_t t;

    /*
     * This code detects and compensates for week counter rollovers that
     * happen while gpsd is running. It will not save you if there was a
     * rollover that confused the receiver before gpsd booted up.  It *will*
     * work even when Block IIF satellites increase the week counter width
     * to 13 bits.
     */
    if ((int)week < (session->context->gps_week & 0x3ff)) {
	gpsd_log(&session->context->errout, LOG_INF,
		 "GPS week 10-bit rollover detected.\n");
	++session->context->rollovers;
    }

    /*
     * This guard copes with both conventional GPS weeks and the "extended"
     * 15-or-16-bit version with no wraparound that appears in Zodiac
     * chips and is supposed to appear in the Geodetic Navigation
     * Information (0x29) packet of SiRF chips.  Some SiRF firmware versions
     * (notably 231) actually ship the wrapped 10-bit week, despite what
     * the protocol reference claims.
     */
    if (week < 1024)
	week += session->context->rollovers * 1024;

    t = GPS_EPOCH + (week * SECS_PER_WEEK) + tow;
    t -= session->context->leap_seconds;

    session->context->gps_week = week;
    session->context->gps_tow = tow;
    session->context->valid |= GPS_TIME_VALID;

    return t;
}

/* end */
