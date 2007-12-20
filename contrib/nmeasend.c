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

static int nmea_send(int , const char *, ... );
static void nmea_add_checksum(char *);

int main(int argc, char **argv) {
	int speed, l, fd;
	struct termios term;
	char buf[BUFSIZ];

	if (argc != 4){
		fprintf(stderr, "usage: nmeasend <speed> <port> nmea-body\n");
		return 1;
	}

	if ((l = strlen(argv[3])) > 90){
		fprintf(stderr, "oversized message\n");
		return 1;
	}

	speed = atoi(argv[1]);
	switch (speed) {
	case 230400:
	case 115200:
	case 57600:
	case 38400:
	case 28800:
	case 14400:
	case 9600:
	case 4800:
		break;
	default:
		fprintf(stderr, "invalid speed\n");
		return 1;
	}

	if ((fd = open(argv[2], O_RDWR | O_NONBLOCK | O_NOCTTY, 0644)) == -1)
		err(1, "open");

	tcgetattr(fd, &term);
	cfmakeraw(&term);
	cfsetospeed(&term, speed);
	cfsetispeed(&term, speed);
	if (tcsetattr(fd, TCSANOW | TCSAFLUSH, &term) == -1)
		err(1, "tcsetattr");

	tcflush(fd, TCIOFLUSH);
	nmea_send(fd, "$%s", argv[3]);
	tcdrain(fd);
	while (1){
		l = read(fd, buf, BUFSIZ);
		if (l > 0){
//			tcflush(fd, TCIFLUSH);
			printf("%s", buf); fflush(stdout);
		}
		usleep(1000);
		bzero(buf, BUFSIZ);
	}
	return 0;
}

static void nmea_add_checksum(char *sentence)
/* add NMEA checksum to a possibly  *-terminated sentence */
{
    unsigned char sum = '\0';
    char c, *p = sentence;

    if (*p == '$') {
	p++;
	while ( ((c = *p) != '*') && (c != '\0')) {
	    sum ^= c;
	    p++;
	}
	*p++ = '*';
	(void)snprintf(p, 5, "%02X\r\n", (unsigned int)sum);
    }
}

static int nmea_send(int fd, const char *fmt, ... )
/* ship a command to the GPS, adding * and correct checksum */
{
    size_t status;
    char buf[BUFSIZ];
    va_list ap;

    va_start(ap, fmt) ;
    (void)vsnprintf(buf, sizeof(buf)-5, fmt, ap);
    va_end(ap);
    (void)strlcat(buf, "*", BUFSIZ);
    nmea_add_checksum(buf);
    (void)fputs(buf, stderr);
    status = (size_t)write(fd, buf, strlen(buf));
    if (status == strlen(buf)) {
	return (int)status;
    } else {
	perror("nmea_send");
	return -1;
    }
}
