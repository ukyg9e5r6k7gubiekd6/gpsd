/* 
 * tool to configure Garmin Serial GPS
 *
 */

#define __USE_POSIX199309 1
#include <time.h> // for nanosleep()

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <getopt.h>
#include <ctype.h>

/* gross - globals */
static struct termios ttyset;
static unsigned int bps;
static int debug_level = 0;

#define NO_PACKET	0
#define GARMIN_PACKET	1
#define NMEA_PACKET	2

/* how many characters to look at when trying to find baud rate lock */
#define SNIFF_RETRIES	1200

void logit( int level, char *fmt, ...)  {
    va_list ap;

    if ( level > debug_level ) {
	return;
    }
    va_start(ap, fmt) ;
    (void)vprintf(fmt, ap);
    va_end(ap);
}


#ifndef HAVE_STRLCAT
/*	$OpenBSD: strlcat.c,v 1.13 2005/08/08 08:05:37 espie Exp $	*/

/*
 * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
/*@ -usedef -mustdefine @*/
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = (size_t)(d - dst);
	n = siz - dlen;

	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}
/*@ +usedef +mustdefine @*/
#endif /* HAVE_STRLCAT */


/*****************************************************************************
 *
 * Serial-line handling
 * stolen shamelessly from sirfmon.c
 *
 *****************************************************************************/

static unsigned int get_speed(struct termios* ttyctl)
{
    speed_t code = cfgetospeed(ttyctl);
    switch (code) {
    case B0:     return(0);
    case B300:   return(300);
    case B600:   return(300);
    case B1200:  return(1200);
    case B2400:  return(2400);
    case B4800:  return(4800);
    case B9600:  return(9600);
    case B19200: return(19200);
    case B38400: return(38400);
    case B57600: return(57600);
    default: return(115200);
    }
}

static int set_speed( int fd,unsigned int speed)
{
    unsigned int	rate, count, state;
    int st;
    unsigned char	c;

    (void)tcflush(fd, TCIOFLUSH);	/* toss stale data */

    if (speed != 0) {
	/*@ +ignoresigns @*/
	if (speed < 300)
	    rate = 0;
	else if (speed < 600)
	    rate =  B300;
	else if (speed < 1200)
	    rate =  B600;
	else if (speed < 2400)
	    rate =  B1200;
	else if (speed < 4800)
	    rate =  B2400;
	else if (speed < 9600)
	    rate =  B4800;
	else if (speed < 19200)
	    rate =  B9600;
	else if (speed < 38400)
	    rate =  B19200;
	else if (speed < 57600)
	    rate =  B38400;
	else
	    rate =  B57600;
	/*@ -ignoresigns @*/

	/*@ ignore @*/
	(void)cfsetispeed(&ttyset, (speed_t)rate);
	(void)cfsetospeed(&ttyset, (speed_t)rate);
	/*@ end @*/
    }
    ttyset.c_cflag &=~ CSIZE;
    /* Garmin always 8N1 */
    ttyset.c_cflag |= (CSIZE & CS8);
    if (tcsetattr(fd, TCSANOW, &ttyset) != 0) {
        (void)logit(0, "ERROR: can not set port speed\n");
	return NO_PACKET;
    }
    (void)tcflush(fd, TCIOFLUSH);

    (void)logit(1, "Hunting at speed %u, 8N1\n", get_speed(&ttyset));

    /* sniff for NMEA or GARMIN packet */
    /* GARMIN_BINARY: 0x10, 0x03, 0x10 */
    /* NMEA: \0d\0a$GP */
    /*   or: \0d\0a$PG */
    state = 0;
    for (count = 0; count < SNIFF_RETRIES; count++) {
	if ((st = (int)read(fd, &c, 1)) < 0)
	    return 0;
	else
	    count += st;

        if ( isprint( c ) ) {
		(void)logit(8, "State: %d, Got: %#02x/%c\n", state, c, c);
	} else {
		(void)logit(8, "State: %d, Got: %#02x\n", state, c);
	}
	switch ( state ) {
	case 0:
	    if (c == '\x10')
		state = 101;
	    else if (c == '\x0d')
		state = 201;
	    break;
	case 101:
	    if (c == '\x03')
		state = 102;
	    else if (c == '\x0d')
		state = 201;
	    else
		state = 0;
	    break;
	case 102:
	    if (c == '\x10')
		return GARMIN_PACKET;
	    else if (c == '\x0d')
		state = 201;
	    else
		state = 0;
	    break;
	case 201:
	    if (c == '\x10')
		state = 101;
	    else if (c == '\x0a')
		state = 202;
	    else
		state = 0;
	    break;
	case 202:
	    if (c == '\x10')
		state = 101;
	    else if (c == '$')
		state = 203;
	    else
		state = 0;
	    break;
	case 203:
	    if (c == '\x10')
		state = 101;
	    else if (c == 'G')
		state = 204;
	    else if (c == 'P')
		state = 204;
	    else
		state = 0;
	    break;
	case 204:
	    if (c == '\x10')
		state = 101;
	    else if (c == 'G')
		return NMEA_PACKET;
	    else if (c == 'P')
		return NMEA_PACKET;
	    else
		state = 0;
	    break;
        default:
	    /* huh? */
		state = 0;
	    break;
	}
    }
    
    return NO_PACKET;
}

static unsigned int *ip, rates[] = {0, 4800, 9600, 19200, 38400, 57600};

/* return speed found */
static unsigned int hunt_open(int fd, int *st)
{
    int speed;
    /*
     * Tip from Chris Kuethe: the FTDI chip used in the Trip-Nav
     * 200 (and possibly other USB GPSes) gets completely hosed
     * in the presence of flow control.  Thus, turn off CRTSCTS.
     */
    ttyset.c_cflag &= ~(PARENB | CRTSCTS);
    ttyset.c_cflag |= CREAD | CLOCAL;
    ttyset.c_iflag = ttyset.c_oflag = ttyset.c_lflag = (tcflag_t) 0;
    ttyset.c_oflag = (ONLCR);

    for (ip = rates; ip < rates + sizeof(rates)/sizeof(rates[0]); ip++) {
	    *st = set_speed(fd, *ip);
	    speed = get_speed(&ttyset);
	    if (*st  == GARMIN_PACKET) {
		logit( 0, "Got GARMIN Packet, 8N1 @ %d\n", speed);
	        return speed;
	    } else if (*st == NMEA_PACKET) {
		logit( 0, "Got NMEA Packet, 8N1 @ %d\n", speed);
	        return speed;
	    }
    }
    return 0;
}

static void serial_initialize(char *device, int *fd, int *st)
{
    if ( (*fd = open(device,O_RDWR)) < 0) {
	perror(device);
	exit(1);
    }

    /* Save original terminal parameters */
    if (tcgetattr(*fd, &ttyset) ) {
	logit(0, "ERROR: Can't get terminal params!\n");
	exit(1);
    }
    if ( (bps = hunt_open(*fd, st))==0) {
	logit(0, "Can't sync up with device!\n");
	exit(1);
    }
}

void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$') {
	p++;
    } else {
        logit(0, "ERROR: Bad NMEA sentence: '%s'\n", sentence);
    }
    while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
    }
    *p++ = '*';
    (void)snprintf(p, 5, "%02X\r\n", (unsigned)sum);
}

int nmea_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
    int status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    if (fmt[0] == '$') {
	(void)strlcat(buf, "*", BUFSIZ);
	nmea_add_checksum(buf);
    } else
	(void)strlcat(buf, "\r\n", BUFSIZ);
    status = (int)write(fd, buf, strlen(buf));
    if (status == (int)strlen(buf)) {
	logit(2, "=> GPS: %s\n", buf);
	return status;
    } else {
	logit(2, "=> GPS: %s FAILED\n", buf);
	return -1;
    }
}

static void usage(void) {
	fprintf(stderr, "Usage: garmintool [OPTIONS] {serial-port}\n\n"
	        "SVN ID: $Id:$ \n"
		"-?   show this help\n"
		"-h   show this help\n"
		"-n   Change to NMEA mode\n"
		"-b   Change to binary mode\n"
		"-D n Set debug level to n (9 max)\n"
	        "-V   print version and exit\n\n"
		);
}

int main( int argc, char **argv)
{
	struct timespec delay, rem;
	int fd;
	int st;    /* packet type detected */
	int status;
	char *buf;
	int option;
	int to_nmea = 0;
	int to_binary = 0;
	char *device = "";

	while ((option = getopt(argc, argv, "?hbnVD:")) != -1) {
		switch (option) {
		case 'D':
			debug_level = (int)strtol(optarg, 0, 0);
			break;
		case 'n':
			// go to NMEA mode
			to_nmea = 1;
			break;
		case 'b':
			// go to Binary mode
			to_binary = 1;
			break;
		case 'V':
	                (void)fprintf(stderr, "%s: SVN ID: $Id: $ \n", argv[0]);
			exit(0);
		case '?':
		case 'h':
		default:
			usage( );
			exit(1);
		}
	}
	/* get the device to open */
	if (optind < argc) {
		device = argv[optind];
	}
	if ( !device || !strlen(device) ) {
		logit(0, "ERROR: missing device name\n");
		usage();
		exit(1);
	}

	if ( to_nmea && to_binary ) {
		logit(0, "ERROR: you can not specify -n and -b!\n");
		usage();
		exit(1);
	}


	serial_initialize(device, &fd, &st);

	if ( st == NO_PACKET ) {
		logit(0, "ERROR: could not detect valid packets\n");
		exit(1);
	} else if ( to_nmea && st == NMEA_PACKET ) {
		logit(0, "GPS already in NMEA mode\n");
	} else if ( to_nmea ) {
		buf = "\x10\x0A\x02\x26\x00\xCE\x10\x03";
		status = (int)write(fd, buf, 8);
		if (status == 8 ) {
			logit(2, "=> GPS: turn off binary %02x %02x %02x... \n"
				, buf[0], buf[1], buf[2]);
		} else {
			logit(0, "=> GPS: FAILED\n");
			return 1;
		}
		// wait 33mS, essential!
		memset( &delay, 0, sizeof(delay));
		delay.tv_sec = 0;
		delay.tv_nsec = 33300000L;
		nanosleep(&delay, &rem);

		/* once a sec, no binary, no averaging, NMEA 2.3, WAAS */
		
		nmea_send(fd, "$PGRMC1,1,1");
		//nmea_send(fd, "$PGRMC1,1,1,1,,,,2,W,N");
		nmea_send(fd, "$PGRMI,,,,,,,R");
		// wait 333mS, essential!
		// then figure out the new speed.
		memset( &delay, 0, sizeof(delay));
		delay.tv_sec = 0;
		delay.tv_nsec = 333000000L;
		nanosleep(&delay, &rem);

		if ( (bps = hunt_open(fd, &st))==0) {
			logit(0, "Can't sync up with device!\n");
			exit(1);
		}
	} else if ( to_binary && st == GARMIN_PACKET ) {
		logit(0, "GPS already in GARMIN mode\n");
	} else if ( to_binary ) {
		nmea_send(fd, "$PGRMC1,1,2,1,,,,2,W,N");
		nmea_send(fd, "$PGRMI,,,,,,,R");
		// garmin serial binary is 9600 only!
		logit(0, "NOTE: Garmin binary is 9600 baud only!\n");
		// wait 333mS, essential!
		// then figure out the new speed.
		memset( &delay, 0, sizeof(delay));
		delay.tv_sec = 0;
		delay.tv_nsec = 333000000L;
		nanosleep(&delay, &rem);

		if ( (bps = hunt_open(fd, &st))==0) {
			logit(0, "Can't sync up with device!\n");
			exit(1);
		}
	} else {
		logit(0, "ERROR: Nothing to do!\n");
		usage();
		exit(1);
	}
	
#if 0
    /* once a sec, binary, no averaging, NMEA 2.3, WAAS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC1,1,2,1,,,,2,W,N");
    /* reset to get into binary */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMI,,,,,,,R");
#endif
#if 0
    /* first turn off garmin binary 
    (void)gpsd_write(session, "\x10\x0A\x02\x26\x00\xCE\x10\x03", 8); */
    /* once a sec, binary, no averaging, NMEA 2.3, WAAS */
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMC1,1,2,1,,,,2,W,N");
    (void)nmea_send(session->gpsdata.gps_fd, "$PGRMCE");
#endif
}
