#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>

#include "gpsd.h"

static int verbose = 5;

void gpsd_report(int errlevel, const char *fmt, ... )
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	buf[0] = '\0';
	va_start(ap, fmt) ;
	(void)vsnprintf(buf + strlen(buf), sizeof(buf)-strlen(buf), fmt, ap);
	va_end(ap);

	(void)fputs(buf, stderr);
    }
}

void rtcm_print_msg(struct rtcm_msghdr *msghdr)
/* dump the contents of a parsed RTCM104 message */
{
    int             len = (int)msghdr->w2.frmlen;
    double          zcount = msghdr->w2.zcnt * ZCOUNT_SCALE;

    printf("H\t%u\t%u\t%0.1f\t%u\t%u\t%u\n",
	   msghdr->w1.msgtype,
	   msghdr->w1.refstaid,
	   zcount,
	   msghdr->w2.sqnum,
	   msghdr->w2.frmlen,
	   msghdr->w2.stathlth);
    switch (msghdr->w1.msgtype) {
    case 1:
    case 9:
	{
	    struct rtcm_msg1    *m = (struct rtcm_msg1 *) msghdr;

	    while (len >= 0) {
		if (len >= 2)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w3.satident1,
			   m->w3.udre1,
			   m->w4.issuedata1,
			   zcount,
			   m->w3.pc1 * (m->w3.scale1 ? PCLARGE : PCSMALL),
			   m->w4.rangerate1 * (m->w3.scale1 ?
					       RRLARGE : RRSMALL));
		if (len >= 4)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w4.satident2,
			   m->w4.udre2,
			   m->w6.issuedata2,
			   zcount,
			   m->w5.pc2 * (m->w4.scale2 ? PCLARGE : PCSMALL),
			   m->w5.rangerate2 * (m->w4.scale2 ?
					       RRLARGE : RRSMALL));
		
		/*@ -shiftimplementation @*/
		if (len >= 5)
		    printf("S\t%u\t%u\t%u\t%0.1f\t%0.3f\t%0.3f\n",
			   m->w6.satident3,
			   m->w6.udre3,
			   m->w7.issuedata3,
			   zcount,
			   ((m->w6.pc3_h << 8) | (m->w7.pc3_l)) *
			   (m->w6.scale3 ? PCLARGE : PCSMALL),
			   m->w7.rangerate3 * (m->w6.scale3 ?
					       RRLARGE : RRSMALL));
		/*@ +shiftimplementation @*/
		len -= 5;
		m = (struct rtcm_msg1 *) (((RTCMWORD *) m) + 5);
	    }
	}
	break;
    default:
	break;
    }
}

int main(int argc, char **argv)
{
    int             c;
    struct rtcm_ctx ctxbuf, *ctx = &ctxbuf;
    struct rtcm_msghdr *res;

    while ((c = getopt(argc, argv, "v:")) != EOF) {
	switch (c) {
	case 'v':		/* verbose */
	    verbose = 5 + atoi(optarg);
	    break;

	case '?':
	default:
	    /* usage(); */
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    rtcm_init(ctx);

    while ((c = getchar()) != EOF) {
	res = rtcm_decode(ctx, (unsigned int)c);
	if (res != RTCM_NO_SYNC && res != RTCM_SYNC)
	    rtcm_print_msg(res);
    }
    exit(0);
}

/* end */
