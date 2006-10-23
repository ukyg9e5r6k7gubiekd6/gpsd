/* $Id$ */

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
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "gpsd.h"
#include "italk.h"

#define READLEN 128

struct portconf {
	int cur_speed, cur_proto;
	int new_speed, new_proto;
};

#pragma pack(1)
struct resetmsg {
	char h1, h2;
	char src, dst, mid, txn, len;
	unsigned short cmd;
	unsigned short arg1;
	unsigned long  dummy;
	unsigned short cksum;
	char end;
} __attribute__((packed));

void itrax_reset(int);
void italk_add_checksum(char *, size_t);
void itrax_protocol_droid(int, struct termios *, struct portconf *);
int itrax_probe(int, struct termios *, int *, int *);
int serialConfig(int , struct termios *, int );
int serialSpeed(int , struct termios *, int );
void usage(void);

void
itrax_reset(int fd){
	int i, n;
	char buf[BUFSIZ];
	char resetstr[18] = {	0x3c, 0x21, /* HEADER */
				0x7f, /* src: NODE_HOST | TASK_HOST */
				0x20, /* dst: NODE_ITRAX | TASK_SYSTEM */
				0x70, /* mid: ITALK_MEMCTRL */
				0x00, /* txn: none */
				0x03, /* length - 1 */
				0x00, 0x04, /* MEM_BOOT */
				0x00, 0x00, /* MEM_BOOT_NORMAL */
				0x00, 0x00, 0x00, 0x00, /* DUMMY */
				0x17, 0x00, /* CHECKSUM */
				0x3e /* TRAILER */ };
	struct resetmsg rm = {
		.h1 = '<',
		.h2 = '!',
		.src = NODE_HOST | TASK_HOST,
		.dst = NODE_ITRAX | TASK_SYSTEM,
		.mid = ITALK_MEMCTRL,
		.txn = 0,
		.len = 3, /* (2 + 2 + 4)/2 - 1 */
		.cmd = MEM_BOOT,
		.arg1 = MEM_BOOT_NORMAL,
		.dummy = 0,
		.cksum = 0,
		.end = '>'
	};

	memcpy(&resetstr, &rm, 18);
	italk_add_checksum((char *)&rm, sizeof(rm));
	write(fd, &rm, sizeof(rm));
	for (i = 0; i < 5; i++){
		n = write(fd, resetstr, sizeof(resetstr));
		tcdrain(fd);
		usleep(1000);
	}
	read(fd, buf, BUFSIZ);
	for(n = 0; n < BUFSIZ; n++){
		if (0 == n%16)
			printf("\n%04x   ", n);
		printf("%02x ", buf[n]&0xff);
	}
	printf("\n");
}

void
italk_add_checksum(char *buf, size_t len){
	volatile unsigned long tmp = 0;
	volatile unsigned short sum = 0 , w = 0, *sp;
	int k, n;
/*
 * XXX this checksum routine is silly. fix it.
 * ntohs and htons are my friends
 * probably <sys/endian.h> will be needed
 */

	n = buf[6];
	for (k = 0; k <= n; k++){
		sp = (unsigned short*)(buf+7+2*k);
		w = *sp;
//		w = htole16(*sp);
//		memcpy(&w, buf+7+2*k, 2);
		tmp = (sum + 1) * (w + k);
		sum ^= ((tmp >> 16) ^ tmp);
	}
	buf[len-3] = sum;
}

void
itrax_protocol_droid(int fd, struct termios *term, struct portconf *conf){
	int i;
	char buf[BUFSIZ];
	/*
	 * apparently iTalk does not have a protocol switch message;
	 * to get back to NMEA you need to reset the receiver. Foo!
	 */

	if (conf->cur_proto == PROTO_ITALK){
		int s, p;
		itrax_reset(fd); /* should put the receiver into nmea */
		sleep(1);
		itrax_probe(fd, term, &s, &p);
	}

	snprintf(buf, BUFSIZ, "$PFST,%s,,%u",
	    (conf->new_proto == PROTO_NMEA)? "NMEA" : "ITALK",
	    conf->new_speed);

	for(i = 0; i < 5; i++){
		tcflush(fd, TCIOFLUSH);
		nmea_send(fd, buf);
		usleep(10000);
	}
}

/*
 * itrax has this wonderful "ping" message whereby you send it "<?>" at speed
 * X and it immediately replies with "<?1>" for NMEA or "<?0>" for iTalk if
 * you got the speed right. As a side effect, if we return OK, the tty is
 * already set up correctly for further communication with the receiver.
 */
int
itrax_probe(int fd, struct termios *term, int *speed, int *proto) {
	int i, j, k, n;
	int speeds[9] =
		{4800, 9600, 14400, 28800, 38400, 57600, 115200, 230400, 0};
	char *probe = "\r\n<?>";
	char buf[READLEN];
	struct pollfd pfd[1];

	i = 0;
	while(speeds[i]){
		k = 0;
		serialConfig(fd, term, speeds[i]);
		*speed = speeds[i];
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
probe:		tcflush(fd, TCIOFLUSH);
		bzero(buf, READLEN);
		write(fd, probe, 5);
		tcdrain(fd);
		write(fd, probe, 5);
		usleep(1000);
		if ((n = poll(pfd, 1, -1)) > 0){
			n = read(fd, buf, READLEN);
			for (j = 0; j < READLEN-3; j++){
				if (strncmp(buf+j, "<?1>", 4) == 0){
					*proto = PROTO_NMEA;
					return 0;
				}
				if (strncmp(buf+j, "<?0>", 4) == 0){
					*proto = PROTO_ITALK;
					return 0;
				}
			}
			if (k < 1){
				k++;
				goto probe;
			}
			i++;
		}
	}
	*speed = *proto = -1;
	return -1;
}


int
serialConfig(int pfd, struct termios *term, int speed){
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
	while (((rv = tcsetattr(pfd, TCSADRAIN, term)) == -1) &&
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


int
serialSpeed(int pfd, struct termios *term, int speed){
	int rv;
	int r = 0;

	switch(speed){
#ifdef B230400
	case 230400:
		speed = B230400;
		break;
#endif
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
#ifdef B38400
	case 38400:
		speed = B38400;
		break;
#endif
#ifdef B28800
	case 28800:
		speed = B28800;
		break;
#endif
#ifdef B19200
	case 19200:
		speed = B19200;
		break;
#endif
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
	tcgetattr(pfd, term);
	cfsetispeed(term, speed);
	cfsetospeed(term, speed);
	while (((rv = tcsetattr(pfd, TCSADRAIN, term)) == -1) && \
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
	int fd, ch, s, p;
	struct termios term;
	struct portconf conf;
#ifdef HAVE_STRTONUM
	const char *e;
#endif /* HAVE_STRTONUM */

	conf.new_speed = conf.new_proto = -1;

	while ((ch = getopt(argc, argv, "?hVbns:")) != -1){
		switch (ch){
		case 'b':
			conf.new_proto = PROTO_ITALK;
			break;
		case 'n':
			conf.new_proto = PROTO_NMEA;
			break;
		case 's':
#if HAVE_STRTONUM
			conf.new_speed = strtonum(optarg, 4800, 230400, &e);
			if (e)
				err(1, "%s (%s)", e, optarg);
#else
			conf.new_speed = atoi(optarg);
			if (conf.new_speed < 4800 || conf.new_speed > 230400)
				err(1, "Illegal value %d\n", conf.new_speed);
#endif /* HAVE_STRTONUM */
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argv[0] == NULL || strlen(argv[0]) == 0){
		printf("Missing device name\n");
		usage();
	}

	switch(conf.new_speed){
	case -1:
	case 0:
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
#ifdef B230400
	case 230400:
#endif
		break;
	default:
		printf("bad speed %d\n", conf.new_speed);
		return 1;
	}
	
	if ((fd = open(argv[0], O_RDWR|O_NONBLOCK|O_EXCL, 0644)) == -1)
		err(1, "open(%s)", argv[0]);

	if (itrax_probe(fd, &term, &s, &p) == -1){
		printf("itrax receiver not found\n");
		goto quit;
	} else {
		printf("itrax receiver found: %s@%d\n",
		    (p ? "NMEA" : "iTalk"), s);
		conf.cur_proto = p;
		conf.cur_speed = s;
	}

	if (conf.new_proto == -1 && conf.new_speed == -1)
		goto quit;
	if (conf.new_speed == -1)
		conf.new_speed = conf.cur_speed;
	if (conf.new_proto == -1)
		conf.new_proto = conf.cur_proto;

	printf("switching to %s@%d\n",
	    (conf.new_proto ? "NMEA" : "iTalk"), conf.new_speed);

	if (conf.new_proto == PROTO_ITALK){
		if (conf.new_speed < 19200) {
			errx(1, "iTalk speed must not be < 19200\n");
		} else if (conf.new_speed < 115200)
			warn("iTalk speed should not be < 115200\n");
	}

	itrax_protocol_droid(fd, &term, &conf);

	if (itrax_probe(fd, &term, &s, &p) == -1){
		printf("itrax receiver not found\n");
	} else {
		printf("itrax receiver found: %s@%d\n",
		    (p ? "NMEA" : "iTalk"), s);
	}
	
quit:	close(fd);
	return 0;
}

void
usage(void){
	printf("Usage: itraxtool [-b|-n] [-s speed] <device>\n"
	    "SVN ID: $Id$\n");
	exit(1);
}

