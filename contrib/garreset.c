#include <sys/types.h>
#include <sys/termios.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char pkt[] = {0x10, 0x0a, 0x02, 0x26, 0x00, 0xce, 0x10, 0x03};
int speeds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};

int
main(int argc, char **argv){
	int fd, i, n;
	struct termios t;

	if ((fd = open(argv[1], O_RDWR | O_EXCL | O_NONBLOCK, 0600)) == -1)
		err(1, "open");

	n = sizeof(speeds) / sizeof(speeds[0]);
	for(i = 0; i < n; i++){
		tcgetattr(fd, &t);
		cfmakeraw(&t);
		cfsetspeed(&t, speeds[i]);
		tcsetattr(fd, TCSANOW | TCSAFLUSH, &t);
		fprintf(stderr, "%d ", speeds[i]);
		if (write(fd, pkt, sizeof(pkt)) != sizeof(pkt))
			err(1, "write");
		tcdrain(fd);
		usleep(333333);
	}
	fprintf(stderr, "done.\n");
	close(fd);
}
