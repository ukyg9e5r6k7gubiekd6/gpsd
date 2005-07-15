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

#ifndef _CSKPROG_H_
#define _CSKPROG_H_

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <netinet/in.h>	/* for htonl() under Linux */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*
 * I can't imagine a GPS firmware less than 256KB / 2Mbit. The latest build
 * that I have (2.3.2) is 296KB. So 256KB is probably low enough to allow
 * really old firmwares to load.
 *
 * As far as I know, USB receivers have 512KB / 4Mbit of flash. Application
 * note APNT00016 (Alternate Flash Programming Algorithms) says that the S2AR
 * reference design supports 4, 8 or 16 Mbit flash memories, but with current
 * firmwares not even using 60% of a 4Mbit flash on a commercial receiver,
 * I'm not going to stress over loading huge images. The define below is
 * 524288 bytes, but that blows up nearly 3 times as S-records.
 * 928K srec -> 296K binary
 */
#define MIN_FW_SIZE 262144
#define MAX_FW_SIZE 1572864

/* a reasonable loader is probably 15K - 20K */
#define MIN_LD_SIZE 15440
#define MAX_LD_SIZE 20480

/* From the SiRF protocol manual... may as well be consistent */
#define PROTO_SIRF 0
#define PROTO_NMEA 1

#define BOOST_38400 0
#define BOOST_57600 1
#define BOOST_115200 2

/* block size when writing to the serial port. related to FIFO size */
#define WRBLK 128

/* Prototypes */
/* sirfflash.c */
int		main(int, char **);
void		usage(void);

/* utils.c */
int		serialConfig(int, struct termios *, int);
int		serialSpeed(int, struct termios *, int);
int		sirfSetProto(int, struct termios *, int, int);
int		sirfSendUpdateCmd(int);
int		sirfSendLoader(int, struct termios *, char *, int);
int		sirfSendFirmware(int, char *, int);
int		sirfWrite(int, unsigned char *);
unsigned char	nmea_checksum(unsigned char *);

/* srecord.c */
void		hexdump(int , unsigned char *, unsigned char *);
unsigned char	sr_sum(int, int, unsigned char *);
int		bin2srec(int, int, unsigned char *, unsigned char *);
int		srec_hdr(unsigned char *);
int		srec_fin(int, unsigned char *);
char hc(char);


#endif /* _CSKPROG_H_ */
