#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#define WRLEN 256
void spinner(int);

int main( int argc, char **argv){
	struct stat sb;
	struct termios term;
	char *buf, tn[32];
	int ifd, ofd, sfd, t, l, speed;

	if (argc != 3 ){
		fprintf(stderr, "usage: binreplay <speed> <file>\n");
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

	if ((ifd = open(argv[2], O_RDONLY, 0444)) == -1)
		err(1, "open");

	if (fstat(ifd, &sb) == -1)
		err(1, "fstat");

	if ((buf = mmap(0, sb.st_size, PROT_READ, MAP_FILE, ifd, 0)) == MAP_FAILED)
		err(1, "mmap");

	cfmakeraw(&term);
	cfsetospeed(&term, speed);
	cfsetispeed(&term, speed);
	if (openpty(&ofd, &sfd, tn, &term, NULL) == -1)
		err(1, "openpty");

	tcsetattr(ofd, TCSANOW, &term);
	tcsetattr(sfd, TCSANOW, &term);

	chmod(tn, 0444);
	printf("configured %s for %dbps\n", tn, speed);
	t = 1000000 / (speed / 8);

	for(l = 0; l < sb.st_size; l += WRLEN ){
		write(ofd, buf+l, WRLEN );
		tcdrain(ofd);
//		tcdrain(sfd);
		tcflush(ofd, TCIFLUSH);
		tcflush(sfd, TCIFLUSH);
		spinner( l );
		usleep(t);
	}

	munmap(buf, sb.st_size);
	close(ifd);
	close(ofd);
	fprintf(stderr, "\010\010\010\010\010\010\010\010\010\010\010\010\n");
	return 0;
}

void spinner(int n){
	char *s = "|/-\\";

	if (n % (WRLEN * 4))
		return;

	n /= (WRLEN * 4);

	fprintf(stderr, "\010\010\010\010\010\010\010\010\010\010\010\010\010");
	fprintf(stderr, "%c %d", s[n%4], n);
	fflush(stderr);
}

