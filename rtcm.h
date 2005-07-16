/*
 * Structures for interpreting words in an RTCM-104 message (after
 * parity checking and removing inversion). RTCM104 is an obscure and
 * complicated serial protocol used for broadcasting pseudorange
 * corrections from differential-GPS reference stations. This header
 * is part of the GPSD package: see <http://gpsd.berlios.de> for more.
 *
 * The RTCM words are 30-bit words.  We will lay them into memory into
 * 30-bit (low-end justified) chunks.  To write them out we will write
 * 5 Magnavox-format bytes where the low 6-bits of the byte are 6-bits
 * of the 30-word msg.
 */

typedef /*@unsignedintegraltype@*/ unsigned int RTCMWORD;

/*  
 * From the RCTM104 standard:
 *
 * "The 30 bit words (as opposed to 32 bit words) coupled with a 50 Hz
 * transmission rate provides a convenient timing capability where the
 * times of word boundaries are a rational multiple of 0.6 seconds."
 *
 * "Each frame is N+2 words long, where N is the number of message data
 * words. For example, a filler message (type 6 or 34) with no message
 * data will have N=0, and will consist only of two header words. The
 * maximum number of data words allowed by the format is 31, so that
 * the longest possible message will have a total of 33 words."
 */
#define RTCM_WORDS_MAX	33

#define MAXCORRECTIONS	15	/* max correction count in type 1 or 9 */

struct rtcm_t {
    /* header contents */
    unsigned type;	/* RTCM message type */
    unsigned length;	/* length (words) */
    double   zcount;	/* time within hour -- GPS time, no leap seconds */
    unsigned refstaid;	/* reference station ID */
    unsigned seqnum;	/* nessage sequence number (modulo 8) */
    unsigned stathlth;	/* station health */

    /* message data in decoded form */
    union {
	struct {		/* data for messages 1 and 9 */
	    unsigned satident;	/* satellite ID */
	    unsigned udre;	/* user differential range error */
	    unsigned issuedata;	/* issue of data */
	    double rangerr;	/* range error */
	    double rangerate;	/* range error rate */
	} ranges[MAXCORRECTIONS];
    };

    /* this is the decoding context */
    bool            locked;
    int             curr_offset;
    RTCMWORD        curr_word;
    RTCMWORD        buf[RTCM_WORDS_MAX];
    unsigned int    bufindex;
};

enum rtcmstat_t {
    RTCM_NO_SYNC, RTCM_SYNC, RTCM_STRUCTURE,
};

#define RTCM_ERRLEVEL_BASE	5

extern void rtcm_init(/*@out@*/struct rtcm_t *);
extern enum rtcmstat_t rtcm_decode(struct rtcm_t *, unsigned int);
extern void rtcm_dump(struct rtcm_t *, /*@out@*/char[], size_t);

/* end */
