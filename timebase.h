/* $Id$ */
#ifndef _GPSD_TIMEBASE_H_
#define _GPSD_TIMEBASE_H_

/* timebase.h -- constants that will require patching over time */
/*
 * The current (fixed) leap-second correction, and the future Unix
 * time after which to start hunting leap-second corrections from GPS
 * subframe data if the GPS doesn't supply them any more readily.
 *
 * Deferring the check is a hack to speed up fix acquisition --
 * subframe data is bulky enough to substantially increase latency.
 * To update LEAP_SECONDS and START_SUBFRAME, see the IERS leap-second
 * bulletin page at:
 * <http://hpiers.obspm.fr/eop-pc/products/bulletins/bulletins.html>
 *
 * You can use the Python expression
 *	time.mktime(time.strptime(... , "%d %b %Y %H:%M:%S"))
 * to generate an integer value for START_SUBFRAME. 
 */

/*
 * This constant is used to get UTC from chipsets that report GPS time only.
 *
 * It's not a disaster if it's wrong; most such chips get the offset
 * from some report abstracted from the subframe data, so their worst
 * case is their time info will be incorrect for the remainder of an
 * entire GPS message cycle (about 22 minutes) if you start gpsd up
 * right after a leap-second.
 *
 * This value is only critical if the chipset gets GPS time only and
 * not the offset; in this case gpsd will always report UTC that is exactly
 * as incorrect as the constant.  Currently this is true only for the
 * Evermore chipset.
 */
#define LEAP_SECONDS	15

/* IERS says no leap second will be inserted in December 2009.
 */
#define START_SUBFRAME	1277956799	/* 31 June 2010 23:59:59 */

/*
 * This is used only when an NMEA device issues a two-digit year in a GPRMC
 * and there has been no previous ZDA to set the year.  We used to
 * query the system clock for this,  but there's no good way to cope 
 * with the mess if the system clock has been zeroed.
 */
#define CENTURY_BASE	2000

/* timebase.h ends here */
#endif /* _GPSD_TIMEBASE_H_ */
