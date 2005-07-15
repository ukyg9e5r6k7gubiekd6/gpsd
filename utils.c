/*
 * Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
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

#include "cskprog.h"

#include <netinet/in.h>	/* for htonl() under Linux */

int
serialSpeed(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	switch(speed){
	case 115200:
		speed = B115200;
		break;
	case 57600:
		speed = B57600;
		break;
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
		speed = B9600;
		break;
	default:
		errno = EINVAL;
		return -1;
		/* NOTREACHED */
		break;
	}

	/* set UART speed */
	tcgetattr(pfd, term);
	cfsetispeed(term, speed);
	cfsetospeed(term, speed);
	while ((rv = tcsetattr(pfd, TCSAFLUSH, term) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		usleep (1000);
		r++;
	}

	if(rv == -1)
		return -1;
	else
		return 0;
}


int
serialConfig(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;


	/* get the current terminal settings */
	tcgetattr(pfd, term);
	/* set the port into "raw" mode. */
	cfmakeraw(term);
	term->c_lflag &=~ (ICANON);
	/* Enable serial I/O, ignore modem lines */
	term->c_cflag |= (CLOCAL | CREAD);
	/* No output postprocessing */
	term->c_oflag &=~ (OPOST);
	/* 8 data bits */
	term->c_cflag |= CS8;
	term->c_iflag &=~ (ISTRIP);
	/* No parity */
	term->c_iflag &=~ (INPCK);
	term->c_cflag &=~ (PARENB | PARODD);
	/* 1 Stop bit */
	term->c_cflag &=~ (CSIZE | CSTOPB);
	/* No flow control */
	term->c_iflag &=~ (IXON | IXOFF);
#if defined(CCTS_OFLOW) && defined(CRTS_IFLOW) && defined(MDMBUF)
	term->c_oflag &=~ (CCTS_OFLOW | CRTS_IFLOW | MDMBUF);
#endif
#if defined(CRTSCTS)
	term->c_oflag &=~ (CRTSCTS);
#endif

	/* we'd like to read back at least 2 characters in .2sec */
	term->c_cc[VMIN] = 2;
	term->c_cc[VTIME] = 2;

	/* apply all the funky control settings */
	while ((rv = tcsetattr(pfd, TCSAFLUSH, term) == -1) && \
	    (errno == EINTR) && (r < 3)) {
		/* retry up to 3 times on EINTR */
		usleep (1000);
		r++;
	}

	if(rv == -1)
		return -1;
	
	/* and if that all worked, try change the UART speed */
	return serialSpeed(pfd, term, speed);
}

int
sirfSendUpdateCmd(int pfd){
	unsigned char msg[] =	{0xa0,0xa2,	/* header */
				0x00,0x01,	/* message length */
				0x94,		/* 0x94: firmware update */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */
	return (sirfWrite(pfd, msg));
}

int
sirfSendLoader(int pfd, struct termios *term, char *loader, int ls){
	unsigned int x;
	int speed = 115200;
	unsigned char boost[] = {'S', BOOST_115200};
	unsigned char *msg;
	int r, nbr, nbs, nbx;

	if((msg = malloc(ls+10)) == NULL){
		return -1; /* oops. bail out */
	}

	x = htonl((unsigned int)ls);
	msg[0] = 'S';
	msg[1] = (unsigned char)0;
	memcpy(msg+2, &x, 4); /* length */
	memcpy(msg+6, loader, ls); /* loader */
	memset(msg+6+ls, 0, 4); /* reset vector */
	
	/* send the command to jack up the speed */
	if((r = write(pfd, boost, 2)) != 2)
		return -1; /* oops. bail out */

	/* wait for the serial speed change to take effect */
	tcdrain(pfd);
	usleep(1000);

	/* now set up the serial port at this higher speed */
	serialSpeed(pfd, term, speed);

	/* send the real loader */
	nbr = ls+10; nbs = WRBLK ; nbx = 0;
	while(nbr){
		if(nbr > WRBLK )
			nbs = WRBLK ;
		else
			nbs = nbr;

r0:		if((r = write(pfd, msg+nbx, nbs)) == -1){
			if (errno == EAGAIN){ /* retry */
				tcdrain(pfd); /* wait a moment */
				errno = 0; /* clear errno */
				nbr -= r; /* number bytes remaining */
				nbx += r; /* number bytes sent */
				goto r0;
			} else {
				return -1; /* oops. bail out */
			}
		}
		nbr -= r;
		nbx += r;
	}
	return 0;
}

int
sirfSendFirmware(int pfd, char *fw, int len){
	int l, r, i;
	char *sendbuf, recvbuf[8];

	/* srecord loading is interactive. send line, get reply */
	/* when sending S-records, check for SA/S5 or SE */

	if((sendbuf = malloc(85)) == NULL)
		err(1, NULL);

	bzero(recvbuf,8);
	i = 0;
	while(strlen(fw)){
		/* grab a line of firmware, ignore line endings */
		if ((r = strlen(fw))){
			bzero(sendbuf,85);
			if((r = sscanf(fw, "%80s", sendbuf)) == EOF)
				return 0;

			l = strlen(sendbuf);
			if ((l < 1) || (l > 80))
				return -1;

			fw += l;
			len -= l;

			while((fw[0] != 'S') && (fw[0] != '\0'))
				fw++;

			sendbuf[l] = '\r';
			sendbuf[l+1] = '\n';
			l += 2;

			if ((++i % 1000) == 0)
				printf ("%6d\n", i);

			tcflush(pfd, TCIFLUSH);
			if((r = write(pfd, sendbuf, l+2)) != l+2)
				return -1; /* oops. bail out */

			tcdrain(pfd);
			if((r = read(pfd, recvbuf, 7)) == -1)
				return -1; /* oops. bail out */

			if (!((recvbuf[0] == 'S') && ((recvbuf[1] == 'A') || (recvbuf[1] == '5'))))
				return -1; /* oops. bail out */
		}
	}
	return 0;
}

int
sirfSetProto(int pfd, struct termios *term, int speed, int proto){
	int l, r, i;
	int spd[8] = {115200, 57600, 38400, 28800, 19200, 14400, 9600, 4800};
	unsigned char *nmea, *tmp;
	unsigned char sirf[] =	{0xa0,0xa2,	/* header */
				0x00,0x01,	/* message length */
				0xa5,		/* message 0xa5: UART config */
				0x00,0,0, 0,0,0,0, 8,1,0, 0,0, /* port 0 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 1 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 2 */
				0xff,0,0, 0,0,0,0, 0,0,0, 0,0, /* port 3 */
				0x00,0x00,	/* checksum */
				0xb0,0xb3};	/* trailer */


	if((nmea = malloc(32)) == NULL)
		return -1;

	if((tmp = malloc(32)) == NULL)
		return -1;

	bzero(nmea, 32);
	bzero(tmp, 32);
	snprintf((char *)tmp, 31,"PSRF100,%u,%u,8,1,0", speed, proto);
	snprintf((char *)nmea, 31,"%s%s*%02x", "$", tmp, nmea_checksum(tmp));
	l = strlen((char *)nmea);

	sirf[7] = sirf[6] = (char)proto;
	i = htonl(speed); /* borrow "i" to put speed into proper byte order */
	bcopy(&i, sirf+8, 4);

	/* send at whatever baud we're currently using */
	sirfWrite(pfd, sirf);
	if ((r = write(pfd, nmea, l)) != r)
		return -1;
	
	tcdrain(pfd);

	/* now spam the receiver with the config messages */
	for(i = 0; i < 8; i++){
		serialSpeed(pfd, term, spd[i]);
		sirfWrite(pfd, sirf);
		write(pfd, nmea, l);
		tcdrain(pfd);
		usleep(100000);
	}

	serialSpeed(pfd, term, speed);
	tcflush(pfd, TCIOFLUSH);

	return 0;
}

unsigned char
nmea_checksum(unsigned char *s){
	unsigned char c, r = 0;
	while ((c = *s++))
		r += c;

	return r;
}

/* ************************************************************************ */
/* This stuff has mostly been stolen from gpsd's sirf.c                     */
/* Not sure who wrote this function, but these guys in the credits...       */
/*     Remco Treffkorn <remco@rvt.com>                                      */
/*     Derrick J. Brashear <shadow@dementia.org>                            */
/*     Russ Nelson <nelson@crynwyr.com>                                     */
/*     Eric S. Raymond <esr@thyrsus.com>                                    */
/* ************************************************************************ */

int
sirfWrite(int fd, unsigned char *msg) {
	unsigned int crc;
	size_t i, len;

	len = (size_t)((msg[2] << 8) | msg[3]);

	/* calculate CRC */
	crc = 0;
	for (i = 0; i < len; i++)
	crc += (int)msg[4 + i];
	crc &= 0x7fff;

	/* enter CRC after payload */
	msg[len + 4] = (unsigned char)((crc & 0xff00) >> 8);
	msg[len + 5] = (unsigned char)( crc & 0x00ff);

	errno = 0;
	if (write(fd, msg, len+8) != (len+8))
		return -1;

	(void)tcdrain(fd);
	return 0;
}
