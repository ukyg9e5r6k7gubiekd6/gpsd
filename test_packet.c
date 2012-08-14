/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/types.h>
#include <fcntl.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"

static int verbose = 0;

void gpsd_report(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style, use stderr or syslog */
{
    if (errlevel <= verbose) {
	char buf[BUFSIZ];
	va_list ap;

	buf[0] = '\0';
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	(void)fputs(buf, stderr);
    }
}

struct map
{
    char *legend;
    char test[MAX_PACKET_LENGTH + 1];
    size_t testlen;
    int garbage_offset;
    int type;
};

/* *INDENT-OFF* */
/*@ -initallelements +charint -usedef @*/
static struct map singletests[] = {
    /* NMEA tests */
    {
	.legend = "NMEA packet with checksum (1)",
	.test = "$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	.testlen = 36,
	.garbage_offset = 0,
	NMEA_PACKET,
    },
    {
	.legend = "NMEA packet with checksum (2)",
	.test = "$GPGGA,110534.994,4002.1425,N,07531.2585,W,0,00,50.0,172.7,M,-33.8,M,0.0,0000*7A\r\n",
	.testlen = 82,
	.garbage_offset = 0,
	.type = NMEA_PACKET,
    },
    {
	.legend = "NMEA packet with checksum and 4 chars of leading garbage",
	.test = "\xff\xbf\x00\xbf$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n",
	.testlen = 40,
	.garbage_offset = 4,
	.type = NMEA_PACKET,
    },
    {
	.legend = "NMEA packet without checksum",
	.test = "$PSRF105,1\r\n",
	.testlen = 12,
	.garbage_offset = 0,
	.type = NMEA_PACKET,
    },
    {
	.legend = "NMEA packet with wrong checksum",
	.test = "$GPVTG,308.74,T,,M,0.00,N,0.0,K*28\r\n",
	.testlen = 36,
	.garbage_offset = 0,
	.type = BAD_PACKET,
    },
    /* SiRF tests */
    {
	.legend = "SiRF WAAS version ID",
	.test = {
	    0xA0, 0xA2, 0x00, 0x15,
	    0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44,
	    0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53,
	    0x4D, 0x00, 0x00, 0x00, 0x00,
	    0x03, 0x82, 0xB0, 0xB3},
	.testlen = 29,
	.garbage_offset = 0,
	.type = SIRF_PACKET,
    },
    {
	.legend = "SiRF WAAS version ID with 3 chars of leading garbage",
	.test = {
	    0xff, 0x00, 0xff,
	    0xA0, 0xA2, 0x00, 0x15,
	    0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44,
	    0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53,
	    0x4D, 0x00, 0x00, 0x00, 0x00,
	    0x03, 0x82, 0xB0, 0xB3},
	.testlen = 32,
	.garbage_offset = 3,
	.type = SIRF_PACKET,
    },
    {
	.legend = "SiRF WAAS version ID with wrong checksum",
	.test = {
	    0xA0, 0xA2, 0x00, 0x15,
	    0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44,
	    0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53,
	    0x4D, 0x00, 0x00, 0x00, 0x00,
	    0x03, 0x00, 0xB0, 0xB3},
	.testlen = 29,
	.garbage_offset = 0,
	.type = BAD_PACKET,
    },
    {
	.legend = "SiRF WAAS version ID with bad length",
	.test = {
	    0xA0, 0xA2, 0xff, 0x15,
	    0x06, 0x06, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x44,
	    0x4B, 0x49, 0x54, 0x31, 0x31, 0x39, 0x20, 0x53,
	    0x4D, 0x00, 0x00, 0x00, 0x00,
	    0x03, 0x82, 0xB0, 0xB3},
	.testlen = 29,
	.garbage_offset = 0,
	.type = BAD_PACKET,
    },
    /* Zodiac tests */
    {
	.legend = "Zodiac binary 1000 Geodetic Status Output Message",
	.test = {
	    0xff, 0x81, 0xe8, 0x03, 0x31, 0x00, 0x00, 0x00, 0xe8, 0x79,
	    0x74, 0x0e, 0x00, 0x00, 0x24, 0x00, 0x24, 0x00, 0x04, 0x00,
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x03, 0x23, 0x00,
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x06, 0x00,
	    0xcd, 0x07, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00, 0x7b, 0x0d,
	    0x00, 0x00, 0x12, 0x6b, 0xa7, 0x04, 0x41, 0x75, 0x32, 0xf8,
	    0x03, 0x1f, 0x00, 0x00, 0xe6, 0xf2, 0x00, 0x00, 0x00, 0x00,
	    0x00, 0x00, 0x11, 0xf6, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x40,
	    0xd9, 0x12, 0x90, 0xd0, 0x03, 0x00, 0x00, 0xa3, 0xe1, 0x11,
	    0x10, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa3, 0xe1, 0x11,
	    0x00, 0x00, 0x00, 0x00, 0xe0, 0x93, 0x04, 0x00, 0x04, 0xaa},
	.testlen = 110,
	.garbage_offset = 0,
	.type = ZODIAC_PACKET,
    },
    /* EverMore tests */
    {
	.legend = "EverMore status packet 0x20",
	.test = {
	    0x10, 0x02, 0x0D, 0x20, 0xE1, 0x00, 0x00, 0x00,
	    0x0A, 0x00, 0x1E, 0x00, 0x32, 0x00, 0x5b, 0x10,
	    0x03},
	.testlen = 17,
	.garbage_offset = 0,
	.type = EVERMORE_PACKET,
    },
    {
	.legend = "EverMore packet 0x04 with 0x10 0x10 sequence",
	.test = {
	    0x10, 0x02, 0x0f, 0x04, 0x00, 0x00, 0x10, 0x10,
	    0xa7, 0x13, 0x03, 0x2c, 0x26, 0x24, 0x0a, 0x17,
	    0x00, 0x68, 0x10, 0x03},
	.testlen = 20,
	.garbage_offset = 0,
	.type = EVERMORE_PACKET,
    },
    {
	.legend = "EverMore packet 0x04 with 0x10 0x10 sequence, some noise before packet data",
	 .test = {
	    0x10, 0x03, 0xff, 0x10, 0x02, 0x0f, 0x04, 0x00,
	    0x00, 0x10, 0x10, 0xa7, 0x13, 0x03, 0x2c, 0x26,
	    0x24, 0x0a, 0x17, 0x00, 0x68, 0x10, 0x03},
	.testlen = 23,
	.garbage_offset = 3,
	.type = EVERMORE_PACKET,
    },
    {
	.legend = "EverMore packet 0x04, 0x10 and some other data at the beginning",
	.test = {
	    0x10, 0x12, 0x10, 0x03, 0xff, 0x10, 0x02, 0x0f,
	    0x04, 0x00, 0x00, 0x10, 0x10, 0xa7, 0x13, 0x03,
	    0x2c, 0x26, 0x24, 0x0a, 0x17, 0x00, 0x68, 0x10,
	    0x03},
	.testlen = 25,
	.garbage_offset = 5,
	.type = EVERMORE_PACKET,
    },
    {
	.legend = "EverMore packet 0x04, 0x10 three times at the beginning",
	.test = {
	    0x10, 0x10, 0x10,
	    0x10, 0x02, 0x0f, 0x04, 0x00, 0x00, 0x10, 0x10,
	    0xa7, 0x13, 0x03, 0x2c, 0x26, 0x24, 0x0a, 0x17,
	    0x00, 0x68, 0x10, 0x03},
	.testlen = 23,
	.garbage_offset = 3,
	.type = EVERMORE_PACKET,
    },
    {
	/* from page 4-3 of RTCM 10403.1 */
	.legend = "RTCM104V3 type 1005 packet",
	/*
	 * Reference Station Id = 2003
	 * GPS Service supported, but not GLONASS or Galileo
	 * ARP ECEF-X = 1114104.5999 meters
	 * ARP ECEF-Y = -4850729.7108 meters
	 * ARP ECEF-Z = 3975521.4643 meters
	 */
	.test = {
	    0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3, 0x02, 0x02,
	    0x98, 0x0E, 0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62,
	    0xAC, 0x09, 0x41, 0x98, 0x6F, 0x33, 0x36, 0x0B,
	    0x98,
	    },
	.testlen = 25,
	.garbage_offset = 0,
	.type = RTCM3_PACKET,
    },
    {
	.legend = "RTCM104V3 type 1005 packet with 4th byte garbled",
	.test = {
	    0xD3, 0x00, 0x13, 0x3F, 0xD7, 0xD3, 0x02, 0x02,
	    0x98, 0x0E, 0xDE, 0xEF, 0x34, 0xB4, 0xBD, 0x62,
	    0xAC, 0x09, 0x41, 0x98, 0x6F, 0x33, 0x36, 0x0B,
	    0x98,
	    },
	.testlen = 25,
	.garbage_offset = 0,
	.type = BAD_PACKET,
    },
    {
	/* from page 3-71 of the RTCM 10403.1 */
	.legend = "RTCM104V3 type 1029 packet",
	.test = {
	    0xD3, 0x00, 0x27, 0x40, 0x50, 0x17, 0x00, 0x84,
	    0x73, 0x6E, 0x15, 0x1E, 0x55, 0x54, 0x46, 0x2D,
	    0x38, 0x20, 0xD0, 0xBF, 0xD1, 0x80, 0xD0, 0xBE,
	    0xD0, 0xB2, 0xD0, 0xB5, 0xD1, 0x80, 0xD0, 0xBA,
	    0xD0, 0xB0, 0x20, 0x77, 0xC3, 0xB6, 0x72, 0x74,
	    0x65, 0x72, 0xED, 0xA3, 0x3B
	    },
	.testlen = 45,
	.garbage_offset = 0,
	.type = RTCM3_PACKET,
    },
};
/*@ +initallelements -charint +usedef @*/
/* *INDENT-ON* */

/* *INDENT-OFF* */
/*@ -initallelements +charint -usedef @*/
static struct map runontests[] = {
    /* NMEA tests */
    {
	.legend = "Double NMEA packet with checksum",
	.test = "$GPVTG,308.74,T,,M,0.00,N,0.0,K*68\r\n$GPGGA,110534.994,4002.1425,N,07531.2585,W,0,00,50.0,172.7,M,-33.8,M,0.0,0000*7A\r\n",
	.testlen = 118,
	0,
	NMEA_PACKET,
    },
};
/*@ +initallelements -charint +usedef @*/
/* *INDENT-ON* */

static int packet_test(struct map *mp)
{
    struct gps_packet_t packet;
    int failure = 0;

    packet_init(&packet);
    packet.debug = verbose;
    /*@i@*/ memcpy(packet.inbufptr = packet.inbuffer, mp->test, mp->testlen);
    packet.inbuflen = mp->testlen;
    /*@ -compdef -uniondef -usedef -formatcode @*/
    packet_parse(&packet);
    if (packet.type != mp->type)
	printf("%2zi: %s test FAILED (packet type %d wrong).\n",
	       mp - singletests + 1, mp->legend, packet.type);
    else if (memcmp
	     (mp->test + mp->garbage_offset, packet.outbuffer,
	      packet.outbuflen)) {
	printf("%2zi: %s test FAILED (data garbled).\n", mp - singletests + 1,
	       mp->legend);
	++failure;
    } else
	printf("%2zi: %s test succeeded.\n", mp - singletests + 1,
	       mp->legend);    /*@ +compdef +uniondef +usedef +formatcode @*/

    return failure;
}

static void runon_test(struct map *mp)
{
    struct gps_packet_t packet;
    int nullfd = open("/dev/null", O_RDONLY);
    ssize_t st;

    packet_init(&packet);
    packet.debug = verbose;
    /*@i@*/ memcpy(packet.inbufptr = packet.inbuffer, mp->test, mp->testlen);
    packet.inbuflen = mp->testlen;
    /*@ -compdef -uniondef -usedef -formatcode @*/
    (void)fputs(mp->test, stdout);
    do {
	st = packet_get(nullfd, &packet);
	//printf("packet_parse() returned %zd\n", st);
    } while (st > 0);
    /*@ +compdef +uniondef +usedef +formatcode @*/
}

int main(int argc, char *argv[])
{
    struct map *mp;
    int failcount = 0;
    int option, singletest = 0;

    verbose = 0;
    while ((option = getopt(argc, argv, "e:t:v:")) != -1) {
	switch (option) {
	case 'e':
	    mp = singletests + atoi(optarg) - 1;
	    (void)fwrite(mp->test, mp->testlen, sizeof(char), stdout);
	    (void)fflush(stdout);
	    exit(EXIT_SUCCESS);
	case 't':
	    singletest = atoi(optarg);
	    break;
	case 'v':
	    verbose = atoi(optarg);
	    break;
	}
    }

    if (singletest)
	failcount += packet_test(singletests + singletest - 1);
    else {
	(void)fputs("=== Packet identification tests ===\n", stdout);
	for (mp = singletests;
	     mp < singletests + sizeof(singletests) / sizeof(singletests[0]);
	     mp++)
	    failcount += packet_test(mp);
	(void)fputs("=== EOF with buffer nonempty test ===\n", stdout);
	runon_test(&runontests[0]);
    }
    exit(failcount > 0 ? 1 : 0);
}
