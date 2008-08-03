/* $Id$ */
#ifndef _CRC24Q_H_
#define _CRC24Q_H_

/* Interface for CRC-24Q cyclic redundancy chercksum code */

extern void crc24q_sign(unsigned char *data, int len);

extern bool crc24q_check(unsigned char *data, int len);

extern unsigned crc24q_hash(unsigned char *data, int len);
#endif /* _CRC24Q_H_ */
