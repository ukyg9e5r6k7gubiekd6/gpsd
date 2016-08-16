/*
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

/* cfmakeraw() needs _DEFAULT_SOURCE */
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/param.h>		/* defines BSD */
#ifdef __linux__
#include <sys/sysmacros.h>	/* defines major() */
#endif	/* __linux__ */

#include "gpsd_config.h"
#ifdef ENABLE_BLUEZ
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#endif /* ENABLE_BLUEZ */

#include "gpsd.h"

/* Workaround for HP-UX 11.23, which is missing CRTSCTS */
#ifndef CRTSCTS
#  ifdef CNEW_RTSCTS
#    define CRTSCTS CNEW_RTSCTS
#  else
#    define CRTSCTS 0
#  endif /* CNEW_RTSCTS */
#endif /* !CRTSCTS */

static sourcetype_t gpsd_classify(const char *path)
/* figure out what kind of device we're looking at */
{
    struct stat sb;

    if (stat(path, &sb) == -1)
	return source_unknown;
    else if (S_ISREG(sb.st_mode))
	return source_blockdev;
    /* this assumes we won't get UDP from a filesystem socket */
    else if (S_ISSOCK(sb.st_mode))
	return source_tcp;
    /* OS-independent check for ptys using Unix98 naming convention */
    else if (strncmp(path, "/dev/pts/", 9) == 0)
	return source_pty;
    else if (strncmp(path, "/dev/pps", 8) == 0)
	return source_pps;
    else if (S_ISFIFO(sb.st_mode))
	return source_pipe;
    else if (S_ISCHR(sb.st_mode)) {
	sourcetype_t devtype = source_rs232;
#ifdef __linux__
	/* Linux major device numbers live here
	 *
	 * https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/devices.txt
	 *
	 * Note: This code works because Linux major device numbers are
	 * stable and architecture-independent.  It is *not* a good model
	 * for other Unixes where either or both assumptions may break.
	 */
	int devmajor = major(sb.st_rdev);
        /* 207 are Freescale i.MX UARTs (ttymxc*) */
	if (devmajor == 4 || devmajor == 204 || devmajor == 207)
	    devtype = source_rs232;
	else if (devmajor == 188 || devmajor == 166)
	    devtype = source_usb;
	else if (devmajor == 216 || devmajor == 217)
	    devtype = source_bluetooth;
	else if (devmajor == 3 || (devmajor >= 136 && devmajor <= 143))
	    devtype = source_pty;
#endif /* __linux__ */
	/*
	 * See http://nadeausoftware.com/articles/2012/01/c_c_tip_how_use_compiler_predefined_macros_detect_operating_system
	 * for discussion how this works.  Key graphs:
	 *
	 * Compilers for the old BSD base for these distributions
	 * defined the __bsdi__ macro, but none of these distributions
	 * define it now. This leaves no generic "BSD" macro defined
	 * by the compiler itself, but all UNIX-style OSes provide a
	 * <sys/param.h> file. On BSD distributions, and only on BSD
	 * distributions, this file defines a BSD macro that's set to
	 * the OS version. Checking for this generic macro is more
	 * robust than looking for known BSD distributions with
	 * __DragonFly__, __FreeBSD__, __NetBSD__, and __OpenBSD__
	 * macros.
	 *
	 * Apple's OSX for the Mac and iOS for iPhones and iPads are
	 * based in part on a fork of FreeBSD distributed as
	 * Darwin. As such, OSX and iOS also define the BSD macro
	 * within <sys/param.h>. However, compilers for OSX, iOS, and
	 * Darwin do not define __unix__. To detect all BSD OSes,
	 * including OSX, iOS, and Darwin, use an #if/#endif that
	 * checks for __unix__ along with __APPLE__ and __MACH__ (see
	 * the later section on OSX and iOS).
	 */
#ifdef BSD
	/*
	 * Hacky check for pty, which is what really matters for avoiding
	 * adaptive delay.
	 */
	if (strncmp(path, "/dev/ttyp", 9) == 0 ||
	    strncmp(path, "/dev/ttyq", 9) == 0)
	    devtype = source_pty;
	else if (strncmp(path, "/dev/ttyU", 9) == 0 ||
	    strncmp(path, "/dev/dtyU", 9) == 0)
	    devtype = source_usb;
	/* XXX bluetooth */
#endif /* BSD */
	return devtype;
    } else
	return source_unknown;
}

#ifdef __linux__
#include <dirent.h>
#include <ctype.h>

static int fusercount(const char *path)
/* return true if any process has the specified path open */
{
    DIR *procd, *fdd;
    struct dirent *procentry, *fdentry;
    char procpath[32], fdpath[64], linkpath[64];
    int cnt = 0;

    if ((procd = opendir("/proc")) == NULL)
	return -1;
    while ((procentry = readdir(procd)) != NULL) {
	if (isdigit(procentry->d_name[0])==0)
	    continue;
	(void)snprintf(procpath, sizeof(procpath),
		       "/proc/%s/fd/", procentry->d_name);
	if ((fdd = opendir(procpath)) == NULL)
	    continue;
	while ((fdentry = readdir(fdd)) != NULL) {
	    (void)strlcpy(fdpath, procpath, sizeof(fdpath));
	    (void)strlcat(fdpath, fdentry->d_name, sizeof(fdpath));
	    (void)memset(linkpath, '\0', sizeof(linkpath));
	    if (readlink(fdpath, linkpath, sizeof(linkpath)) == -1)
		continue;
	    if (strcmp(linkpath, path) == 0) {
		++cnt;
	    }
	}
	(void)closedir(fdd);
    }
    (void)closedir(procd);

    return cnt;
}
#endif /* __linux__ */

void gpsd_tty_init(struct gps_device_t *session)
/* to be called on allocating a device */
{
    /* mark GPS fd closed and its baud rate unknown */
    session->gpsdata.gps_fd = -1;
    session->saved_baud = -1;
    session->zerokill = false;
    session->reawake = (time_t)0;
}

#if defined(__CYGWIN__)
/* Workaround for Cygwin, which is missing cfmakeraw */
/* Pasted from man page; added in serial.c arbitrarily */
void cfmakeraw(struct termios *termios_p)
{
    termios_p->c_iflag &=
	~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;
}
#endif /* defined(__CYGWIN__) */

static speed_t gpsd_get_speed_termios(const struct termios *ttyctl)
{
    speed_t code = cfgetospeed(ttyctl);
    switch (code) {
    case B300:
	return (300);
    case B1200:
	return (1200);
    case B2400:
	return (2400);
    case B4800:
	return (4800);
    case B9600:
	return (9600);
    case B19200:
	return (19200);
    case B38400:
	return (38400);
    case B57600:
	return (57600);
    case B115200:
	return (115200);
    case B230400:
	return (230400);
    default: /* B0 */
	return 0;
    }
}

speed_t gpsd_get_speed(const struct gps_device_t *dev)
{
    return gpsd_get_speed_termios(&dev->ttyset);
}

speed_t gpsd_get_speed_old(const struct gps_device_t *dev)
{
    return gpsd_get_speed_termios(&dev->ttyset_old);
}

char gpsd_get_parity(const struct gps_device_t *dev)
{
    char parity = 'N';
    if ((dev->ttyset.c_cflag & (PARENB | PARODD)) == (PARENB | PARODD))
	parity = 'O';
    else if ((dev->ttyset.c_cflag & PARENB) == PARENB)
	parity = 'E';
    return parity;
}

int gpsd_get_stopbits(const struct gps_device_t *dev)
{
    int stopbits = 0;
    if ((dev->ttyset.c_cflag & CS8) == CS8)
	stopbits = 1;
    else if ((dev->ttyset.c_cflag & (CS7 | CSTOPB)) == (CS7 | CSTOPB))
	stopbits = 2;
    return stopbits;
}

bool gpsd_set_raw(struct gps_device_t * session)
{
    (void)cfmakeraw(&session->ttyset);
    if (tcsetattr(session->gpsdata.gps_fd, TCIOFLUSH, &session->ttyset) == -1) {
	gpsd_log(&session->context->errout, LOG_ERROR,
		 "SER: error changing port attributes: %s\n", strerror(errno));
	return false;
    }

    return true;
}

void gpsd_set_speed(struct gps_device_t *session,
		    speed_t speed, char parity, unsigned int stopbits)
{
    speed_t rate;
    struct timespec delay;

    /*
     * Yes, you can set speeds that aren't in the hunt loop.  If you
     * do this, and you aren't on Linux where baud rate is preserved
     * across port closings, you've screwed yourself. Don't do that!
     */
    if (speed < 300)
	rate = B0;
    else if (speed < 1200)
	rate = B300;
    else if (speed < 2400)
	rate = B1200;
    else if (speed < 4800)
	rate = B2400;
    else if (speed < 9600)
	rate = B4800;
    else if (speed < 19200)
	rate = B9600;
    else if (speed < 38400)
	rate = B19200;
    else if (speed < 57600)
	rate = B38400;
    else if (speed < 115200)
	rate = B57600;
    else if (speed < 230400)
	rate = B115200;
    else
	rate = B230400;

    if (rate != cfgetispeed(&session->ttyset)
	|| parity != session->gpsdata.dev.parity
	|| stopbits != session->gpsdata.dev.stopbits) {

	/*
	 * Don't mess with this conditional! Speed zero is supposed to mean
	 * to leave the port speed at whatever it currently is. This leads
	 * to excellent behavior on Linux, which preserves baudrate across
	 * serial device closes - it means that if you've opended this
	 * device before you typically don't have to hunt at all because
	 * it's still at the same speed you left it - you'll typically
	 * get packet lock within 1.5 seconds.  Alas, the BSDs and OS X
	 * aren't so nice.
	 */
	if (rate != B0) {
	    (void)cfsetispeed(&session->ttyset, rate);
	    (void)cfsetospeed(&session->ttyset, rate);
	}
	session->ttyset.c_iflag &= ~(PARMRK | INPCK);
	session->ttyset.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
	session->ttyset.c_cflag |= (stopbits == 2 ? CS7 | CSTOPB : CS8);
	switch (parity) {
	case 'E':
	case (char)2:
	    session->ttyset.c_iflag |= INPCK;
	    session->ttyset.c_cflag |= PARENB;
	    break;
	case 'O':
	case (char)1:
	    session->ttyset.c_iflag |= INPCK;
	    session->ttyset.c_cflag |= PARENB | PARODD;
	    break;
	}
	if (tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset) != 0) {
	    /* strangely this fails on non-serial ports, but if
             * we do not try, we get other failures.
             * so ignore for now, as we always have, until it can
             * be nailed down.
             *
	     * gpsd_log(&session->context->errout, LOG_ERROR,
	     *	     "SER: error setting port attributes: %s, sourcetype: %d\n",
	     *	     strerror(errno), session->sourcetype);
	     * return;
             */
	}

	/*
	 * Serious black magic begins here.  Getting this code wrong can cause
	 * failures to lock to a correct speed, and not clean reproducible
	 * failures but flukey hardware- and timing-dependent ones.  So
	 * be very sure you know what you're doing before hacking it, and
	 * test thoroughly.
	 *
	 * The fundamental problem here is that serial devices take time
	 * to settle into a new baud rate after tcsetattr() is issued. Until
	 * they do so, input will be arbitarily garbled.  Normally this
	 * is not a big problem, but in our hunt loop the garbling can trash
	 * a long enough prefix of each sample to prevent detection of a
	 * packet header.  We could address the symptom by making the sample
	 * size enough larger that subtracting the maximum length of garble
	 * would still leave a sample longer than the maximum packet size.
	 * But it's better (and more efficient) to address the disease.
	 *
	 * In theory, one might think that not even a tcflush() call would
	 * be needed, with tcsetattr() delaying its return until the device
	 * is in a good state.  For simple devices like a 14550 UART that
	 * have fixed response timings this may even work, if the driver
	 * writer was smart enough to delay the return by the right number
	 * of milliseconds after poking the device port(s).
	 *
	 * Problems may arise if the driver's timings are off.  Or we may
	 * be talking to a USB device like the pl2303 commonly used in GPS
	 * mice; on these, the change will not happen immediately because
	 * it has to be sent as a message to the external processor that
	 * has to act upon it, and that processor may still have buffered
	 * data in its own FIFO.  In this case the expected delay may be
	 * too large and too variable (depending on the details of how the
	 * USB device is integrated with its symbiont hardware) to be put
	 * in the driver.
	 *
	 * So, somehow, we have to introduce a delay after tcsatattr()
	 * returns sufficient to allow *any* device to settle.  On the other
	 * hand, a really long delay will make gpsd device registration
	 * unpleasantly laggy.
	 *
	 * The classic way to address this is with a tcflush(), counting
	 * on it to clear the device FIFO. But that call may clear only the
	 * kernel buffers, not the device's hardware FIFO, so it may not
	 * be sufficient by itself.
	 *
	 * flush followed by a 200-millisecond delay followed by flush has
	 * been found to work reliably on the pl2303.  It is also known
	 * from testing that a 100-millisec delay is too short, allowing
	 * occasional failure to lock.
	 */
	(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);

        /* wait 200,000 uSec */
	delay.tv_sec = 0;
	delay.tv_nsec = 200000000L;
	nanosleep(&delay, NULL);
	(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }
    gpsd_log(&session->context->errout, LOG_INF,
	     "SER: speed %lu, %d%c%d\n",
	     (unsigned long)gpsd_get_speed(session), 9 - stopbits, parity,
	     stopbits);

    session->gpsdata.dev.baudrate = (unsigned int)speed;
    session->gpsdata.dev.parity = parity;
    session->gpsdata.dev.stopbits = stopbits;

    /*
     * The device might need a wakeup string before it will send data.
     * If we don't know the device type, ship it every driver's wakeup
     * in hopes it will respond.  But not to USB or Bluetooth, because
     * shipping probe strings to unknown USB serial adaptors or
     * Bluetooth devices may spam devices that aren't GPSes at all and
     * could become confused.
     */
    if (!session->context->readonly
		&& session->sourcetype != source_usb
		&& session->sourcetype != source_bluetooth) {
	if (isatty(session->gpsdata.gps_fd) != 0
	    && !session->context->readonly) {
	    if (session->device_type == NULL) {
		const struct gps_type_t **dp;
		for (dp = gpsd_drivers; *dp; dp++)
		    if ((*dp)->event_hook != NULL)
			(*dp)->event_hook(session, event_wakeup);
	    } else if (session->device_type->event_hook != NULL)
		session->device_type->event_hook(session, event_wakeup);
	}
    }
    packet_reset(&session->lexer);
}

int gpsd_serial_open(struct gps_device_t *session)
/* open a device for access to its data
 * return: the opened file descriptor
 *         PLACEHOLDING_FD - for /dev/ppsX
 *         UNALLOCATED_FD - for open failure
 */

{
    mode_t mode = (mode_t) O_RDWR;

    session->sourcetype = gpsd_classify(session->gpsdata.dev.path);
    session->servicetype = service_sensor;

    /* we may need to hold on to this slot without opening the device */
    if (source_pps == session->sourcetype) {
	(void)gpsd_switch_driver(session, "PPS");
	return PLACEHOLDING_FD;
    }

    if (session->context->readonly
	|| (session->sourcetype <= source_blockdev)) {
	mode = (mode_t) O_RDONLY;
	gpsd_log(&session->context->errout, LOG_INF,
		 "SER: opening read-only GPS data source type %d and at '%s'\n",
		 (int)session->sourcetype, session->gpsdata.dev.path);
    } else {
	gpsd_log(&session->context->errout, LOG_INF,
		 "SER: opening GPS data source type %d at '%s'\n",
		 (int)session->sourcetype, session->gpsdata.dev.path);
    }
#ifdef ENABLE_BLUEZ
    if (bachk(session->gpsdata.dev.path) == 0) {
        struct sockaddr_rc addr = { 0, *BDADDR_ANY, 0};
        session->gpsdata.gps_fd = socket(AF_BLUETOOTH,
					 SOCK_STREAM,
					 BTPROTO_RFCOMM);
        addr.rc_family = AF_BLUETOOTH;
        addr.rc_channel = (uint8_t) 1;
        (void) str2ba(session->gpsdata.dev.path, &addr.rc_bdaddr);
        if (connect(session->gpsdata.gps_fd, (struct sockaddr *) &addr, sizeof (addr)) == -1) {
	    if (errno != EINPROGRESS && errno != EAGAIN) {
		(void)close(session->gpsdata.gps_fd);
		gpsd_log(&session->context->errout, LOG_ERROR,
			 "SER: bluetooth socket connect failed: %s\n",
			 strerror(errno));
		return UNALLOCATED_FD;
	    }
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "SER: bluetooth socket connect in progress or again : %s\n",
		     strerror(errno));
        }
	(void)fcntl(session->gpsdata.gps_fd, F_SETFL, (int)mode);
	gpsd_log(&session->context->errout, LOG_PROG,
		 "SER: bluez device open success: %s %s\n",
		 session->gpsdata.dev.path, strerror(errno));
    } else
#endif /* BLUEZ */
    {
	/*
	 * We open with O_NONBLOCK because we want to not get hung if
	 * the clocal flag is off, but we don't want to stay in that mode.
	 */
	errno = 0;
        if ((session->gpsdata.gps_fd =
	     open(session->gpsdata.dev.path, (int)(mode | O_NONBLOCK | O_NOCTTY))) == -1) {
            gpsd_log(&session->context->errout, LOG_ERROR,
		     "SER: device open of %s failed: %s - retrying read-only\n",
		     session->gpsdata.dev.path,
		     strerror(errno));
	    if ((session->gpsdata.gps_fd =
		 open(session->gpsdata.dev.path, O_RDONLY | O_NONBLOCK | O_NOCTTY)) == -1) {
		gpsd_log(&session->context->errout, LOG_ERROR,
			 "SER: read-only device open of %s failed: %s\n",
			 session->gpsdata.dev.path,
			 strerror(errno));
		return UNALLOCATED_FD;
	    }

	    gpsd_log(&session->context->errout, LOG_PROG,
		     "SER: file device open of %s succeeded\n",
		     session->gpsdata.dev.path);
	}
    }

    /*
     * Ideally we want to exclusion-lock the device before doing any reads.
     * It would have been best to do this at open(2) time, but O_EXCL
     * doesn't work without O_CREAT.
     *
     * We have to make an exception for ptys, which are intentionally
     * opened by another process on the master side, otherwise we'll
     * break all our regression tests.
     *
     * We also exclude bluetooth device because the bluetooth daemon opens them.
     */
    if (!(session->sourcetype == source_pty || session->sourcetype == source_bluetooth)) {
#ifdef TIOCEXCL
	/*
	 * Try to block other processes from using this device while we
	 * have it open (later opens should return EBUSY).  Won't work
	 * against anything with root privileges, alas.
	 */
	(void)ioctl(session->gpsdata.gps_fd, (unsigned long)TIOCEXCL);
#endif /* TIOCEXCL */

#ifdef __linux__
	/*
	 * Don't touch devices already opened by another process.
	 */
	if (fusercount(session->gpsdata.dev.path) > 1) {
            gpsd_log(&session->context->errout, LOG_ERROR,
		     "SER: %s already opened by another process\n",
		     session->gpsdata.dev.path);
	    (void)close(session->gpsdata.gps_fd);
	    session->gpsdata.gps_fd = UNALLOCATED_FD;
	    return UNALLOCATED_FD;
	}
#endif /* __linux__ */
    }

#ifdef FIXED_PORT_SPEED
    session->saved_baud = FIXED_PORT_SPEED;
#endif

    if (session->saved_baud != -1) {
	(void)cfsetispeed(&session->ttyset, (speed_t)session->saved_baud);
	(void)cfsetospeed(&session->ttyset, (speed_t)session->saved_baud);
	if (tcsetattr(session->gpsdata.gps_fd, TCSANOW, &session->ttyset) != 0) {
	    gpsd_log(&session->context->errout, LOG_ERROR,
		     "SER: Error setting port attributes: %s\n",
		     strerror(errno));
	}
	(void)tcflush(session->gpsdata.gps_fd, TCIOFLUSH);
    }

    session->lexer.type = BAD_PACKET;
    if ( 0 != isatty(session->gpsdata.gps_fd) ) {

	/* Save original terminal parameters */
	if (tcgetattr(session->gpsdata.gps_fd, &session->ttyset_old) != 0)
	    return UNALLOCATED_FD;
	session->ttyset = session->ttyset_old;
        /* twiddle the speed, parity, etc. but only on real serial ports */
	memset(session->ttyset.c_cc, 0, sizeof(session->ttyset.c_cc));
	//session->ttyset.c_cc[VTIME] = 1;
	/*
	 * Tip from Chris Kuethe: the FIDI chip used in the Trip-Nav
	 * 200 (and possibly other USB GPSes) gets completely hosed
	 * in the presence of flow control.  Thus, turn off CRTSCTS.
	 *
	 * This is not ideal.  Setting no parity here will mean extra
	 * initialization time for some devices, like certain Trimble
	 * boards, that want 7O2 or other non-8N1 settings. But starting
	 * the hunt loop at 8N1 will minimize the average sync time
	 * over all devices.
	 */
	session->ttyset.c_cflag &= ~(PARENB | PARODD | CRTSCTS | CSTOPB);
	session->ttyset.c_cflag |= CREAD | CLOCAL;
	session->ttyset.c_iflag = session->ttyset.c_oflag =
	    session->ttyset.c_lflag = (tcflag_t) 0;

#ifndef FIXED_PORT_SPEED
	session->baudindex = 0;
#endif /* FIXED_PORT_SPEED */
	gpsd_set_speed(session,
#ifdef FIXED_PORT_SPEED
		       FIXED_PORT_SPEED,
#else
		       gpsd_get_speed_old(session),
#endif /* FIXED_PORT_SPEED */
		       'N',
#ifdef FIXED_STOP_BITS
		       FIXED_STOP_BITS
#else
		       1
#endif /* FIXED_STOP_BITS */
	    );
    }

    /* Probably want to switch back to blocking I/O now that CLOCAL is set. */
    if (session->sourcetype != source_pipe)
    {
	int oldfl = fcntl(session->gpsdata.gps_fd, F_GETFL);
	if (oldfl != -1)
	    (void)fcntl(session->gpsdata.gps_fd, F_SETFL, oldfl & ~O_NONBLOCK);
    }


    /* required so parity field won't be '\0' if saved speed matches */
    if (session->sourcetype <= source_blockdev) {
	session->gpsdata.dev.parity = 'N';
	session->gpsdata.dev.stopbits = 1;
    }

    gpsd_log(&session->context->errout, LOG_SPIN,
	     "SER: open(%s) -> %d in gpsd_serial_open()\n",
	     session->gpsdata.dev.path, session->gpsdata.gps_fd);
    return session->gpsdata.gps_fd;
}

ssize_t gpsd_serial_write(struct gps_device_t * session,
			  const char *buf, const size_t len)
{
    ssize_t status;
    bool ok;
    if (session == NULL ||
	session->context == NULL || session->context->readonly)
	return 0;
    status = write(session->gpsdata.gps_fd, buf, len);
    ok = (status == (ssize_t) len);
    (void)tcdrain(session->gpsdata.gps_fd);
    /* extra guard prevents expensive hexdump calls */
    if (session->context->errout.debug >= LOG_IO) {
	char scratchbuf[MAX_PACKET_LENGTH*2+1];
	gpsd_log(&session->context->errout, LOG_IO,
		 "SER: => GPS: %s%s\n",
		 gpsd_packetdump(scratchbuf, sizeof(scratchbuf),
				 (char *)buf, len), ok ? "" : " FAILED");
    }
    return status;
}

/*
 * This constant controls how long the packet sniffer will spend looking
 * for a packet leader before it gives up.  It *must* be larger than
 * MAX_PACKET_LENGTH or we risk never syncing up at all.  Large values
 * will produce annoying startup lag.
 */
#define SNIFF_RETRIES	(MAX_PACKET_LENGTH + 128)

bool gpsd_next_hunt_setting(struct gps_device_t * session)
/* advance to the next hunt setting  */
{
    /* don't waste time in the hunt loop if this is not actually a tty */
    if (isatty(session->gpsdata.gps_fd) == 0)
	return false;

    /* ...or if it's nominally a tty but delivers only PPS and no data */
    if (session->sourcetype == source_pps)
	return false;

    if (session->lexer.retry_counter++ >= SNIFF_RETRIES) {
#ifdef FIXED_PORT_SPEED
	return false;
#else
	/* every rate we're likely to see on a GPS */
	static unsigned int rates[] =
	    { 0, 4800, 9600, 19200, 38400, 57600, 115200, 230400};

	if (session->baudindex++ >=
	    (unsigned int)(sizeof(rates) / sizeof(rates[0])) - 1) {
	    session->baudindex = 0;
#ifdef FIXED_STOP_BITS
	    return false;	/* hunt is over, no sync */
#else
	    if (session->gpsdata.dev.stopbits++ >= 2)
		return false;	/* hunt is over, no sync */
#endif /* FIXED_STOP_BITS */
	}
#endif /* FIXED_PORT_SPEED */
	// cppcheck-suppress unreachableCode
	gpsd_set_speed(session,
#ifdef FIXED_PORT_SPEED
		       FIXED_PORT_SPEED,
#else
		       rates[session->baudindex],
#endif /* FIXED_PORT_SPEED */
		       session->gpsdata.dev.parity,
#ifdef FIXED_STOP_BITS
		       FIXED_STOP_BITS
#else
		       session->gpsdata.dev.stopbits
#endif /* FIXED_STOP_BITS */
	    );
	session->lexer.retry_counter = 0;
    }

    return true;		/* keep hunting */

}

void gpsd_assert_sync(struct gps_device_t *session)
/* to be called when we want to register that we've synced with a device */
{
    /*
     * We've achieved first sync with the device. Remember the
     * baudrate so we can try it first next time this device
     * is opened.
     */
    if (session->saved_baud == -1)
	session->saved_baud = (int)cfgetispeed(&session->ttyset);
}

void gpsd_close(struct gps_device_t *session)
{
    if (!BAD_SOCKET(session->gpsdata.gps_fd)) {
#ifdef TIOCNXCL
	(void)ioctl(session->gpsdata.gps_fd, (unsigned long)TIOCNXCL);
#endif /* TIOCNXCL */
	(void)tcdrain(session->gpsdata.gps_fd);
	if (isatty(session->gpsdata.gps_fd) != 0) {
	    /* force hangup on close on systems that don't do HUPCL properly */
	    (void)cfsetispeed(&session->ttyset, (speed_t) B0);
	    (void)cfsetospeed(&session->ttyset, (speed_t) B0);
	    (void)tcsetattr(session->gpsdata.gps_fd, TCSANOW,
			    &session->ttyset);
	}
	/* this is the clean way to do it */
	session->ttyset_old.c_cflag |= HUPCL;
	/*
	 * Don't revert the serial parameters if we didn't have to mess with
	 * them the first time.  Economical, and avoids tripping over an
	 * obscure Linux 2.6 kernel bug that disables threaded
	 * ioctl(TIOCMWAIT) on a device after tcsetattr() is called.
	 */
	if (cfgetispeed(&session->ttyset_old) != cfgetispeed(&session->ttyset) || (session->ttyset_old.c_cflag & CSTOPB) != (session->ttyset.c_cflag & CSTOPB)) {
	    /*
	     * If we revert, keep the most recent baud rate.
	     * Cuts down on autobaud overhead the next time.
	     */
	    (void)cfsetispeed(&session->ttyset_old,
			      (speed_t) session->gpsdata.dev.baudrate);
	    (void)cfsetospeed(&session->ttyset_old,
			      (speed_t) session->gpsdata.dev.baudrate);
	    (void)tcsetattr(session->gpsdata.gps_fd, TCSANOW,
			    &session->ttyset_old);
	}
	gpsd_log(&session->context->errout, LOG_SPIN,
		 "SER: close(%d) in gpsd_close(%s)\n",
		 session->gpsdata.gps_fd, session->gpsdata.dev.path);
	(void)close(session->gpsdata.gps_fd);
	session->gpsdata.gps_fd = -1;
    }
}
