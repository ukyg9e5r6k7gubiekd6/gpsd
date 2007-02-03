/* $Id: sirfctl.c 3648 2006-10-25 19:37:52Z ckuethe $ */
/* $CSK: sirfproto.c,v 1.6 2006/10/13 17:48:54 ckuethe Exp $ */

/*
 * Copyright (c) 2006 Chris Kuethe <chris.kuethe@gmail.com>
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

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define PROTO_SIRF 0
#define PROTO_NMEA 1
#define MAX_PACKET_LENGTH 512

int sirfSetProto(int , struct termios *, unsigned int , unsigned int );
int sirf_write(int , unsigned char *);
void nmea_add_checksum(char *);
int nmea_send(int , const char *, ... );
int serialConfig(int , struct termios *, int );
int serialSpeed(int , struct termios *, int );

int
sirfSetProto(int pfd, struct termios *term, unsigned int speed, unsigned int proto){
	int i;
	int spd[8] = {115200, 57600, 38400, 28800, 19200, 14400, 9600, 4800};
	unsigned char sirf[] =	{
				0xa0,0xa2,	/* header */
				0x00,0x31,	/* message length */
				0xa5,		/* message 0xa5: UART config */
				0x00,0,0, 0,0,0,0, 8,1,0, 0,0, /* port 0 */
				0xff,5,5, 0,0,0,0, 0,0,0, 0,0, /* port 1 */
				0xff,5,5, 0,0,0,0, 0,0,0, 0,0, /* port 2 */
				0xff,5,5, 0,0,0,0, 0,0,0, 0,0, /* port 3 */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */

	if (serialConfig(pfd, term, 38400) == -1)
		return -1;

	sirf[7] = sirf[6] = (unsigned char)proto;
	i = htonl(speed); /* borrow "i" to put speed into proper byte order */
	bcopy(&i, sirf+8, 4);

	/* send at whatever baud we're currently using */
	sirf_write(pfd, sirf);
	tcdrain(pfd);
	usleep(200000);
	nmea_send(pfd, "$PSRF100,%u,%u,8,1,0", proto, speed);
	tcdrain(pfd);
	usleep(200000);

	/* now spam the receiver with the config messages */
	for(i = 0; i < (int)(sizeof(spd)/sizeof(spd[0])); i++) {
		serialSpeed(pfd, term, spd[i]);
		sirf_write(pfd, sirf);
		usleep(100000);
		printf("sirf/%d -> %d\n", spd[i], speed);

		nmea_send(pfd, "$PSRF100,%u,%u,8,1,0", proto, speed);
		printf("nmea/%d -> %d\n", spd[i], speed);
		tcdrain(pfd);
		usleep(200000);
	}

	serialSpeed(pfd, term, speed);
	tcdrain(pfd);
	tcflush(pfd, TCIFLUSH);

	return 0;
}

int sirf_write(int fd, unsigned char *msg) {
	unsigned int	crc;
	size_t	i, len;
	int	ok;

	len = (size_t)((msg[2] << 8) | msg[3]);

	/* calculate CRC */
	crc = 0;
	for (i = 0; i < len; i++)
	crc += (int)msg[4 + i];
	crc &= 0x7fff;

	/* enter CRC after payload */
	msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
	msg[len + 5] = (unsigned char)( crc & 0x00ff);

	tcflush(fd, TCIOFLUSH);
	ok = (write(fd, msg, len+8) == (ssize_t)(len+8));
	tcdrain(fd);
	return(ok);
}


void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
	unsigned char sum = '\0';
	char c, *p = sentence;

	if (*p == '$') {
	p++;
	}
	while ( ((c = *p) != '*') && (c != '\0')) {
	sum ^= c;
	p++;
	}
	*p++ = '*';
	snprintf(p, 5, "%02X\r\n", (unsigned)sum);
}

int nmea_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
	int status;
	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt) ;
	vsnprintf(buf, sizeof(buf)-5, fmt, ap);
	va_end(ap);
	if (fmt[0] == '$') {
		strlcat(buf, "*", BUFSIZ);
		nmea_add_checksum(buf);
	} else
		strlcat(buf, "\r\n", BUFSIZ);

	tcflush(fd, TCIOFLUSH);
	status = (int)write(fd, buf, strlen(buf));
	tcdrain(fd);
	if (status == (int)strlen(buf)) {
		return status;
	} else {
		return -1;
	}
}

int serialConfig(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	/* get the current terminal settings */
	tcgetattr(pfd, term);
	cfmakeraw(term);
	term->c_cflag |= (CLOCAL | CREAD);
	term->c_cflag &=~ (PARENB | CRTSCTS);
	term->c_iflag = term->c_oflag = term->c_lflag = (tcflag_t) 0;
	term->c_oflag = (ONLCR);

	/* we'd like to read back at least 2 characters in .2sec */
	term->c_cc[VMIN] = 2;
	term->c_cc[VTIME] = 2;

	/* apply all the funky control settings */
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		usleep(1000);
		r++;
	}

	if(rv == -1){
		printf("serialConfig failed: %s\n", strerror(errno));
		return -1;
	}
	
	/* and if that all worked, try change the UART speed */
	return serialSpeed(pfd, term, speed);
}


int serialSpeed(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	switch(speed){
#ifdef B115200
	case 115200:
		speed = B115200;
		break;
#endif
#ifdef B57600
	case 57600:
		speed = B57600;
		break;
#endif
	case 38400:
		speed = B38400;
		break;
#ifdef B28800
	case 28800:
		speed = B28800;
		break;
#endif
	case 19200:
		speed = B19200;
		break;
#ifdef B14400
	case 14400:
		speed = B14400;
		break;
#endif
	case 9600:
		speed = B9600;
		break;
	case 4800:
		speed = B4800;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	/* set UART speed */
	(int)tcgetattr(pfd, term);
	cfsetispeed(term, speed);
	cfsetospeed(term, speed);
	while (((rv = tcsetattr(pfd, TCSAFLUSH, term)) == -1) && \
		(errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		usleep(1000);
		r++;
	}

	if(rv == -1)
		return -1;
	else
		return 0;
}

int
main(int argc, char **argv){
	int fd, speed, proto;
	struct termios term;

	if (argc !=  4){
		printf("Usage: %s <tty> <nmea|sirf> <speed>\n", argv[0]);
		exit(1);
	}

	if (strcmp(argv[2], "nmea") == 0)
		proto = PROTO_NMEA;
	else if (strcmp(argv[2], "sirf") == 0)
		proto = PROTO_NMEA;
	else {
		printf("bad protocol '%s'. use 'nmea' or 'sirf'\n", argv[2]);
		exit(1);
	}

	speed = atoi(argv[3]);
	switch(speed){
	case 4800:
	case 9600:
#ifdef B14400
	case 14400:
#endif
#ifdef B28800
	case 28800:
#endif
#ifdef B38400
	case 38400:
#endif
#ifdef B57600
	case 57600:
#endif
#ifdef B115200
	case 115200:
#endif
		break;
	default:
		printf("bad speed %d\n", speed);
		exit(1);
	}
	
	if ((fd = open(argv[1], O_RDWR|O_NONBLOCK|O_EXCL, 0644)) == -1)
		err(1, "open(%s)", argv[1]);

	sirfSetProto(fd, &term, speed, proto);
	close(fd);
	return 0;
}
