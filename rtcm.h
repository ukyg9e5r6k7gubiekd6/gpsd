/*
 * Structures for interpreting words in an RTCM-104 message (after
 * parity checking and removing inversion). RTCM104 is an obscure and
 * complicated serial protocol used for broadcasting pseudorange
 * corrections from differential-GPS reference stations. This header
 * is part of the GPSD package: see <http://gpsd.berlios.de> for more.
 *
 * The RTCM words are 30-bit words.  We will lay them into memory into
 * 30-bit (low-end justified) chunks.  To write them out we will write
 * 5 Magnavox-format bytes where the low 6-bits of the byte are 6-bits
 * of the 30-word msg.
 */




/* end */
