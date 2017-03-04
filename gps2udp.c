/*
 * gps2udp
 *
 * Dump NMEA to UDP socket for AIShub
 *      gps2udp -u data.aishub.net:1234
 *
 * Author: Fulup Ar Foll (directly inspired from gpspipe.c)
 * Date:   2013-03-01
 *
 * This file is Copyright (c) 2013 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */

/* strsep() needs _DEFAULT_SOURCE */
#define _DEFAULT_SOURCE

#include <time.h>
#include "gpsd_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <termios.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "gpsd.h"
#include "gpsdclient.h"
#include "revision.h"
#include "strfuncs.h"

#define MAX_TIME_LEN 80
#define MAX_GPSD_RETRY 10

static struct gps_data_t gpsdata;

/* UDP socket variables */
#define MAX_UDP_DEST 5
static struct sockaddr_in remote[MAX_UDP_DEST];
static int sock[MAX_UDP_DEST];
static int udpchannel;

/* gpsclient source */
static struct fixsource_t gpsd_source;
static unsigned int flags;
static int debug = 0;
static bool aisonly = false;

static char* time2string(void)
/* return local time hh:mm:ss */
{
   static char buffer[MAX_TIME_LEN];
   time_t curtime;
   struct tm *loctime;

   /* Get the current time. */
   curtime = time (NULL);

   /* Convert it to local time representation. */
   loctime = localtime (&curtime);

   /* Print it out in a nice format. */
   (void)strftime (buffer, sizeof(buffer), "%H:%M:%S", loctime);

   return (buffer);
}

static int send_udp (char *nmeastring, size_t ind)
{
    char message [255];
    char *buffer;
    int  channel;

    /* if string length is unknow make a copy and compute it */
    if (ind == 0) {
	/* compute message size and add 0x0a 0x0d */
	for (ind=0; nmeastring [ind] != '\0'; ind ++) {
	    if (ind >= sizeof(message) - 3) {
		(void)fprintf(stderr, "gps2udp: too big [%s] \n", nmeastring);
		return -1;
	    }
	    message[ind] = nmeastring[ind];
	}
	buffer = message;
    } else {
	/* use directly nmeastring but change terminition */
	buffer = nmeastring;
	ind = ind-1;
    }
    /* Add termination to NMEA feed for AISHUB */
    buffer[ind] = '\r'; ind++;
    buffer[ind] = '\n'; ind++;
    buffer[ind] = '\0';

    if ((flags & WATCH_JSON)==0 && buffer[0] == '{') {
	/* do not send JSON when not configured to do so */
	return 0;
    }

    /* send message on udp channel */
    for (channel=0; channel < udpchannel; channel ++) {
	ssize_t status = sendto(sock[channel],
				buffer,
				ind,
				0,
				(const struct sockaddr *)&remote[channel],
				(int)sizeof(remote));
	if (status < (ssize_t)ind) {
	    (void)fprintf(stderr, "gps2udp: failed to send [%s] \n", nmeastring);
	    return -1;
	}
    }
    return 0;
}


static int open_udp(char **hostport)
/* Open and bind udp socket to host */
{
   int channel;

   for (channel=0; channel <udpchannel; channel ++)
   {
       char *hostname = NULL;
       char *portname = NULL;
       char *endptr = NULL;
       int  portnum;
       struct hostent *hp;

       /* parse argument */
       hostname = strsep(&hostport[channel], ":");
       portname = strsep(&hostport[channel], ":");
       if ((hostname == NULL) || (portname == NULL)) {
	   (void)fprintf(stderr, "gps2udp: syntax is [-u hostname:port]\n");
	   return (-1);
       }

       errno = 0;
       portnum = (int)strtol(portname, &endptr, 10);
       if (1 > portnum || 65535 < portnum || '\0' != *endptr || 0 != errno) {
	   (void)fprintf(stderr, "gps2udp: syntax is [-u hostname:port] [%s] is not a valid port number\n",portname);
	   return (-1);
       }

       sock[channel]= socket(AF_INET, SOCK_DGRAM, 0);
       if (sock[channel] < 0) {
	   (void)fprintf(stderr, "gps2udp: error creating UDP socket\n");
	   return (-1);
       }

       remote[channel].sin_family = (sa_family_t)AF_INET;
       hp = gethostbyname(hostname);
       if (hp==NULL) {
	   (void)fprintf(stderr,
	                 "gps2udp: syntax is [-u hostname:port] [%s]"
	                 " is not a valid hostname\n",
	                 hostname);
	   return (-1);
       }

       memcpy( &remote[channel].sin_addr, hp->h_addr, hp->h_length);
       remote[channel].sin_port = htons((in_port_t)portnum);
   }
return (0);
}

static void usage(void)
{
    (void)fprintf(stderr,
		  "Usage: gps2udp [OPTIONS] [server[:port[:device]]]\n\n"
		  "-h Show this help.\n"
                  "-u Send UDP NMEA/JASON feed to host:port [multiple -u host:port accepted\n"
		  "-n Feed NMEA.\n"
		  "-j Feed Jason.\n"
		  "-a Select !AISDM message only.\n"
		  "-c [count] exit after count packets.\n"
		  "-b Run in background as a daemon.\n"
		  "-d [0-2] 1 display sent packets, 2 ignored packets.\n"
		  "-v Print version and exit.\n\n"
                  "example: gps2udp -a -n -c 2 -d 1 -u data.aishub.net:2222 fridu.net\n"
		  );
}

static void connect2gpsd(bool restart)
/* loop until we connect with gpsd */
{
    unsigned int delay;

    if (restart) {
	(void)gps_close(&gpsdata);
	if (debug > 0)
	    (void)fprintf(stdout,
			  "gps2udp [%s] reset gpsd connection\n",
			  time2string());
    }

    /* loop until we reach GPSd */
    for (delay = 10; ; delay = delay*2) {
        int status = gps_open(gpsd_source.server, gpsd_source.port, &gpsdata);
        if (status != 0) {
	    (void)fprintf(stderr,
			  "gps2udp [%s] connection failed at %s:%s\n",
			  time2string(), gpsd_source.server, gpsd_source.port);
           (void)sleep(delay);
        } else {
	    if (debug > 0)
		(void)fprintf(stdout, "gps2udp [%s] connect to gpsd %s:%s\n",
			      time2string(), gpsd_source.server, gpsd_source.port);
	    break;
        }
    }
    /* select the right set of gps data */
    (void)gps_stream(&gpsdata, flags, gpsd_source.device);

}

static ssize_t read_gpsd(char *message, size_t len)
/* get data from gpsd */
{
    struct timeval tv;
    fd_set fds,master;
    int ind;
    char c;
    int retry=0;

    // prepare select structure */
    FD_ZERO(&master);
    FD_SET(gpsdata.gps_fd, &master);

    /* allow room for trailing NUL */
    len--;

    /* loop until we get some data or an error */
    for (ind = 0; ind < (int)len;) {
	int result;
        /* prepare for a blocking read with a 10s timeout */
        tv.tv_sec =  10;
        tv.tv_usec = 0;
        fds = master;
        result = select(gpsdata.gps_fd+1, &fds, NULL, NULL, &tv);

        switch (result)
	{
        case 1: /* we have data waiting, let's process them */
	    result = (int)read(gpsdata.gps_fd, &c, 1);

	    /* If we lost gpsd connection reset it */
	    if (result != 1) {
		connect2gpsd (true);
	    }

	    if ((c == '\n') || (c == '\r')){
		message[ind]='\0';

		if (ind > 0) {
		    if (retry > 0) {
			if (debug ==1)
			    (void)fprintf (stdout,"\r");
			if (debug > 1)
			    (void)fprintf(stdout,
					  " [%s] No Data for: %ds\n",
					  time2string(), retry*10);
		    }

		    if (aisonly && message[0] != '!') {
			if (debug >1)
			    (void)fprintf(stdout,
					  ".... [%s %d] %s\n", time2string(),
					  ind, message);
			return(0);
		    }
		}

		return ((ssize_t)ind+1);
	    } else {
		message[ind]= c;
		ind++;
	    }
	    break;

        case 0:	/* no data fail in timeout */
	    retry++;
	    /* if too many empty packets are received reset gpsd connection */
	    if (retry > MAX_GPSD_RETRY)
	    {
		connect2gpsd(true);
		retry = 0;
	    }
	    if (debug > 0)
		ignore_return(write (1, ".", 1));
	    break;

        default:	/* we lost connection with gpsd */
	    connect2gpsd(true);
	    break;
        }
    }
    message[ind] = '\0';
    (void)fprintf (stderr,"\n gps2udp: message too big [%s]\n", message);
    return(-1);
}

static unsigned char AISto6bit(unsigned char c)
/* 6 bits decoding of AIS payload */
{
    unsigned char cp = c;

    if(c < (unsigned char)0x30)
        return (unsigned char)-1;
    if(c > (unsigned char)0x77)
        return (unsigned char)-1;
    if(((unsigned char)0x57 < c) && (c < (unsigned char)0x60))
        return (unsigned char)-1;

    cp += (unsigned char)0x28;

    if(cp > (unsigned char)0x80)
        cp += (unsigned char)0x20;
    else
        cp += (unsigned char)0x28;
    return (unsigned char)(cp & (unsigned char)0x3f);
}

static unsigned int AISGetInt(unsigned char *bitbytes, unsigned int sp, unsigned int len)
/* get MMSI from AIS bit string */
{
    unsigned int acc = 0;
    unsigned int s0p = sp-1;                          // to zero base
    unsigned int i;

    for(i=0 ; i<len ; i++)
    {
	unsigned int cp, cx, c0;
        acc  = acc << 1;
        cp = (s0p + i) / 6;
        cx = (unsigned int)bitbytes[cp];      // what if cp >= byte_length?
        c0 = (cx >> (5 - ((s0p + i) % 6))) & 1;
        acc |= c0;
    }

    return acc;
}

int main(int argc, char **argv)
{
    bool daemonize = false;
    long count = -1;
    int option;
    char *udphostport[MAX_UDP_DEST];

    flags = WATCH_ENABLE;
    while ((option = getopt(argc, argv, "?habnjvc:l:u:d:")) != -1)
    {
	switch (option) {
	case 'd':
            debug = atoi(optarg);
            if ((debug <1) || (debug > 2)) {
                usage();
	        exit(1);
            }
	    break;
	case 'n':
            if (debug >0)
		(void)fprintf(stdout, "NMEA selected\n");
	    flags |= WATCH_NMEA;
	    break;
	case 'j':
            if (debug >0)
		(void)fprintf(stdout, "JSON selected\n");
	    flags |= WATCH_JSON;
	    break;
	case 'a':
            aisonly = true;
	    break;
	case 'c':
	    count = atol(optarg);
	    break;
	case 'b':
	    daemonize = true;
	    break;
        case 'u':
            if (udpchannel >= MAX_UDP_DEST) {
		(void)fprintf(stderr,
			      "gps2udp: too many UDP destinations (max=%d)\n",
			      MAX_UDP_DEST);
            } else {
		udphostport[udpchannel++] = optarg;
            }
            break;
	case 'v':
	    (void)fprintf(stderr, "%s: %s (revision %s)\n",
			  argv[0], VERSION, REVISION);
	    exit(0);
	case '?':
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }

    /* Grok the server, port, and device. */
    if (optind < argc)
	gpsd_source_spec(argv[optind], &gpsd_source);
    else
	gpsd_source_spec(NULL, &gpsd_source);
    if (gpsd_source.device != NULL)
	flags |= WATCH_DEVICE;

    /* check before going background if we can connect to gpsd */
    connect2gpsd(false);

    /* Open UDP port */
    if (udpchannel > 0) {
        int status = open_udp(udphostport);
	if (status !=0) exit (1);
    }

    /* Daemonize if the user requested it. */
    if (daemonize) {
	if (os_daemon(0, 0) != 0) {
	    (void)fprintf(stderr,
			  "gps2udp: daemonization failed: %s\n",
			  strerror(errno));
        }
    }

    /* infinite loop to get data from gpsd and push them to aggregators */
    for (;;)
    {
	char buffer[512];
	ssize_t  len;

	len = read_gpsd(buffer, sizeof(buffer));

	/* ignore empty message */
	if (len > 3)
	{
	    if (debug > 0)
	    {
		(void)fprintf (stdout,"---> [%s] -- %s",time2string(),buffer);

		// Try to extract MMSI from AIS payload
		if (str_starts_with(buffer, "!AIVDM"))
		{
#define MAX_INFO 6
		    int  i,j;
		    unsigned char packet[512];
		    unsigned char *adrpkt = packet;
		    unsigned char *info[MAX_INFO];
		    unsigned int  mmsi;
		    unsigned char bitstrings [255];

		    // strtok break original string
		    (void)strlcpy((char *)packet, buffer, sizeof(packet));
		    for (j=0; j<MAX_INFO; j++) {
			info[j] = (unsigned char *)strsep((char **)&adrpkt, ",");
		    }

		    for(i=0 ; i < (int)strlen((char *)info[5]); i++)  {
			if (i > (int) sizeof (bitstrings)) break;
			bitstrings[i] = AISto6bit(info[5][i]);
		    }

		    mmsi = AISGetInt(bitstrings, 9, 30);
		    (void)fprintf(stdout," MMSI=%9u", mmsi);

		}
		(void)fprintf(stdout,"\n");
	    }

	    // send to all UDP destinations
	    if (udpchannel > 0)
		(void)send_udp(buffer, (size_t)len);

	    // if we count messages check it now
	    if (count >= 0) {
		if (count-- == 0) {
		    /* completed count */
		    (void)fprintf(stderr,
				  "gpsd2udp: normal exit after counted packets\n");
		    exit (0);
		}
	    }  // end count
        } // end len > 3
    } // end for (;;)

    // This is an infinite loop, should never be here
    (void)fprintf (stderr, "gpsd2udp ERROR abnormal exit\n");
    exit (-1);
}

/* end */
