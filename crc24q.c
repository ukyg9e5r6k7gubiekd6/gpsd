/*
 * This is an implementation of the CRC-24Q cyclic redundancy checksum
 * used by Qualcomm, RTCM104V3, and PGP 6.5.1. According to the RTCM104V3
 * standard, it uses the error polynomial
 *
 *    x^24+ x^23+ x^18+ x^17+ x^14+ x^11+ x^10+ x^7+ x^6+ x^5+ x^4+ x^3+ x+1
 *
 * This corresponds to a mask of 0x1864CFB.  For a primer on CRC theory,
 * including detailed discussion of how and why the error polynomial is
 * expressed by this mask, see <http://www.ross.net/crc/>.
 *
 * 1) It detects all single bit errors per 24-bit code word.
 * 2) It detects all double bit error combinations in a code word.
 * 3) It detects any odd number of errors.
 * 4) It detects any burst error for which the length of the burst is less than
 *    or equal to 24 bits.
 * 5) It detects most large error bursts with length greater than 24 bits;
 *    the odds of a false positive are at most 2^-23.
 *
 * This hash should not be considered cryptographically secure, but it
 * is extremely good at detecting noise errors.
 *
 * Note that this version has a seed of 0 wired in.  The RTCM104V3 standard
 * requires this.
 *
 * This file is Copyright (c) 2008,2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "crc24q.h"

#ifdef REBUILD_CRC_TABLE
/*
 * The crc24q code table below can be regenerated with the following code:
 */
#include <stdio.h>
#include <stdlib.h>

unsigned table[256];

#define CRCSEED	0		/* could be NZ to detect leading zeros */
#define CRCPOLY	0x1864CFB	/* encodes all info about the polynomial */

static void crc_init(unsigned table[256])
{
    unsigned i, j;
    unsigned h;

    table[0] = CRCSEED;
    table[1] = h = CRCPOLY;

    for (i = 2; i < 256; i *= 2) {
	if ((h <<= 1) & 0x1000000)
	    h ^= CRCPOLY;
	for (j = 0; j < i; j++)
	    table[i + j] = table[j] ^ h;
    }
}

int main(int argc, char *argv[])
{
    int i;

    crc_init(table);

    for (i = 0; i < 256; i++) {
	printf("0x%08X, ", table[i]);
	if ((i % 4) == 3)
	    putchar('\n');
    }

    exit(EXIT_SUCCESS);
}
#endif

static const unsigned crc24q[256] = {
    0x00000000U, 0x01864CFBU, 0x028AD50DU, 0x030C99F6U,
    0x0493E6E1U, 0x0515AA1AU, 0x061933ECU, 0x079F7F17U,
    0x08A18139U, 0x0927CDC2U, 0x0A2B5434U, 0x0BAD18CFU,
    0x0C3267D8U, 0x0DB42B23U, 0x0EB8B2D5U, 0x0F3EFE2EU,
    0x10C54E89U, 0x11430272U, 0x124F9B84U, 0x13C9D77FU,
    0x1456A868U, 0x15D0E493U, 0x16DC7D65U, 0x175A319EU,
    0x1864CFB0U, 0x19E2834BU, 0x1AEE1ABDU, 0x1B685646U,
    0x1CF72951U, 0x1D7165AAU, 0x1E7DFC5CU, 0x1FFBB0A7U,
    0x200CD1E9U, 0x218A9D12U, 0x228604E4U, 0x2300481FU,
    0x249F3708U, 0x25197BF3U, 0x2615E205U, 0x2793AEFEU,
    0x28AD50D0U, 0x292B1C2BU, 0x2A2785DDU, 0x2BA1C926U,
    0x2C3EB631U, 0x2DB8FACAU, 0x2EB4633CU, 0x2F322FC7U,
    0x30C99F60U, 0x314FD39BU, 0x32434A6DU, 0x33C50696U,
    0x345A7981U, 0x35DC357AU, 0x36D0AC8CU, 0x3756E077U,
    0x38681E59U, 0x39EE52A2U, 0x3AE2CB54U, 0x3B6487AFU,
    0x3CFBF8B8U, 0x3D7DB443U, 0x3E712DB5U, 0x3FF7614EU,
    0x4019A3D2U, 0x419FEF29U, 0x429376DFU, 0x43153A24U,
    0x448A4533U, 0x450C09C8U, 0x4600903EU, 0x4786DCC5U,
    0x48B822EBU, 0x493E6E10U, 0x4A32F7E6U, 0x4BB4BB1DU,
    0x4C2BC40AU, 0x4DAD88F1U, 0x4EA11107U, 0x4F275DFCU,
    0x50DCED5BU, 0x515AA1A0U, 0x52563856U, 0x53D074ADU,
    0x544F0BBAU, 0x55C94741U, 0x56C5DEB7U, 0x5743924CU,
    0x587D6C62U, 0x59FB2099U, 0x5AF7B96FU, 0x5B71F594U,
    0x5CEE8A83U, 0x5D68C678U, 0x5E645F8EU, 0x5FE21375U,
    0x6015723BU, 0x61933EC0U, 0x629FA736U, 0x6319EBCDU,
    0x648694DAU, 0x6500D821U, 0x660C41D7U, 0x678A0D2CU,
    0x68B4F302U, 0x6932BFF9U, 0x6A3E260FU, 0x6BB86AF4U,
    0x6C2715E3U, 0x6DA15918U, 0x6EADC0EEU, 0x6F2B8C15U,
    0x70D03CB2U, 0x71567049U, 0x725AE9BFU, 0x73DCA544U,
    0x7443DA53U, 0x75C596A8U, 0x76C90F5EU, 0x774F43A5U,
    0x7871BD8BU, 0x79F7F170U, 0x7AFB6886U, 0x7B7D247DU,
    0x7CE25B6AU, 0x7D641791U, 0x7E688E67U, 0x7FEEC29CU,
    0x803347A4U, 0x81B50B5FU, 0x82B992A9U, 0x833FDE52U,
    0x84A0A145U, 0x8526EDBEU, 0x862A7448U, 0x87AC38B3U,
    0x8892C69DU, 0x89148A66U, 0x8A181390U, 0x8B9E5F6BU,
    0x8C01207CU, 0x8D876C87U, 0x8E8BF571U, 0x8F0DB98AU,
    0x90F6092DU, 0x917045D6U, 0x927CDC20U, 0x93FA90DBU,
    0x9465EFCCU, 0x95E3A337U, 0x96EF3AC1U, 0x9769763AU,
    0x98578814U, 0x99D1C4EFU, 0x9ADD5D19U, 0x9B5B11E2U,
    0x9CC46EF5U, 0x9D42220EU, 0x9E4EBBF8U, 0x9FC8F703U,
    0xA03F964DU, 0xA1B9DAB6U, 0xA2B54340U, 0xA3330FBBU,
    0xA4AC70ACU, 0xA52A3C57U, 0xA626A5A1U, 0xA7A0E95AU,
    0xA89E1774U, 0xA9185B8FU, 0xAA14C279U, 0xAB928E82U,
    0xAC0DF195U, 0xAD8BBD6EU, 0xAE872498U, 0xAF016863U,
    0xB0FAD8C4U, 0xB17C943FU, 0xB2700DC9U, 0xB3F64132U,
    0xB4693E25U, 0xB5EF72DEU, 0xB6E3EB28U, 0xB765A7D3U,
    0xB85B59FDU, 0xB9DD1506U, 0xBAD18CF0U, 0xBB57C00BU,
    0xBCC8BF1CU, 0xBD4EF3E7U, 0xBE426A11U, 0xBFC426EAU,
    0xC02AE476U, 0xC1ACA88DU, 0xC2A0317BU, 0xC3267D80U,
    0xC4B90297U, 0xC53F4E6CU, 0xC633D79AU, 0xC7B59B61U,
    0xC88B654FU, 0xC90D29B4U, 0xCA01B042U, 0xCB87FCB9U,
    0xCC1883AEU, 0xCD9ECF55U, 0xCE9256A3U, 0xCF141A58U,
    0xD0EFAAFFU, 0xD169E604U, 0xD2657FF2U, 0xD3E33309U,
    0xD47C4C1EU, 0xD5FA00E5U, 0xD6F69913U, 0xD770D5E8U,
    0xD84E2BC6U, 0xD9C8673DU, 0xDAC4FECBU, 0xDB42B230U,
    0xDCDDCD27U, 0xDD5B81DCU, 0xDE57182AU, 0xDFD154D1U,
    0xE026359FU, 0xE1A07964U, 0xE2ACE092U, 0xE32AAC69U,
    0xE4B5D37EU, 0xE5339F85U, 0xE63F0673U, 0xE7B94A88U,
    0xE887B4A6U, 0xE901F85DU, 0xEA0D61ABU, 0xEB8B2D50U,
    0xEC145247U, 0xED921EBCU, 0xEE9E874AU, 0xEF18CBB1U,
    0xF0E37B16U, 0xF16537EDU, 0xF269AE1BU, 0xF3EFE2E0U,
    0xF4709DF7U, 0xF5F6D10CU, 0xF6FA48FAU, 0xF77C0401U,
    0xF842FA2FU, 0xF9C4B6D4U, 0xFAC82F22U, 0xFB4E63D9U,
    0xFCD11CCEU, 0xFD575035U, 0xFE5BC9C3U, 0xFFDD8538U,
};

unsigned crc24q_hash(unsigned char *data, int len)
{
    int i;
    unsigned crc = 0;

    for (i = 0; i < len; i++) {
	crc = (crc << 8) ^ crc24q[data[i] ^ (unsigned char)(crc >> 16)];
    }

    crc = (crc & 0x00ffffff);

    return crc;
}

#define LO(x)	(unsigned char)((x) & 0xff)
#define MID(x)	(unsigned char)(((x) >> 8) & 0xff)
#define HI(x)	(unsigned char)(((x) >> 16) & 0xff)

void crc24q_sign(unsigned char *data, int len)
{
    unsigned crc = crc24q_hash(data, len);

    data[len] = HI(crc);
    data[len + 1] = MID(crc);
    data[len + 2] = LO(crc);
}

bool crc24q_check(unsigned char *data, int len)
{
    unsigned crc = crc24q_hash(data, len - 3);

    return (((data[len - 3] == HI(crc)) &&
	     (data[len - 2] == MID(crc)) && (data[len - 1] == LO(crc))));
}
