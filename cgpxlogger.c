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

#include <sys/types.h>
#include <sys/cdefs.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BS 512

#define NUM 8
char *poll = "SPAMDQTV\n";
char *host = "127.0.0.1";
unsigned int want_exit = 0;
unsigned short port = 2947;
unsigned int sl = 5;

char *progname;

void process(char *);
void usage(void);
void dnserr(void);
void bye(int);
void process(char *);
void write_record(void);
void header(void);
void footer(void);
void track_start(void);
void track_end(void);
int tracking = 0;

struct {
	double latitude;
	double longitude;
	float altitude;
	float speed;
	float course;
	float hdop;
	short svs;
	char status;
	char mode;
	char time[32];
} gps_ctx;


int
main(int argc, char **argv){
	int ch, fd, l, rl;
	char *buf;
	struct in_addr addr;
	struct sockaddr_in sa;
	struct hostent *he;
	struct timeval tv;
	fd_set fds;

	progname = argv[0];
	while ((ch = getopt(argc, argv, "hVi:s:p:")) != -1){
	switch (ch) {
	case 'i':
		sl = (unsigned int)atoi(optarg);
		if (sl < 1)
			sl = 1;
		if (sl >= 3600)
			fprintf(stderr, "WARNING: polling interval is an hour or more!\n");
		break;
	case 's':
		host = optarg;
		break;
	case 'p':
		port = (unsigned short)atoi(optarg);
		break;
	case 'V':
		(void)fprintf(stderr, "SVN ID: $Id: cgpxlogger.c$ \n");
		exit(0);
	default:
		usage();
		/* NOTREACHED */
	}
	}

	argc -= optind;
	argv += optind;

	bzero((char *)&sa, sizeof(sa));
	if( inet_aton(host, &addr) ){
		bcopy(&addr, (char *)&sa.sin_addr, sizeof(addr));
	} else {
		he = gethostbyname(host);
		if (he != NULL){
			bcopy(he->h_addr_list[0], (char *)&sa.sin_addr, he->h_length);
		} else {
			dnserr();
			/* NOTREACHED */
		}
	}

	if ((buf = malloc( BS )) == NULL){
		perror(NULL);
		exit(1);
	}

	sa.sin_port= htons(port);
	sa.sin_family = AF_INET;

	if ((fd = socket(AF_INET,SOCK_STREAM,0)) == -1){
		perror(NULL);
		exit(1);
	}

	if (connect(fd,(struct sockaddr *)&sa,sizeof(sa)) == -1){
		perror(NULL);
		close(fd);
		exit(1);
	}

	l = strlen(poll);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	signal(SIGINT, bye);
	signal(SIGTERM, bye);
	signal(SIGQUIT, bye);
	signal(SIGHUP, bye);

	header();
	for(;;){
		if (want_exit){
			footer();
			fprintf(stderr, "Exiting on signal %d!\n", want_exit);
			fflush(NULL);
			shutdown(fd, SHUT_RDWR);
			close(fd);
			exit(0);
		}

		write(fd, poll, l);

		tv.tv_usec = 250000;
		tv.tv_sec = 0;
		select(fd + 1, &fds, NULL, NULL, &tv);

		bzero(buf, BS);
		if ((rl = read(fd, buf, BS - 1)) != -1){
			process(buf);
		} else {
			if ((errno != EINTR) && (errno != EAGAIN)){
				/* ignore EINTR and EAGAIN */
				want_exit = SIGPIPE;
				sl = 1;
				fprintf(stderr,"%s\n", strerror(errno));
			}
		}
		sleep(sl);
	}
}

void usage(){
	fprintf(stderr, "Usage: %s [-h] [-s server] [-p port] [-i interval]\n\t", progname);
	fprintf(stderr, "\tdefaults to '%s -s 127.0.0.1 -p 2947 -i 5'\n", progname);
	exit(1);
}

void dnserr(){
	herror(progname);
	exit(1);
}

void bye(int signum){ want_exit = signum; }

void process(char *buf){
	char *answers[NUM + 2], **ap;
	int i, j;
	char c;

	if (strncmp("GPSD,", buf, 5) != 0)
		return; /* lines should start with "GPSD," */

	/* nuke them pesky trailing CR & LF */
	i = strlen(buf);
	if((buf[i - 1] == '\r') || (buf[i - 1] == '\n'))
		buf[i - 1] = '\0';

	i = strlen(buf);
	if((buf[i - 1] == '\r') || (buf[i - 1] == '\n'))
		buf[i - 1] = '\0';

	/* tokenize the string at the commas */
 	for (ap = answers; ap < &answers[NUM+1] &&
 		(*ap = strsep(&buf, ",")) != NULL;) {
 		if (**ap != '\0')
 			ap++;
 	}
 	*ap = NULL;

	bzero( &gps_ctx, sizeof(gps_ctx));
	/* do stuff with each of the strings */
	for(i = 0; i < NUM+1 ; i++){
		c = answers[i][0];
		switch(c){
		case 'S':
			sscanf(answers[i], "S=%d", &j);
			gps_ctx.status = j;
			break;
		case 'P':
			sscanf(answers[i], "P=%lf %lf", &gps_ctx.latitude, &gps_ctx.longitude);
			break;
		case 'A':
			sscanf(answers[i], "A=%f", &gps_ctx.altitude);
			break;
		case 'M':
			sscanf(answers[i], "M=%d", &j);
			gps_ctx.mode = j;
			break;
		case 'Q':
			sscanf(answers[i], "Q=%hd %*s %f", &gps_ctx.svs, &gps_ctx.hdop );
			break;
		case 'T':
			sscanf(answers[i], "T=%f", &gps_ctx.course);
			break;
		case 'V':
			sscanf(answers[i], "V=%f", &gps_ctx.speed);
			break;
		case 'D':
			sscanf(answers[i], "D=%s", (char *)&gps_ctx.time);
			break;
		default: /* no-op */ ;
		}
	}
	if ((gps_ctx.mode > 1) && (gps_ctx.status > 0))
		write_record();
	else
		track_end();
}

void write_record(){
	track_start();
	printf("      <trkpt lat=\"%.6f\" ", gps_ctx.latitude );
	printf("lon=\"%.6f\">\n", gps_ctx.longitude );

	if ((gps_ctx.status >= 2) && (gps_ctx.mode >= 3)){ /* dgps or pps */
		if (gps_ctx.mode == 4) { /* military pps */
			printf("        <fix>pps</fix>\n");
		} else { /* civilian dgps or sbas */
			printf("        <fix>dgps</fix>\n");
		}
	} else { /* no dgps or pps */
		if (gps_ctx.mode == 3) {
			printf("        <fix>3d</fix>\n");
		} else if (gps_ctx.mode == 2) {
			printf("        <fix>2d</fix>\n");
		} else if (gps_ctx.mode == 1) {
			printf("        <fix>none</fix>\n");
		} /* don't print anything if no fix indicator */
	}

	/* print altitude if we have a fix and it's 3d of some sort */
	if ((gps_ctx.mode >= 3) && (gps_ctx.status >= 1))
		printf("        <ele>%.2f</ele>\n", gps_ctx.altitude);

	/* SiRF reports HDOP in 0.2 steps and the lowest I've seen is 0.6 */
	if (gps_ctx.svs >= 0.2)
		printf("        <hdop>%.1f</hdop>\n", gps_ctx.hdop);

	/* print # satellites used in fix, if reasonable to do so */
	if ((gps_ctx.svs > 0) && (gps_ctx.mode >= 2))
		printf("        <sat>%d</sat>\n", gps_ctx.svs);

	if (strlen(gps_ctx.time)) /* plausible timestamp */
		printf("        <time>%s</time>\n", gps_ctx.time);
	printf("      </trkpt>\n");
	fflush(stdout);
}

void header(){
	printf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	printf("<gpx version=\"1.1\" creator=\"GPX GPSD client\"\n");
	printf("        xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n");
	printf("        xmlns=\"http://www.topografix.com/GPX/1.1\"\n");
	printf("        xsi:schemaLocation=\"http://www.topografix.com/GPS/1/1\n");
	printf("        http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");
	printf("  <metadata>\n");
	printf("    <name>GPX GPSD client</name>\n");
	printf("    <author>Chris Kuethe (chris.kuethe@gmail.com)</author>\n");
	printf("    <copyright>2-clause BSD License</copyright>\n");
	printf("  </metadata>\n");
	printf("\n");
	printf("\n");
}

void footer(){
	track_end();
	printf("</gpx>\n");
}

void track_start(){
	if (tracking != 0)
		return;
	printf("<!-- track start -->\n  <trk>\n    <trkseg>\n");
	tracking = 1;
}


void track_end(){
	if (tracking == 0)
		return;
	printf("    </trkseg>\n  </trk>\n<!-- track end -->\n");
	tracking = 0;
}

