#ifndef _GPSD_TIMEBASE_H_
#define _GPSD_TIMEBASE_H_

/* timebase.h -- constants that will require patching over time */

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

/*
 * Default century.  Gets used if the system clock value at startup
 * time looks invalid.
 */
#define CENTURY_BASE	2000

/* timebase.h ends here */
#endif /* _GPSD_TIMEBASE_H_ */
