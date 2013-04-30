/*
 * gps2udp
 *
 * Dump NMEA to UDP socket for AIShub 
 *      gps2udp -u data.aishub.net:1234
 *
 * Author Fulup Ar Foll (directly inspired from gpspipe.c from gpsd official distrib)
 * Date   01-march-2013
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 *
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/time.h>

#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */

#include "gpsd.h"
#include "gpsdclient.h"
#include "revision.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>


static struct gps_data_t gpsdata;

/* UDP socket variables */
#define MAX_UDP_DEST 5
static struct sockaddr_in remote[MAX_UDP_DEST];
static int sock[MAX_UDP_DEST];
static int udpchanel;

/* gpsclient source */
#define MAX_GPSD_RETRY 10
static struct fixsource_t gpsd_source;
static int flags;
static int debug=0;
static int aisonly=0;

// return local time hh:mm:ss
static char* time2string (void)  {
   #define MAX_TIME_LEN 80
   static char buffer[MAX_TIME_LEN];
   time_t curtime;
   struct tm *loctime;
     
   /* Get the current time. */
   curtime = time (NULL);
     
   /* Convert it to local time representation. */
   loctime = localtime (&curtime);
     
     
   /* Print it out in a nice format. */
   strftime (buffer, MAX_TIME_LEN, "%H:%M:%S", loctime);
     
return (buffer);
}

static int send_udp (char *nmeastring, int ind) {
   char message [255];
   char *buffer;
   int  status, chanel;

   /* if string lenght is unknow make a copy and compute it */
   if (ind == 0) {
     /* compute message size and add 0x0a 0x0d */
     for (ind=0; nmeastring [ind] != '\0'; ind ++) {
        if (ind > (int)sizeof (message)) {
          fprintf(stderr, "gps2udp: too big [%s] \n", nmeastring);
          return -1;
        }
        message[ind] = nmeastring[ind];
     }
     buffer = message;
   } else {
     /* use directly nmeastring but change terminition */
     buffer = nmeastring;
     ind=ind-1;
   }
   /* Add termination to NMEA feed for AISHUB */
   buffer[ind]='\n'; ind ++;
   buffer[ind]='\r'; ind ++;
   buffer[ind]='\0';

   /* send message on udp chanel */   
   for (chanel=0; chanel < udpchanel; chanel ++) {
     status=sendto(sock[chanel],buffer,ind,0,&remote[chanel],sizeof(remote));
     if (status < ind) {
       fprintf(stderr, "gps2udp: fail to send [%s] \n", nmeastring);
       return -1;
     }
   }
return 0;
}


static int open_udp(char **hostport)
/* Open and bind udp socket to host */
{
   struct hostent *hp;
   char *hostname = NULL;
   char *portname = NULL;
   int  portnum, chanel;

   for (chanel=0; chanel <udpchanel; chanel ++) {

   /* parse argement */
     hostname = strsep(&hostport[chanel], ":");
     portname = strsep(&hostport[chanel], ":");
     if ((hostname == NULL) || (portname == NULL)) {
       fprintf(stderr, "gps2udp: syntaxe is [-u hostname:port]\n");
       return (-1);
     }

     portnum= strtol (portname,NULL,0);
     if (errno != 0) {
       fprintf(stderr, "gps2udp: syntaxe is [-u hostname:port] [%s] is not a valid port number\n",portname);
       return (-1);
     }

     sock[chanel]= socket(AF_INET, SOCK_DGRAM, 0);
     if (sock[chanel] < 0) {
        fprintf(stderr, "gps2udp: error creating UDP socket\n");
        return (-1);
     }

     remote[chanel].sin_family = AF_INET;
     hp = gethostbyname(hostname);
     if (hp==NULL) {
       fprintf(stderr, "gps2udp: syntaxe is [-u hostname:port] [%s] is not a valid hostnamer\n",hostname);
       return (-1);
     }

     bcopy((char *)hp->h_addr, (char *)&remote[chanel].sin_addr, hp->h_length);
     remote[chanel].sin_port = htons(portnum);
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
		  "You must specify one, or more, of -r, -R, or -w\n"
		  );
}

/* loop until we connect with GPSd */
static void connect2gpsd (int restart) {
    int delay,status;

    if (restart) {
     gps_close   (&gpsdata);
     if (debug > 0)  fprintf(stdout, "gps2udp [%s] reset gpsd connection\n", time2string());

    }

    /* loop until we reach GPSd */
    for (delay=10;;delay=delay*2) {
        status = gps_open(gpsd_source.server, gpsd_source.port, &gpsdata);
        if (status != 0) {
	   fprintf(stderr, "gps2udp [%s] connection failed at %s:%s\n",
	      time2string(), gpsd_source.server, gpsd_source.port);
           sleep (delay);
        } else {
          if (debug > 0)  fprintf(stdout, "gps2udp [%s] connect to gpsd %s:%s\n",
             time2string(), gpsd_source.server, gpsd_source.port);
          break;
        }
    }
    /* select the right set of gps data */
    gps_stream(&gpsdata, flags, gpsd_source.device);
    
}

/* get date from gpsd */
static int read_gpsd (char *message, int len) {
   
   struct timeval tv;
   fd_set fds,master;
   int result,ind;
   char c;
   int retry=0;

    // prepare select structure */
    FD_ZERO(&master);
    FD_SET(gpsdata.gps_fd, &master);


   /* loop until we get some data or an error */
   for (ind=0; ind<len;) {

        /* prepare for a blocking read with a 10s timeout */
        tv.tv_sec =  10;
        tv.tv_usec = 0;
        memcpy(&fds,&master,sizeof(fd_set));
        result = select(gpsdata.gps_fd+1, &fds, NULL, NULL, &tv);
      
        switch (result) {
        case 1: /* we have data waiting let's process them */

           result = (int)read(gpsdata.gps_fd, &c, 1);

           /* If we lost gpsd connection reset it */
           if (result != 1) {
              connect2gpsd (true);
           }
            
           if ((c == '\n') || (c == '\r')){
               message[ind]='\0';

               if (ind > 0) {
                 if (retry > 0) {
                     if (debug ==1) fprintf (stdout,"\r");
                     if (debug > 1) fprintf (stdout," [%s] No Data for: %ds\n",time2string(), retry*10);
                 }

                 if (aisonly && message[0] != '!') {
                    if (debug >1) fprintf (stdout,".... [%s %d] %s\n", time2string(), ind, message);
                    return (0);
                 }
               }
                     
               return (ind+1);
           } else {
              message[ind]= c;
              ind ++;
           }
           break;

        case 0: /* no data fail in timeout */
           retry ++;
           /* if too many empty packet are received reset gpsd connection */
           if (retry > MAX_GPSD_RETRY)  {
              connect2gpsd (true);
              retry=0;
           }
           if (debug > 0) (void)write (1,".",1);

           break;

        default:/* we lost connection with gpsd */
           connect2gpsd (true);
           break;
        }
   }
   message [ind]='\0';
   fprintf (stderr,"\n gps2udp: message to big [%s]\n", message);
   return (-1);
}

// 6 bits decoding of AIS payload
static unsigned char AISto6bit(char c) {
    if(c < 0x30)
        return (unsigned char)-1;
    if(c > 0x77)
        return (unsigned char)-1;
    if((0x57 < c) && (c < 0x60))
        return (unsigned char)-1;

    unsigned char cp = c;
    cp += 0x28;

    if(cp > 0x80)
        cp += 0x20;
    else
        cp += 0x28;
    return (unsigned char)(cp & 0x3f);
}

// get mmsi from ais bit string
static int AISGetInt(unsigned char* bitbytes, int sp, int len) {
    int acc = 0;
    int s0p = sp-1;                          // to zero base
    int cp, cx, c0, i, cs;

    for(i=0 ; i<len ; i++)
    {
        acc  = acc << 1;
        cp = (s0p + i) / 6;
        cx = (int)bitbytes[cp];              // what if cp >= byte_length?
        cs = 5 - ((s0p + i) % 6);
        c0 = (cx >> (5 - ((s0p + i) % 6))) & 1;
        acc |= c0;
    }

    return acc;

}


int main(int argc, char **argv) {
    bool daemonize = false;
    long count = -1;
    int option, status;
    char *udphostport[MAX_UDP_DEST];

    flags = WATCH_ENABLE;
    while ((option = getopt(argc, argv, "?habnjcvl:u:d:")) != -1) {

	switch (option) {
	case 'd':
            debug= strtol(optarg, 0, 0);
	    break;
	case 'n':
            if (debug >0) fprintf (stdout, "NMEA selected\n");
	    flags |= WATCH_NMEA;
	    break;
	case 'j':
            if (debug >0) fprintf (stdout, "JASON selected\n");
	    flags |= WATCH_JSON;
	    break;
	case 'a':
            aisonly=1;
	    break;
	case 'c':
	    count = strtol(optarg, 0, 0);
	    break;
	case 'b':
	    daemonize = true;
	    break;
        case 'u':
            if (udpchanel > MAX_UDP_DEST) {
               fprintf (stderr, "gps2udp: to many UDP destination (max=%d)\n",MAX_UDP_DEST);
            } else {
               udphostport [udpchanel]= optarg;
               udpchanel ++;
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
    if (optind < argc) gpsd_source_spec(argv[optind], &gpsd_source);
    else gpsd_source_spec(NULL, &gpsd_source);
    if (gpsd_source.device != NULL) flags |= WATCH_DEVICE;

    /* check before going background if we can connect to gpsd */
    connect2gpsd (false);

    /* Open UDP port */
    if (udpchanel > 0) {
        status = open_udp(udphostport);
	if (status !=0) exit (1);
    }

    /* Daemonize if the user requested it. */
    if (daemonize) {
	if (daemon(0, 0) != 0) {
	    fprintf(stderr, "gps2udp: demonization failed: %s\n", strerror(errno));
        }
    }

    /* Infinit loop to get date from GPSd and push them to AIShub */
    for (;;) {

       char buffer [512];
       int  len;

       len = read_gpsd (buffer, sizeof (buffer));
 
       /* ignore empty message */
       if (len > 3) {
         if (debug > 0) {
               fprintf (stdout,"---> [%s] -- %s",time2string(),buffer);

               // Try to extract MMSI from AIS payload
               if (strncmp (buffer,"!AIVDM",6) == 0) {
                  #define MAX_INFO 6
                  char packet [512];
                  char *adrpkt=packet;
                  char *info  [MAX_INFO];
                  int  i,j;
                  unsigned int  mmsi;
                  unsigned char bitstrings [255];

                  // strtok break original string
                  strncpy (packet,buffer,sizeof(packet));
                  for (j=0; j<MAX_INFO; j++) {
                     info [j] = strsep (&adrpkt, ",");
                  }

                  for(i=0 ; i<(int)strlen(info[5]); i++)  {
                      if (i > (int) sizeof (bitstrings)) break;
                      bitstrings[i] = AISto6bit(info[5][i]);
                  }

                 mmsi=AISGetInt (bitstrings, 9, 30); 
                 fprintf (stdout," MMSI=%9d", mmsi);

               } 
               fprintf (stdout,"\n");

         }

         // send to all UDP destination
         if (udpchanel > 0) send_udp (buffer, len);

         // if we count messages check it now
         if (count >= 0) {
   	    if (count-- == 0) {
	      /* completed count */
              fprintf (stderr, "gpsd2udp normal exit after [%d] packets\n",(int)count);
	      exit (0); 
	    }
         }  // end count  
        } // end len > 3
     } // end for (;;)
 
// This is an infinite loop, should never be here
fprintf (stderr, "gpsd2udp ERROR abnormal exit\n");
exit (-1);
}
