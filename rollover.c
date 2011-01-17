/*****************************************************************************

Excerpted from my blog entry on this code:

The root cause of the Rollover of Doom is the peculiar time
reference that GPS uses.  Times are expressed as two numbers: a count
of weeks since the start of 1980, and a count of seconds in the
week. So far so good - except that, for hysterical raisins, the week
counter is only 10 bits long.  The first week rollover was in 1999;
the second will be in 2019.

So, what happens on your GPS when you reach counter zero?  Why, the
time it reports warps back to the date of the last rollover, currently
1999.  Obviously, if you are logging or computing anything
time-dependent through a rollover and relying on GPS time, you are
screwed.

Now, we do get one additional piece of time information: the current 
leap-second offset. The object of this exercise is to figure out what 
you can do with it.

In order to allow UTC to be computed from the GPS-week/GPS-second
pair, the satellite also broadcasts a cumulative leap-second offset.
The offset was 0 when the system first went live; in January 2010
it is 15 seconds. It is updated every 6 months based on spin measurements
by the <a href="http://www.iers.org/">IERS</a>.

For purposes of this exercise, you get to assume that you have a table
of leap seconds handy, in Unix time (seconds since midnight before 1
Jan 1970, UTC corrected).  You do *not* get to assume that your table
of leap seconds is current to date, only up to when you shipped your
software.

For extra evilness, you also do not get to assume that the week rollover
period is constant. The not-yet-deployed Block III satellites will have
13-bit week rollover counters, pushing the next rollover back to 2173AD.

For extra-special evilness, there are two different ways your GPS date
could be clobbered. after a rollover.  If your receiver firmware was
designed by an idiot, all GPS week/second pairs will be translated
into an offset from the last rollover, and date reporting will go
wonky precisely on the next rollover.  If your designer is slightly
more clever, GPS dates between the last rollover and the ship date of
the receiver firmware will be mapped into offsets from the *next*
rollover, and date reporting will stay sane for an entire 19 years
from that ship date.

You are presented with a GPS time (a week-counter/seconds-in-week
pair), and a leap-second offset. You also have your (incomplete) table
of leap seconds. The GPS week counter may invalid due to the Rollover
of Doom. Specify an algorithm that detects rollover cases as often as
possible, and explain which cases you cannot detect.

Hint: This problem is a Chinese finger-trap for careful and conscientious
programmers. The better you are, the worse this problem is likely to hurt
your brain. Embrace the suck.

I continue:

*Good* programmers try to solve this problem by using the leap second to
pin down the epoch date of the current rollover period so they can correct
the week counter.  There are several reasons this cannot work.

One is that you will not *know* the cumulative leap-second offset for
times after your software ships, so there is no way to know how the
leap-second offset you are handed maps to a year in that case.  Even
if you did, somehow, havd a list of all future leap seconds, the
mapping from leap second to year has about two years of uncertainty on
average.  This means applying that mapping could lead you to guess a
year on the wrong side of the nearest rollover line about one tenth of
the time.

Here is what you can do.

If the timestamp you are handed is within the range of the first and
last entries, check the leap-second offset.  If it is correct for that
range, there has been no rollover.  If it does not match the
leap-second offset for that range, your date is from a later rollover
period than your receiver was designed to handle and has gotten
clobbered.

The odd thing about this test is the range of rollover cases it can detect.
If your table covers a range of N seconds after the last rollover, it will
detect rollover from all future weeks as long as they are within N seconds
after *their* rollover.  It will be leaast effective from a software release
immediately after a rollover and most effective from a release immediately
before one.

Much of the time, this algorithm will return "I cannot tell".  The reason this
problem is like a Chinese finger trap is because good programmers will hurt
their brains trying to come up with a solution that does not punt any cases.

*****************************************************************************/

#include "gpsd.h"

static int gpsd_check_utc(const int leap, const double unixtime)
/* consistency-check a GPS-reported UTC against a leap second */
{
    static double c_epochs[] = {
#include "leapcheck.i"
    };
    #define DIM(a) (int)(sizeof(a)/sizeof(a[0]))

    if (leap < 0 || leap >= DIM(c_epochs))
        return -1;   /* cannot tell, leap second out of table bounds */
    else if (unixtime < c_epochs[0] || unixtime >= c_epochs[DIM(c_epochs)-1])
        return -1;   /* cannot tell, time not in table */
    else if (unixtime >= c_epochs[leap] && unixtime <= c_epochs[leap+1])
        return 1;    /* leap second consistent with specified year */
    else
        return 0;    /* leap second inconsistent, probable rollover error */
}

void gpsd_rollover_check(/*@in@*/struct gps_device_t *session, 
			 const double unixtime)
{

    /*
     * Check the time passed in against the leap-second offset the satellites
     * are reporting.  After a rollover, the receiver will probably report a
     * time far enough in the past that it won't be consistent with the
     * leap-second value.
     */
    if ((session->context->valid & LEAP_SECOND_VALID)!=0 && 
	gpsd_check_utc(session->context->leap_seconds, unixtime) == 0) {
	char scr[128];
	(void)unix_to_iso8601(unixtime, scr, sizeof(scr));
	gpsd_report(LOG_WARN, "leap-second %d is impossible at time %s (%f)\n", 
		    session->context->leap_seconds, scr, unixtime);
    }

    /*
     * If the system clock is zero or has a small-integer value,
     * no further sanity-checking is possible.
     */
    if (session->context->start_time < GPS_EPOCH)
	return;

    /*
     * If the GPS is reporting a time from before the daemon started, we've
     * had a rollover event while the daemon was running.
     *
     * The reason for the 12-hour slop is that our recorded start time is local,
     * but GPSes deliver time as though in UTC.  This test could be exact if we
     * counted on knowing our timezone at startup, but since we can't count on
     * knowing location...
     */
    if (unixtime + (12*60*60) < (double)session->context->start_time) {
	char scr[128];
	(void)unix_to_iso8601(unixtime, scr, sizeof(scr));
	gpsd_report(LOG_WARN, "GPS week rollover makes time %s (%f) invalid\n", 
		    scr, unixtime);
    }
}

double gpsd_resolve_time(/*@in@*/struct gps_device_t *session,
			 unsigned short week, double tow)
{
    double t;
    session->context->gps_week = week;
    session->context->gps_tow = tow;
    t = gpstime_to_unix((int)week, session->context->gps_tow)
	- session->context->leap_seconds;
    gpsd_rollover_check(session, session->newdata.time);
    return t;
}

/* end */
