#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>

#if defined (HAVE_SYS_TERMIOS_H)
#include <sys/termios.h>
#else
#if defined (HAVE_TERMIOS_H)
#include <termios.h>
#endif
#endif

#if defined (HAVE_TERMIO_H)
#include <termio.h>
#define USE_TERMIO 1
#define TIOCGETA	TCGETA
#define TIOCSETAF	TCSETAF

#ifndef ONLCR
#define ONLCR		ONLRET
#endif

#define termios		termio
#define tcflag_t	ushort
#endif

#include "gpsd.h"

#define DEFAULTPORT "2947"

extern int debug;
extern char *device_name;
extern int device_speed;


/* define global variables */
static int ttyfd = -1;
static struct termios ttyset, ttyset_old;

int serial_open()
{
    char *temp;
    char *p;

    temp = malloc(strlen(device_name) + 1);
    strcpy(temp, device_name);

    if ( (p = strchr(temp, ':')) ) {
	char *port = DEFAULTPORT;
	int one = 1;

	if (*(p + 1))
	    port = p + 1;
	*p = '\0';

	/* temp now holds the HOSTNAME portion and port the port number. */
	if (debug > 5)
	    fprintf(stderr, "Host: %s  Port: %s\n", temp, port);
	ttyfd = connectTCP(temp, port);
	free(temp);
	port = 0;

	setsockopt(ttyfd, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one));

	if (write(ttyfd, "r\n", 2) != 2)
	    errexit("Can't write to socket");
    } else {
	ttyfd = open(temp, O_RDWR | O_NONBLOCK);
	free(temp);

	if (ttyfd < 0)
	    return (-1);

	if (isatty(ttyfd)) {

            /* Save original terminal parameters */
            if (tcgetattr(ttyfd,&ttyset_old) != 0)
              return (-1);

	    if (ioctl(ttyfd, TIOCGETA, &ttyset) < 0)
		return (-1);

#if defined (USE_TERMIO)
	    ttyset.c_cflag = CBAUD & device_speed;
#else
	    ttyset.c_ispeed = device_speed;
	    ttyset.c_ospeed = device_speed;
#endif
	    ttyset.c_cflag &= ~(PARENB | CRTSCTS);
	    ttyset.c_cflag |= (CSIZE & CS8) | CREAD | CLOCAL;
	    ttyset.c_iflag = ttyset.c_oflag = ttyset.c_lflag = (tcflag_t) 0;
	    ttyset.c_oflag = (ONLCR);
	    if (ioctl(ttyfd, TIOCSETAF, &ttyset) < 0)
		return (-1);
	}
    }
    return ttyfd;
}

void serial_close()
{
    if (ttyfd != -1) {
	if (isatty(ttyfd)) {
#if defined (USE_TERMIO)
	    ttyset.c_cflag = CBAUD & B0;
#else
	    ttyset.c_ispeed = B0;
	    ttyset.c_ospeed = B0;
#endif
	    ioctl(ttyfd, TIOCSETAF, &ttyset);
	}
	/* Restore original terminal parameters */
        tcsetattr(ttyfd,TCSANOW,&ttyset_old);

	close(ttyfd);
	ttyfd = -1;
    }
}
