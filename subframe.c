/* subframe.c -- interpret satellite subframe data.
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include "gpsd.h"
#include "timebase.h"

#ifdef __NOT_YET__
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
				unsigned int svid, unsigned int words[])
{
    unsigned int i;
    unsigned int preamble, parity;

    /*
     * This function assumes an array of 10 ints, each of which carries
     * a raw 30-bit GPS word use your favorite search engine to find the
     * latest version of the specification: IS-GPS-200.
     *
     * Each raw 30-bit word is made of 24 data bits and 6 parity bits. The
     * raw word and transport word are emitted from the GPS MSB-first and
     * right justified. In other words, masking the raw word against 0x3f
     * will return just the parity bits. Masking with 0x3fffffff and shifting
     * 6 bits to the right returns just the 24 data bits. The top two bits
     * (b31 and b30) are undefined; chipset designers may store copies of
     * the bits D29* and D30* here to aid parity checking.
     *
     * Since bits D29* and D30* are not available in word 0, it is tested for
     * a known preamble to help check its validity and determine whether the
     * word is inverted.
     *
     */
    gpsd_report(LOG_IO, "50B: gpsd_interpret_subframe_raw: "
		"%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);

    preamble = (words[0] >> 22) & 0xff;
    if (preamble == 0x8b) {	/* preamble is inverted */
	words[0] ^= 0x3fffffc0;	/* invert */
    } else if (preamble != 0x74) {
	gpsd_report(LOG_WARN,
		    "50B: gpsd_interpret_subframe_raw: bad preamble 0x%x\n",
		    preamble);
	return 0;
    }
    words[0] = (words[0] >> 6) & 0xffffff;

    for (i = 1; i < 10; i++) {
	int invert;
	/* D30* says invert */
	invert = (words[i] & 0x40000000) ? 1 : 0;
	/* inverted data, invert it back */
	if (invert) {
	    words[i] ^= 0x3fffffc0;
	}
	parity = isgps_parity(words[i]);
	if (parity != (words[i] & 0x3f)) {
	    gpsd_report(LOG_PROG,
			"50B: gpsd_interpret_subframe_raw parity fail words[%d] 0x%x != 0x%x\n",
			i, parity, (words[i] & 0x1));
	    return 0;
	}
	words[i] = (words[i] >> 6) & 0xffffff;
    }

    gpsd_interpret_subframe(session, svid, words);
    return 0;
}

void gpsd_interpret_subframe(struct gps_device_t *session,
			     unsigned int svid, unsigned int words[])
{
    /*
     * Heavy black magic begins here!
     *
     * A description of how to decode these bits is at
     * <http://home-2.worldonline.nl/~samsvl/nav2eu.htm>
     *
     * We're mostly looking for subframe 4 page 18 word 9, the leap second
     * correction. This functions assumes an array of words without parity
     * or inversion (inverted word 0 is OK). It may be called directly by a
     * driver if the chipset emits acceptable data.
     *
     * To date this code has been tested on iTrax, SiRF and ublox.
     */
    unsigned int pageid, subframe, data_id, leap, lsf, wnlsf, dn, preamble;
    gpsd_report(LOG_IO,
		"50B: gpsd_interpret_subframe: (%d) "
		"%06x %06x %06x %06x %06x %06x %06x %06x %06x %06x\n",
		svid, words[0], words[1], words[2], words[3], words[4],
		words[5], words[6], words[7], words[8], words[9]);

    preamble = (unsigned int)((words[0] >> 16) & 0xffL);
    if (preamble == 0x8b) {
	preamble ^= 0xff;
	words[0] ^= 0xffffff;
    }
    if (preamble != 0x74) {
	gpsd_report(LOG_WARN,
		    "50B: gpsd_interpret_subframe bad preamble: 0x%x header 0x%x\n",
		    preamble, words[0]);
	return;
    }
    /* The subframe ID is in the Hand Over Word (page 80) */
    subframe = ((words[1] >> 2) & 0x07);
    gpsd_report(LOG_PROG,
		"50B: gpsd_interpret_subframe: Subframe %d\n", subframe);
    /*
     * Consult the latest revision of IS-GPS-200 for the mapping
     * between magic SVIDs and pages.
     */
    pageid = (words[2] & 0x3F0000) >> 16; /* only in frames 4 & 5 */
    data_id = (words[2] >> 22) & 0x3;     /* only in frames 4 & 5 */

    switch (subframe) {
    case 1:
        /* subframe 1: clock parameters */
	/* get Week Number WN) from subframe 1 */
	session->context->gps_week =
	    (unsigned short)((words[2] & 0xffc000) >> 14);
	gpsd_report(LOG_PROG, "50B: WN: %u\n", session->context->gps_week);
	break;
    case 2:
        /* subframe 2: ephemeris for transmitting SV */
    	break;
    case 3:
        /* subframe 3: ephemeris for transmitting SV */
    	break;
    case 4:
	gpsd_report(LOG_PROG,
		"50B: gpsd_interpret_subframe: Page 4-%d data_id %d\n",
		pageid, data_id);
	switch (pageid) {
	case 1:
	case 6:
	case 11:
	case 12:
	case 14:
	case 15:
	case 16:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
	case 24:
	    /* reserved pages */
	    break;

	case 2:
	case 3:
	case 4:
	case 5:
	case 7:
	case 8:
	case 9:
	case 10:
	    /* almanac data for SV 25 through 32 respectively; */
	    break;

	case 13:
	    /* NMCT */
	    break;
	
	case 17:
	    /* special messages */
	    break;
	
	case 18:
	    /* ionospheric and UTC data */
	    break;
	
	case 25:
	    /* A-S flags/SV configurations for 32 SVs, 
	     * plus SV health for SV 25 through 32
	     */
	    break;
	
	case 55:
	    /* FIXME!! there is no page 55!! */
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
	    leap = (words[8] & 0xff0000) >> 16;	/* current leap seconds */
	    /* careful WN is 10 bits, but WNlsf is 8 bits! */
	    wnlsf = (words[8] & 0x00ff00) >> 8;	/* WNlsf (Week Number of LSF) */
	    dn = (words[8] & 0x0000FF);	/* DN (Day Number of LSF) */
	    lsf = (words[9] & 0xff0000) >> 16;	/* leap second future */
	    /*
	     * On SiRFs, the 50BPS data is passed on even when the
	     * parity fails.  This happens frequently.  So the driver 
	     * must be extra careful that bad data does not reach here.
	     */
	    if (LEAP_SECONDS > leap) {
		/* something wrong */
		gpsd_report(LOG_ERROR, "50B: Invalid leap_seconds: %d\n",
			    leap);
		leap = LEAP_SECONDS;
		session->context->valid &= ~LEAP_SECOND_VALID;
	    } else {
		gpsd_report(LOG_INF,
			    "50B: leap-seconds: %d, lsf: %d, WNlsf: %d, DN: %d \n",
			    leap, lsf, wnlsf, dn);
		session->context->valid |= LEAP_SECOND_VALID;
		if (leap != lsf) {
		    gpsd_report(LOG_PROG, "50B: leap-second change coming\n");
		}
	    }
	    session->context->leap_seconds = (int)leap;
	    break;
	default:
	    ;			/* no op */
	}
	break;
    case 5:
	gpsd_report(LOG_PROG,
		"50B: gpsd_interpret_subframe: Page 5-%d data_id %d\n",
		pageid, data_id);
	/* Pages 1 through 24: almanac data for SV 1 through 24
	 * Page 25: SV health data for SV 1 through 24, the almanac 
	 * reference time, the almanac reference week number.
	 */
    	break;
    default:
    	/* unknown/illegal subframe */
	break;
    }
    return;
}

/*@ +usedef @*/
