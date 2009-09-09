/* $Id$ */
/* subframe.c -- interpret satellite subframe data. */
#include <sys/types.h>
#include "gpsd_config.h"
#include "gpsd.h"

#if 0
static char sf4map[] = {-1, 57, 25, 26, 27, 28, 57, 29, 30, 31, 32, 57, 62, 52, 53, 54, 57, 55, 56, 58, 59, 57, 60, 61, 62, 63};
static char sf5map[] = {-1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 51};
#endif

/*@ -usedef @*/
void gpsd_interpret_subframe(struct gps_device_t *session,unsigned int words[])
/* extract leap-second from RTCM-104 subframe data */
{
    /*
     * Heavy black magic begins here!
     *
     * A description of how to decode these bits is at
     * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
     *
     * We're after subframe 4 page 18 word 9, the leap second correction.
     * We assume that the chip is presenting clean data that has been
     * parity-checked.
     *
     * To date this code has been tested on iTrax, SiRF and ublox. It's in
     * the core because other chipsets reporting only GPS time but with the
     * capability to read subframe data may want it.
     */
    unsigned int pageid, subframe, data_id, leap;
    gpsd_report(LOG_IO,
		"50B (raw): %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);
    /*
     * It is the responsibility of each driver to mask off the high 2 bits
     * and shift out the 6 parity bits or do whatever else is necessary to
     * present an array of acceptable words to this decoder. Hopefully parity
     * checks are done in hardware, but if not, the driver should do them.
     */

    /* The subframe ID is in the Hand Over Word (page 80) */
    subframe = ((words[1] >> 2) & 0x07);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    pageid = (words[2] & 0x3F0000) >> 16;
    data_id = (words[2] >> 22) & 0x3;
    gpsd_report(LOG_PROG, "Subframe %d SVID %d data_id %d\n", subframe, pageid, data_id);
    /* we're not interested in anything but subframe 4 - for now*/
    if (subframe != 4)
	return;
    /* once we've filtered, we can ignore the TEL and HOW words */
    gpsd_report(LOG_PROG, "50B: %06x %06x %06x %06x %06x %06x %06x %06x\n",
	    words[2], words[3], words[4], words[5],
	    words[6], words[7], words[8], words[9]);
    switch(pageid){
    case 55:
	/*
	 * "The requisite 176 bits shall occupy bits 9 through 24 of word
	 * TWO, the 24 MSBs of words THREE through EIGHT, plus the 16 MSBs
	 * of word NINE." (word numbers changed to account for zero-indexing)
	 *
	 * Since we've already stripped the low six parity bits, and shifted
	 * the data to a byte boundary, we can just copy it out. */
	{
	char str[24];
	int j = 0;
	/*@ -type @*/
	str[j++] = (words[2] >> 8) & 0xff;
	str[j++] = (words[2]) & 0xff;

	str[j++] = (words[3] >> 16) & 0xff;
	str[j++] = (words[3] >> 8) & 0xff;
	str[j++] = (words[3]) & 0xff;

	str[j++] = (words[4] >> 16) & 0xff;
	str[j++] = (words[4] >> 8) & 0xff;
	str[j++] = (words[4]) & 0xff;

	str[j++] = (words[5] >> 16) & 0xff;
	str[j++] = (words[5] >> 8) & 0xff;
	str[j++] = (words[5]) & 0xff;

	str[j++] = (words[6] >> 16) & 0xff;
	str[j++] = (words[6] >> 8) & 0xff;
	str[j++] = (words[6]) & 0xff;

	str[j++] = (words[7] >> 16) & 0xff;
	str[j++] = (words[7] >> 8) & 0xff;
	str[j++] = (words[7]) & 0xff;

	str[j++] = (words[8] >> 16) & 0xff;
	str[j++] = (words[8] >> 8) & 0xff;
	str[j++] = (words[8]) & 0xff;

	str[j++] = (words[9] >> 16) & 0xff;
	str[j++] = (words[9] >> 8) & 0xff;
	str[j++] = '\0';
	/*@ +type @*/
	gpsd_report(LOG_INF, "gps system message is %s\n", str);
	}
	break;
    case 56:
	leap = (words[8] & 0xff0000) >> 16;
	/*
	 * On SiRFs, there appears to be some bizarre bug that
	 * randomly causes this field to come out two's-complemented.
	 * This could very well be a general problem; work around it.
	 * At the current expected rate of issuing leap-seconds this
	 * kluge won't bite until about 2070, by which time the
	 * vendors had better have fixed their damn firmware...
	 *
	 * Carl: ...I am unsure, and suggest you
	 * experiment.  The D30 bit is in bit 30 of the 32-bit
	 * word (next to MSB), and should signal an inverted
	 * value when it is one coming over the air.  But if
	 * the bit is set and the word decodes right without
	 * inversion, then we properly caught it.  Cases where
	 * you see subframe 6 rather than 1 means we should
	 * have done the inversion but we did not.  Some other
	 * things you can watch for: in any subframe, the
	 * second word (HOW word) should have last 2 parity
	 * bits 00 -- there are bits within the rest of the
	 * word that are set as required to ensure that.  The
	 * same goes for word 10.  That means that both words
	 * 1 and 3 (the words that immediately follow words 10
	 * and 2, respectively) should always be uninverted.
	 * In these cases, the D29 and D30 from the previous
	 * words, found in the two MSBs of the word, should
	 * show 00 -- if they don't then you may find an
	 * unintended inversion due to noise on the data link.
	 */
	if (leap > 128)
	    leap ^= 0xff;
	gpsd_report(LOG_INF, "leap-seconds is %d\n", leap);
	session->context->leap_seconds = (int)leap;
	session->context->valid |= LEAP_SECOND_VALID;
	break;
    default:
	    ; /* no op */
    }
}
/*@ +usedef @*/
