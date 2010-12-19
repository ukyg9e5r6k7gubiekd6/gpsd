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
 * to generate an integer value for START_SUBFRAME, or use the
 * -n option of devtools/leapsecond.py in the source distribution
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

/* Date of next possible leap second adjustment, according to IERS
 */
#define START_SUBFRAME	1309492799	/* 30 Jun 2011 23:59:59 */

/*
 * Default century.  Gets used if the system clock value at startup
 * time looks invalid.
 */
#define CENTURY_BASE	2000

/* timebase.h ends here */
#endif /* _GPSD_TIMEBASE_H_ */
