/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <sys/types.h>

#include "gpsd.h"
#include "timebase.h"

#if 0
static char sf4map[] =
    { -1, 57, 25, 26, 27, 28, 57, 29, 30, 31, 32, 57, 62, 52, 53, 54, 57, 55,
    56, 58, 59, 57, 60, 61, 62, 63
};

static char sf5map[] =
    { -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 51
};
#endif

/*@ -usedef @*/
int gpsd_interpret_subframe_raw(struct gps_device_t *session,
			     unsigned int words[])
{
    unsigned int i;
    unsigned int preamble, parity;

    /* Data is in ICD 200d format */
    /* ICD == Interface Control Document */
    /* download from http://www.navcen.uscg.gov/GPS/ICD200c.htm */
    /* FIXME, the data is flakey, need to check  'parity' which is really a
     * hamming code */

    /* ICD words are really 30 bits, and their LSB is the LSB of the 32 bit
     * int transporting them. The right most 6 bits are 'parity' and the top
     * 2 bits are the bottom two parity bits from the previous word. Mask and
     * shift these away to leave us with 3 data bytes per word */

    /* gotta do the first word by hand, D29* and D30* often missing */
    preamble = (words[0] >> 22) & 0x0ff;
    if (preamble == 0x8b) {
	preamble ^= 0xff;
	words[0] ^= 0x3fffC0;
    }

    for (i = 1; i < 10; i++) {
	int invert;
	/* D30* says invert */
	invert = (words[i] & 0x40000000) ? 1 : 0;
	/* inverted data, invert it back */
	if (invert) {
	    words[i] ^= 0x3fffffC0;
	}
	parity = isgps_parity(words[i]);
	if (parity != (words[i] & 0x3F)) {
	    gpsd_report(LOG_PROG,
			"50BPS parity fail words[%d] 0x%x != 0x%x\n", i,
			parity, (words[i] & 0x1));
	    return 0;
	}
	words[i] = (words[i] & 0x3fffffff) >> 6;
    }
    gpsd_report(LOG_PROG, "50BPS 0x08: "
		"%06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
		words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);
    // Look for the preamble in the first byte OR its complement
    if (preamble != 0x74) {
	gpsd_report(LOG_WARN, "50BPS bad premable: 0x%x header 0x%x\n",
		    preamble, words[0]);
	return 0;
    }

    gpsd_interpret_subframe(session, words);
}

void gpsd_interpret_subframe(struct gps_device_t *session,
			     unsigned int words[])
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
    unsigned int pageid, subframe, data_id, leap, lsf, wnlsf, dn;
    gpsd_report(LOG_PROG,
		"50B: (raw) %06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
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
    gpsd_report(LOG_PROG, "50B: Subframe %d SVID %d data_id %d\n", subframe,
		pageid, data_id);
    switch (subframe) {
    case 1:
    	/* get Week Number WN) from subframe 1 */
	session->context->gps_week = (words[2] & 0xffc000) >> 14; 
	gpsd_report(LOG_PROG, 
	    "50B: WN: %u\n", session->context->gps_week);
	break;
    case 4:
	switch (pageid) {
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
	    gpsd_report(LOG_INF, "50B: gps system message is %s\n", str);
	}
	    break;
	case 56:
	    leap = (words[8] & 0xff0000) >> 16;  /* current leap seconds */
	    /* careful WN is 10 bits, but WNlsf is 8 bits! */
	    wnlsf = (words[8] & 0x00ff00) >>  8; /* WNlsf (Week Number of LSF) */
	    dn = (words[8] & 0x0000FF);          /* DN (Day Number of LSF) */
	    lsf = (words[9] & 0xff0000) >> 16;   /* leap second future */
	    /*
	     * On SiRFs, the 50BPS data is passed on even when the
	     * parity fails.  This happens frequently.  So the driver 
	     * must be extra careful that bad data does not reach here.
	     */
	    if (LEAP_SECONDS > leap) {
		/* something wrong */
		gpsd_report(LOG_ERROR, "50B: Invalid leap_seconds: %d\n", leap);
		leap = LEAP_SECONDS;
		session->context->valid &= ~LEAP_SECOND_VALID;
	    } else {
		gpsd_report(LOG_INF, 
		    "50B: leap-seconds: %d, lsf: %d, WNlsf: %d, DN: %d \n", 
		    leap, lsf, wnlsf, dn);
		session->context->valid |= LEAP_SECOND_VALID;
		if ( leap != lsf ) {
			gpsd_report(LOG_PROG, "50B: leap-second change coming\n");
		}
	    }
	    session->context->leap_seconds = (int)leap;
	    break;
	default:
	    ;			/* no op */
	}
	break;
    }
}

/*@ +usedef @*/
