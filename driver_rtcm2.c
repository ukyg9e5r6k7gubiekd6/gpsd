/*****************************************************************************

This is a decoder for RTCM-104 2.x, an obscure and complicated serial
protocol used for broadcasting pseudorange corrections from
differential-GPS reference stations.  The applicable
standard is

RTCM RECOMMENDED STANDARDS FOR DIFFERENTIAL GNSS (GLOBAL NAVIGATION
SATELLITE) SERVICE, VERSION 2.3 (RTCM PAPER 136-2001/SC104-STD)

Ordering instructions are accessible from <http://www.rtcm.org/>
under "Publications".  This describes version 2.3 of the RTCM specification.
RTCM-104 was later completely redesigned as level 3.0.

Also applicable is ITU-R M.823: "Technical characteristics of
differential transmissions for global navigation satellite systems
from maritime radio beacons in the frequency band 283.5 - 315 kHz in
region 1 and 285 - 325 kHz in regions 2 & 3."

The RTCM 2.x protocol uses as a transport layer the GPS satellite downlink
protocol described in IS-GPS-200, the Navstar GPS Interface
Specification.  This code relies on the lower-level packet-assembly
code for that protocol in isgps.c.

The lower layer's job is done when it has assembled a message of up to
33 30-bit words of clean parity-checked data.  At this point this upper layer
takes over.  struct rtcm2_msg_t is overlaid on the buffer and the bitfields
are used to extract pieces of it.  Those pieces are copied and (where
necessary) reassembled into a struct rtcm2_t.

This code and the contents of isgps.c are evolved from code by
Wolfgang Rupprecht.  Wolfgang's decoder was loosely based on one
written by John Sager in 1999.  Here are John Sager's original notes:

The RTCM decoder prints a legible representation of the input data.
The RTCM SC-104 specification is copyrighted, so I cannot
quote it - in fact, I have never read it! Most of the information
used to develop the decoder came from publication ITU-R M.823.
This is a specification of the data transmitted from LF DGPS
beacons in the 300kHz band. M.823 contains most of those parts of
RTCM SC-104 directly relevant to the air interface (there
are one or two annoying and vital omissions!). Information
about the serial interface format was gleaned from studying
the output of a beacon receiver test program made available on
Starlink's website.

This code has been checked against ASCII dumps made by a proprietary
decoder running under Windows and is known to be consistent with it
with respect to message types 1, 3, 9, 14, 16, and 31.  Decoding of
message types 4, 5, 6, 7, and 13 has not been checked. Message types
8, 10-12, 15-27, 28-30 (undefined), 31-37, 38-58 (undefined), and
60-63 are not yet supported.

This file is Copyright (c) 2010-2018 by the GPSD project
SPDX-License-Identifier: BSD-2-clause

*****************************************************************************/

#include "gpsd_config.h"  /* must be before all includes */

#include <stdio.h>
#include <string.h>

#include "gpsd.h"

/*
  __BYTE_ORDER__, __ORDER_BIG_ENDIAN__ and __ORDER_LITTLE_ENDIAN__ are
  defined in some gcc versions only, probably depending on the
  architecture. Try to use endian.h if the gcc way fails - endian.h also
  does not seem to be available on all platforms.
*/

#if HAVE_BUILTIN_ENDIANNESS
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#undef WORDS_BIGENDIAN
#else
#error Unknown endianness!
#endif

#else /* HAVE_BUILTIN_ENDIANNESS */

#if defined(HAVE_ENDIAN_H)
#include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>
#endif

/*
 * Darwin (Mac OS X) uses special defines.
 * This must precede the BSD case, since _BIG_ENDIAN may be incorrectly defined
 */
#if !defined( __BYTE_ORDER) && defined(__DARWIN_BYTE_ORDER)
#define __BYTE_ORDER __DARWIN_BYTE_ORDER
#endif
#if !defined( __BIG_ENDIAN) && defined(__DARWIN_BIG_ENDIAN)
#define __BIG_ENDIAN __DARWIN_BIG_ENDIAN
#endif
#if !defined( __LITTLE_ENDIAN) && defined(__DARWIN_LITTLE_ENDIAN)
#define __LITTLE_ENDIAN __DARWIN_LITTLE_ENDIAN
#endif

/*
 * BSD uses _BYTE_ORDER, and Linux uses __BYTE_ORDER.
 */
#if !defined( __BYTE_ORDER) && defined(_BYTE_ORDER)
#define __BYTE_ORDER _BYTE_ORDER
#endif
#if !defined( __BIG_ENDIAN) && defined(_BIG_ENDIAN)
#define __BIG_ENDIAN _BIG_ENDIAN
#endif
#if !defined( __LITTLE_ENDIAN) && defined(_LITTLE_ENDIAN)
#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#endif

#if !defined(__BYTE_ORDER) || !defined(__BIG_ENDIAN) || \
    !defined(__LITTLE_ENDIAN)
#error endianness macros are not defined
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define WORDS_BIGENDIAN 1
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#undef WORDS_BIGENDIAN
#else
#error Unknown endianness!
#endif /* __BYTE_ORDER */

#endif /* HAVE_BUILTIN_ENDIANNESS */

/*
 * Structures for interpreting words in an RTCM-104 2.x message (after
 * parity checking and removing inversion).  Note, these structures
 * are overlayed on the raw data in order to decode them into
 * bitfields; this will fail horribly if your C compiler introduces
 * padding between or before bit fields, or between 8-bit-aligned
 * bitfields and character arrays despite #pragma pack(1).  The right
 * things happen under gcc 4.x on amd64, i386, ia64, all arm and mips
 * variants, m68k, and powerpc)
 *
 * (In practice, the only class of machines on which this is likely
 * to fail are word-aligned architectures without barrel shifters.
 * Very few of these are left in 2012. By test, we know of s390, s390x,
 * and sparc.)
 *
 * The RTCM 2.1 standard is less explicit than it should be about
 * signed-integer representations.  Two's complement is specified for
 * some but not all.
 */

#define ZCOUNT_SCALE    0.6     /* sec */
#define PRCSMALL        0.02    /* meters */
#define PRCLARGE        0.32    /* meters */
#define RRSMALL         0.002   /* meters/sec */
#define RRLARGE         0.032   /* meters/sec */

#define MAXPCSMALL     (0x7FFF * PCSMALL)  /* 16-bits signed */
#define MAXRRSMALL     (0x7F   * RRSMALL)  /*  8-bits signed */

#define XYZ_SCALE       0.01    /* meters */
// extended ECEF scale
#define EXYZ_SCALE      0.0001          // meters
// ECEF delta scale
#define DXYZ_SCALE      0.1     /* meters */
// extended ECEF delta scale: 1/256 cm
#define EDXYZ_SCALE     (1.0/256.0)     // centimeter
// L2 extended ECEF delta scale: 1/16 cm
#define EDXYZ2_SCALE     (1.0/16.0)     // centimeter
#define LA_SCALE        (90.0/32767.0)  /* degrees */
#define LO_SCALE        (180.0/32767.0) /* degrees */
#define FREQ_SCALE      0.1     /* kHz */
#define FREQ_OFFSET     190.0   /* kHz */
#define CNR_OFFSET      24      /* dB */
#define TU_SCALE        5       /* minutes */

#define LATLON_SCALE    0.01    /* degrees */
#define RANGE_SCALE     4       /* kilometers */

#pragma pack(1)

/*
 * Reminder: Emacs reverse-region is useful...
 */

#ifndef WORDS_BIGENDIAN /* little-endian, like x86, amd64 */

struct rtcm2_msg_t {
    struct rtcm2_msghw1 {                       /* header word 1 */
        unsigned int            parity:6;
        unsigned int            refstaid:10;    /* reference station ID */
        unsigned int            msgtype:6;      /* RTCM message type */
        unsigned int            preamble:8;     /* fixed at 01100110 */
        unsigned int            _pad:2;
    } w1;

    struct rtcm2_msghw2 {                       /* header word 2 */
        unsigned int            parity:6;
        unsigned int            stathlth:3;     /* station health */
        unsigned int            frmlen:5;
        unsigned int            sqnum:3;
        unsigned int            zcnt:13;
        unsigned int            _pad:2;
    } w2;

    union {
        /* msg 1 - differential gps corrections */
        struct rtcm2_msg1 {
            struct gps_correction_t {
                struct {                        /* msg 1 word 3 */
                    unsigned int    parity:6;
                    int             prc1:16;
                    unsigned int    satident1:5;        /* satellite ID */
                    unsigned int    udre1:2;
                    unsigned int    scale1:1;
                    unsigned int    _pad:2;
                } w3;
                struct {                        /* msg 1 word 4 */
                    unsigned int    parity:6;
                    unsigned int    satident2:5;        /* satellite ID */
                    unsigned int    udre2:2;
                    unsigned int    scale2:1;
                    unsigned int    iod1:8;
                    int             rrc1:8;
                    unsigned int    _pad:2;
                } w4;
                struct {                        /* msg 1 word 5 */
                    unsigned int    parity:6;
                    int             rrc2:8;
                    int             prc2:16;
                    unsigned int    _pad:2;
                } w5;
                struct {                        /* msg 1 word 6 */
                    unsigned int    parity:6;
                    int             prc3_h:8;
                    unsigned int    satident3:5;        /* satellite ID */
                    unsigned int    udre3:2;
                    unsigned int    scale3:1;
                    unsigned int    iod2:8;
                    unsigned int    _pad:2;
                } w6;
                struct {                        /* msg 1 word 7 */
                    unsigned int    parity:6;
                    unsigned int    iod3:8;
                    int             rrc3:8;
                    /* NOTE: unsigned int for low byte */
                    unsigned int    prc3_l:8;
                    unsigned int    _pad:2;
                } w7;
            } corrections[(RTCM2_WORDS_MAX - 2) / 5];
        } type1;

        /* msg 2 -  Pseudo-range corrections, referring to previous orbit
         * data records (maximum 12 satellites) */

        /* msg 3 - reference station parameters */
        struct rtcm2_msg3 {
            struct {
                unsigned int        parity:6;
                unsigned int        x_h:24;
                unsigned int        _pad:2;
            } w3;
            struct {
                unsigned int        parity:6;
                unsigned int        y_h:16;
                unsigned int        x_l:8;
                unsigned int        _pad:2;
            } w4;
            struct {
                unsigned int        parity:6;
                unsigned int        z_h:8;
                unsigned int        y_l:16;
                unsigned int        _pad:2;
            } w5;
            struct {
                unsigned int        parity:6;
                unsigned int        z_l:24;
                unsigned int        _pad:2;
            } w6;
        } type3;

        /* msg 4 - reference station datum */
        struct rtcm2_msg4 {
            struct {
                unsigned int        parity:6;
                unsigned int        datum_alpha_char2:8;
                unsigned int        datum_alpha_char1:8;
                unsigned int        spare:4;
                unsigned int        dat:1;
                unsigned int        dgnss:3;
                unsigned int        _pad:2;
            } w3;
            struct {
                unsigned int        parity:6;
                unsigned int        datum_sub_div_char2:8;
                unsigned int        datum_sub_div_char1:8;
                unsigned int        datum_sub_div_char3:8;
                unsigned int        _pad:2;
            } w4;
            struct {
                unsigned int        parity:6;
                unsigned int        dy_h:8;
                unsigned int        dx:16;
                unsigned int        _pad:2;
            } w5;
            struct {
                unsigned int        parity:6;
                unsigned int        dz:24;
                unsigned int        dy_l:8;
                unsigned int        _pad:2;
            } w6;
        } type4;

        /* msg 5 - constellation health */
        struct rtcm2_msg5 {
            struct b_health_t {
                unsigned int        parity:6;
                unsigned int        unassigned:2;
                unsigned int        time_unhealthy:4;
                unsigned int        loss_warn:1;
                unsigned int        new_nav_data:1;
                unsigned int        health_enable:1;
                unsigned int        cn0:5;
                unsigned int        data_health:3;
                unsigned int        issue_of_data_link:1;
                unsigned int        sat_id:5;
                unsigned int        reserved:1;
                unsigned int        _pad:2;
            } health[MAXHEALTH];
        } type5;

        /* msg 6 - null message */

        /* msg 7 - beacon almanac */
        struct rtcm2_msg7 {
            struct b_station_t {
                struct {
                    unsigned int    parity:6;
                    int             lon_h:8;
                    int             lat:16;
                    unsigned int    _pad:2;
                } w3;
                struct {
                    unsigned int    parity:6;
                    unsigned int    freq_h:6;
                    unsigned int    range:10;
                    unsigned int    lon_l:8;
                    unsigned int    _pad:2;
                } w4;
                struct {
                    unsigned int    parity:6;
                    unsigned int    encoding:1;
                    unsigned int    sync_type:1;
                    unsigned int    mod_mode:1;
                    unsigned int    bit_rate:3;
                    /*
                     * ITU-R M.823-2 page 9 and RTCM-SC104 v2.1 pages
                     * 4-21 and 4-22 are in conflict over the next two
                     * field sizes.  ITU says 9+3, RTCM says 10+2.
                     * The latter correctly decodes the USCG station
                     * id's so I'll use that one here. -wsr
                     */
                    unsigned int    station_id:10;
                    unsigned int    health:2;
                    unsigned int    freq_l:6;
                    unsigned int    _pad:2;
                } w5;
            } almanac[(RTCM2_WORDS_MAX - 2)/3];
        } type7;

        // msg 8 - Pseudolite almanac

        // msg 9 - GPS Partial correction set

        // msg 10 - P-code differential corrections

        // msg 11 - C/A-code L1, L2 delta corrections

        // msg 12 - Pseudolite station parameters.

        /* msg 13 - Ground Transmitter Parameters (RTCM2.3 only) */
        struct rtcm2_msg13 {
            struct {
                unsigned int        parity:6;
                int                 lat:16;
                unsigned int        reserved:6;
                unsigned int        rangeflag:1;
                unsigned int        status:1;
                unsigned int        _pad:2;
            } w1;
            struct {
                unsigned int        parity:6;
                unsigned int        range:8;
                int                 lon:16;
                unsigned int        _pad:2;
            } w2;
        } type13;

        /* msg 14 - GPS Time of Week (RTCM2.3 only) */
        struct rtcm2_msg14 {
            struct {
                unsigned int        parity:6;
                unsigned int        leapsecs:6;
                unsigned int        hour:8;
                unsigned int        week:10;
                unsigned int        _pad:2;
            } w1;
        } type14;

        // msg 15 - Ionospheric delay message

        /* msg 16 - text msg */
        struct rtcm2_msg16 {
            struct {
                unsigned int        parity:6;
                unsigned int        byte3:8;
                unsigned int        byte2:8;
                unsigned int        byte1:8;
                unsigned int        _pad:2;
            } txt[RTCM2_WORDS_MAX-2];
        } type16;

        // msg 17 - GPS ephmerides.  RTCM 2.1

        // msg 18 - RTK uncorrected carrier phases.  RTCM 2.1
        struct rtcm2_msg18 {
            unsigned int        parity:6;
            unsigned int        tom:20;
            unsigned int        r:2;
            unsigned int        f:2;
            unsigned int        _pad:2;
            struct {
                unsigned int        parity:6;
                unsigned int        cp_h:8;
                unsigned int        clc:5;
                unsigned int        dq:3;
                unsigned int        ident:5;
                unsigned int        g:1;
                unsigned int        pc:1;
                unsigned int        m:1;
                unsigned int        _pad:2;
                unsigned int        parity1:6;
                unsigned int        cp_l:24;
                unsigned int        _pad1:2;
            } sat[15];
        } type18;

        // msg 19 - RTK uncorrected psuedoranges.  RTCM 2.1
        struct rtcm2_msg19 {
            unsigned int        parity:6;
            unsigned int        tom:20;
            unsigned int        sm:2;
            unsigned int        f:2;
            unsigned int        _pad:2;
            struct {
                unsigned int        parity:6;
                unsigned int        pr_h:8;
                unsigned int        me:4;
                unsigned int        dq:4;
                unsigned int        ident:5;
                unsigned int        g:1;
                unsigned int        pc:1;
                unsigned int        m:1;
                unsigned int        _pad:2;
                unsigned int        parity1:6;

                unsigned int        pr_l:24;
                unsigned int        _pad1:2;
            } sat[15];
        } type19;

        // msg 20 - RTK carrier phase corrections.  RTCM 2.1
        struct rtcm2_msg20 {
            unsigned int        parity:6;
            unsigned int        tom:20;
            unsigned int        r:2;
            unsigned int        f:2;
            unsigned int        _pad:2;
            struct {
                unsigned int        parity:6;
                unsigned int        iod:8;
                unsigned int        clc:5;
                unsigned int        dq:3;
                unsigned int        ident:5;
                unsigned int        g:1;
                unsigned int        pc:1;
                unsigned int        m:1;
                unsigned int        _pad:2;
                unsigned int        parity1:6;
                unsigned int        cpc:24;
                unsigned int        _pad1:2;
            } sat[RTCM2_WORDS_MAX / 2];
        } type20;

        // msg 21 - RTK/high accuracy psuedorange corrections.  RTCM 2.1
        struct rtcm2_msg21 {
            unsigned int        parity:6;
            unsigned int        tom:20;
            unsigned int        sm:2;
            unsigned int        f:2;
            unsigned int        _pad:2;
        } type21;

        // msg 22 - Extended reference station parameters
        struct rtcm2_msg22 {
            unsigned int        parity:6;
            int                 ecef_dz:8;
            int                 ecef_dy:8;
            int                 ecef_dx:8;
            unsigned int        _pad:2;
            // word 4
            unsigned int        parity1:6;
            unsigned int        ah:18;
            unsigned int        nh:1;
            unsigned int        ap:1;
            unsigned int        at:1;
            unsigned int        gs:1;
            unsigned int        res:2;
            unsigned int        _pad1:2;
            // word 5
            unsigned int        parity2:6;
            int                 ecef_dz2:8;
            int                 ecef_dy2:8;
            int                 ecef_dx2:8;
            unsigned int        _pad2:2;
        } type22;

        // msg 23 - Type of Antenna.  RTCM 2.3
        struct rtcm2_msg23 {
            struct {
                unsigned int        parity:6;
                unsigned char       byte2:8;
                unsigned char       byte1:8;
                unsigned char       byte0:8;
                unsigned int        _pad:2;
            } words[RTCM2_WORDS_MAX - 2];
        } type23;

        // msg 24 - Reference station ARP..  RTCM 2.3
        struct rtcm2_msg24 {
            struct {
                unsigned int        parity:6;
                unsigned int        x_h:24;
                unsigned int        _pad:2;
            } w3;
            struct {
                unsigned int        parity:6;
                unsigned int        y_h:8;
                unsigned int        r:2;
                unsigned int        x_l:14;
                unsigned int        _pad:2;
            } w4;
            struct {
                unsigned int        parity:6;
                unsigned int        y_m:24;
                unsigned int        _pad:2;
            } w5;
            struct {
                unsigned int        parity:6;
                unsigned int        z_h:16;
                unsigned int        r:2;
                unsigned int        y_l:6;
                unsigned int        _pad:2;
            } w6;
            struct {
                unsigned int        parity:6;
                unsigned int        ah:1;
                unsigned int        gs:1;
                unsigned int        z_l:22;
                unsigned int        _pad:2;
            } w7;
            struct {
                unsigned int        parity:6;
                unsigned int        res:6;
                unsigned int        ah:18;
                unsigned int        _pad:2;
            } w8;
        } type24;

        // msg 27 -  Extended almanac of DGPS.

        /* msg 31 - differential GLONASS corrections */
        struct rtcm2_msg31 {
            struct glonass_correction_t {
                struct {                        /* msg 1 word 3 */
                    unsigned int    parity:6;
                    int             prc1:16;
                    unsigned int    satident1:5;        /* satellite ID */
                    unsigned int    udre1:2;
                    unsigned int    scale1:1;
                    unsigned int    _pad:2;
                } w3;

                struct {                        /* msg 1 word 4 */
                    unsigned int    parity:6;
                    unsigned int    satident2:5;        /* satellite ID */
                    unsigned int    udre2:2;
                    unsigned int    scale2:1;
                    unsigned int    tod1:7;
                    unsigned int    change1:1;
                    int             rrc1:8;
                    unsigned int    _pad:2;
                } w4;

                struct {                        /* msg 1 word 5 */
                    unsigned int    parity:6;
                    int             rrc2:8;
                    int             prc2:16;
                    unsigned int    _pad:2;
                } w5;

                struct {                        /* msg 1 word 6 */
                    unsigned int    parity:6;
                    int             prc3_h:8;
                    unsigned int    satident3:5;        /* satellite ID */
                    unsigned int    udre3:2;
                    unsigned int    scale3:1;
                    unsigned int    tod2:7;
                    unsigned int    change2:1;
                    unsigned int    _pad:2;
                } w6;

                struct {                        /* msg 1 word 7 */
                    unsigned int    parity:6;
                    unsigned int    tod3:7;
                    unsigned int    change3:1;
                    int             rrc3:8;
                    /* NOTE: unsigned int for low byte */
                    unsigned int    prc3_l:8;
                    unsigned int    _pad:2;
                } w7;
            } corrections[(RTCM2_WORDS_MAX - 2) / 5];
        } type31;

        // msg 32 - Differential GLONASS reference station parameters, RTCM 2.3

        // msg 33 - GLONASS constellation health, RTCM 2.3

        // msg 34 - DGLONASS corrections, RTCM 2.3

        // msg 35 - GLONASS radiobeacon almanac, RTCM 2.3

        // msg 36 - GLONASS special message, RTCM 2.3

        // msg 37 - GNSS system time offset, RTCM 2.3

        /* msg 59 -  Proprietary messages, for transmission of any
         * required data */

        // msg 60-63 - Multipurpose usage

        /* unknown message */
        isgps30bits_t   rtcm2_msgunk[RTCM2_WORDS_MAX - 2];
    } msg_type;
} __attribute__((__packed__));

#endif /* LITTLE_ENDIAN */

#ifdef WORDS_BIGENDIAN

struct rtcm2_msg_t {
    struct rtcm2_msghw1 {                       /* header word 1 */
        unsigned int            _pad:2;
        unsigned int            preamble:8;     /* fixed at 01100110 */
        unsigned int            msgtype:6;      /* RTCM message type */
        unsigned int            refstaid:10;    /* reference station ID */
        unsigned int            parity:6;
    } w1;

    struct rtcm2_msghw2 {                       /* header word 2 */
        unsigned int            _pad:2;
        unsigned int            zcnt:13;
        unsigned int            sqnum:3;
        unsigned int            frmlen:5;
        unsigned int            stathlth:3;     /* station health */
        unsigned int            parity:6;
    } w2;

    union {
        /* msg 1 - differential GPS corrections */
        struct rtcm2_msg1 {
            struct gps_correction_t {
                struct {                        /* msg 1 word 3 */
                    unsigned int    _pad:2;
                    unsigned int    scale1:1;
                    unsigned int    udre1:2;
                    unsigned int    satident1:5;  /* satellite ID */
                    int             prc1:16;
                    unsigned int    parity:6;
                } w3;

                struct {                        /* msg 1 word 4 */
                    unsigned int    _pad:2;
                    int             rrc1:8;
                    unsigned int    iod1:8;
                    unsigned int    scale2:1;
                    unsigned int    udre2:2;
                    unsigned int    satident2:5;        /* satellite ID */
                    unsigned int    parity:6;
                } w4;

                struct {                        /* msg 1 word 5 */
                    unsigned int    _pad:2;
                    int             prc2:16;
                    int             rrc2:8;
                    unsigned int    parity:6;
                } w5;

                struct {                        /* msg 1 word 6 */
                    unsigned int    _pad:2;
                    unsigned int    iod2:8;
                    unsigned int    scale3:1;
                    unsigned int    udre3:2;
                    unsigned int    satident3:5;        /* satellite ID */
                    int             prc3_h:8;
                    unsigned int    parity:6;
                } w6;

                struct {                        /* msg 1 word 7 */
                    unsigned int    _pad:2;
                    unsigned int    prc3_l:8;   // NOTE: unsigned for low byte
                    int             rrc3:8;
                    unsigned int    iod3:8;
                    unsigned int    parity:6;
                } w7;
            } corrections[(RTCM2_WORDS_MAX - 2) / 5];
        } type1;

        /* msg 2 -  Pseudo-range corrections, referring to previous orbit
         * data records (maximum 12 satellites) */

        /* msg 3 - reference station parameters */
        struct rtcm2_msg3 {
            struct {
                unsigned int        _pad:2;
                unsigned int        x_h:24;
                unsigned int        parity:6;
            } w3;
            struct {
                unsigned int        _pad:2;
                unsigned int        x_l:8;
                unsigned int        y_h:16;
                unsigned int        parity:6;
            } w4;
            struct {
                unsigned int        _pad:2;
                unsigned int        y_l:16;
                unsigned int        z_h:8;
                unsigned int        parity:6;
            } w5;
            struct {
                unsigned int        _pad:2;
                unsigned int        z_l:24;
                unsigned int        parity:6;
            } w6;
        } type3;

        /* msg 4 - reference station datum */
        struct rtcm2_msg4 {
            struct {
                unsigned int        _pad:2;
                unsigned int        dgnss:3;
                unsigned int        dat:1;
                unsigned int        spare:4;
                unsigned int        datum_alpha_char1:8;
                unsigned int        datum_alpha_char2:8;
                unsigned int        parity:6;
            } w3;
            struct {
                unsigned int        _pad:2;
                unsigned int        datum_sub_div_char3:8;
                unsigned int        datum_sub_div_char1:8;
                unsigned int        datum_sub_div_char2:8;
                unsigned int        parity:6;
            } w4;
            struct {
                unsigned int        _pad:2;
                unsigned int        dx:16;
                unsigned int        dy_h:8;
                unsigned int        parity:6;
            } w5;
            struct {
                unsigned int        _pad:2;
                unsigned int        dy_l:8;
                unsigned int        dz:24;
                unsigned int        parity:6;
            } w6;
        } type4;

        /* msg 5 - constellation health */
        struct rtcm2_msg5 {
            struct b_health_t {
                unsigned int        _pad:2;
                unsigned int        reserved:1;
                unsigned int        sat_id:5;
                unsigned int        issue_of_data_link:1;
                unsigned int        data_health:3;
                unsigned int        cn0:5;
                unsigned int        health_enable:1;
                unsigned int        new_nav_data:1;
                unsigned int        loss_warn:1;
                unsigned int        time_unhealthy:4;
                unsigned int        unassigned:2;
                unsigned int        parity:6;
            } health[MAXHEALTH];
        } type5;

        // msg - 6 -  Null message, used as filler record during time-outs

        /* msg 7 - beacon almanac */
        struct rtcm2_msg7 {
            struct b_station_t {
                struct {
                    unsigned int    _pad:2;
                    int             lat:16;
                    int             lon_h:8;
                    unsigned int    parity:6;
                } w3;
                struct {
                    unsigned int    _pad:2;
                    unsigned int    lon_l:8;
                    unsigned int    range:10;
                    unsigned int    freq_h:6;
                    unsigned int    parity:6;
                } w4;
                struct {
                    unsigned int    _pad:2;
                    unsigned int    freq_l:6;
                    unsigned int    health:2;
                    unsigned int    station_id:10;
                             /* see comments in LE struct above. */
                    unsigned int    bit_rate:3;
                    unsigned int    mod_mode:1;
                    unsigned int    sync_type:1;
                    unsigned int    encoding:1;
                    unsigned int    parity:6;
                } w5;
            } almanac[(RTCM2_WORDS_MAX - 2)/3];
        } type7;

        // msg 8 - Pseudolite almanac

        // msg 9 - GPS Partial correction set

        // msg 10 - P-code differential corrections

        // msg 11 - C/A-code L1, L2 delta corrections

        // msg 12 - Pseudolite station parameters.

        /* msg 13 - Ground Transmitter Parameters (RTCM2.3 only) */
        struct rtcm2_msg13 {
            struct {
                unsigned int        _pad:2;
                unsigned int        status:1;
                unsigned int        rangeflag:1;
                unsigned int        reserved:6;
                int                 lat:16;
                unsigned int        parity:6;
            } w1;
            struct {
                unsigned int        _pad:2;
                int                 lon:16;
                unsigned int        range:8;
                unsigned int        parity:6;
            } w2;
        } type13;

        /* msg 14 - GPS Time of Week (RTCM2.3 only) */
        struct rtcm2_msg14 {
            struct {
                unsigned int        _pad:2;
                unsigned int        week:10;
                unsigned int        hour:8;
                unsigned int        leapsecs:6;
                unsigned int        parity:6;
            } w1;
        } type14;

        // msg 15 - Ionospheric delay message

        /* msg 16 - text msg */
        struct rtcm2_msg16 {
            struct {
                unsigned int        _pad:2;
                unsigned int        byte1:8;
                unsigned int        byte2:8;
                unsigned int        byte3:8;
                unsigned int        parity:6;
            } txt[RTCM2_WORDS_MAX-2];
        } type16;

        // msg 17 - GPS ephmerides.  RTCM 2.1

        // msg 18 - RTK uncorrected carrier phases.  RTCM 2.1
        struct rtcm2_msg18 {
            unsigned int        _pad:2;
            unsigned int        f:2;
            unsigned int        r:2;
            unsigned int        tom:20;
            unsigned int        parity:6;
            struct {
                unsigned int        _pad:2;
                unsigned int        m:1;
                unsigned int        pc:1;
                unsigned int        g:1;
                unsigned int        dq:3;
                unsigned int        ident:5;
                unsigned int        clc:5;
                unsigned int        cp_h:8;
                unsigned int        parity:6;
                unsigned int        _pad:2;
                unsigned int        cp_l:24;
                unsigned int        parity:6;
            } sat[15];
        } type18;

        // msg 19 - RTK uncorrected psuedoranges.  RTCM 2.1
        struct rtcm2_msg19 {
            unsigned int        _pad:2;
            unsigned int        f:2;
            unsigned int        sm:2;
            unsigned int        tom:20;
            unsigned int        parity:6;
            struct {
                unsigned int        _pad:2;
                unsigned int        m:1;
                unsigned int        pc:1;
                unsigned int        g:1;
                unsigned int        ident:5;
                unsigned int        dq:4;
                unsigned int        me:4;
                unsigned int        pr_h:8;
                unsigned int        parity:6;

                unsigned int        _pad1:2;
                unsigned int        pr_l:24;
                unsigned int        parity1:6;
            } sat[15];
        } type19;

        // msg 20 - RTK carrier phase corrections.  RTCM 2.1
        struct rtcm2_msg20 {
            unsigned int        _pad:2;
            unsigned int        f:2;
            unsigned int        r:2;
            unsigned int        tom:20;
            unsigned int        parity:6;
            struct {
                unsigned int        _pad:2;
                unsigned int        m:1;
                unsigned int        pc:1;
                unsigned int        g:1;
                unsigned int        dq:3;
                unsigned int        ident:5;
                unsigned int        clc:5;
                unsigned int        iod:8;
                unsigned int        parity:6;
                unsigned int        _pad:2;
                unsigned int        cpc:24;
                unsigned int        parity:6;
            } sat[RTCM2_WORDS_MAX / 2];
        } type20;

        // msg 21 - RTK/high accuracy psuedorange corrections.  RTCM 2.1
        struct rtcm2_msg21 {
            unsigned int        _pad:2;
            unsigned int        f:2;
            unsigned int        sm:2;
            unsigned int        tom:20;
            unsigned int        parity:6;
        } type21;

        // msg 22 - Extended reference station parameters
        struct rtcm2_msg22 {
            unsigned int        _pad:2;
            int                 ecef_dx:8;
            int                 ecef_dy:8;
            int                 ecef_dz:8;
            unsigned int        parity:6;
            // word 4
            unsigned int        _pad1:2;
            unsigned int        res:2;
            unsigned int        gs:1;
            unsigned int        at:1;
            unsigned int        ap:1;
            unsigned int        nh:1;
            unsigned int        ah:18;
            unsigned int        parity1:6;
            // word 5
            unsigned int        _pad2:2;
            int                 ecef_dx2:8;
            int                 ecef_dy2:8;
            int                 ecef_dz2:8;
            unsigned int        parity2:6;
        } type22;

        // msg 23 - Type of Antenna.  RTCM 2.3
        struct rtcm2_msg23 {
            struct {
                unsigned int        _pad:2;
                unsigned char       byte0:8;
                unsigned char       byte1:8;
                unsigned char       byte2:8;
                unsigned int        parity:6;
            } words[RTCM2_WORDS_MAX - 2];
        } type23;

        // msg 24 - Reference station ARP..  RTCM 2.3
        struct rtcm2_msg24 {
            struct {
                unsigned int        _pad:2;
                unsigned int        x_h:24;
                unsigned int        parity:6;
            } w3;
            struct {
                unsigned int        _pad:2;
                unsigned int        x_l:14;
                unsigned int        r:2;
                unsigned int        y_h:8;
                unsigned int        parity:6;
            } w4;
            struct {
                unsigned int        _pad:2;
                unsigned int        y_m:24;
                unsigned int        parity:6;
            } w5;
            struct {
                unsigned int        _pad:2;
                unsigned int        y_l:6;
                unsigned int        r:2;
                unsigned int        z_h:16;
                unsigned int        parity:6;
            } w6;
            struct {
                unsigned int        _pad:2;
                unsigned int        z_l:22;
                unsigned int        gs:1;
                unsigned int        ah:1;
                unsigned int        parity:6;
            } w7;
            struct {
                unsigned int        _pad:2;
                unsigned int        y_m:18;
                unsigned int        res:6;
                unsigned int        parity:6;
            } w8;
        } type24;

        // msg 27 -  Extended almanac of DGPS.

        /* msg 31 - differential GLONASS corrections */
        struct rtcm2_msg31 {
            struct glonass_correction_t {
                struct {                        /* msg 1 word 3 */
                    unsigned int    _pad:2;
                    unsigned int    scale1:1;
                    unsigned int    udre1:2;
                    unsigned int    satident1:5;        /* satellite ID */
                    int             prc1:16;
                    unsigned int    parity:6;
                } w3;

                struct {                        /* msg 1 word 4 */
                    unsigned int    _pad:2;
                    int             rrc1:8;
                    unsigned int    change1:1;
                    unsigned int    tod1:7;
                    unsigned int    scale2:1;
                    unsigned int    udre2:2;
                    unsigned int    satident2:5;        /* satellite ID */
                    unsigned int    parity:6;
                } w4;

                struct {                        /* msg 1 word 5 */
                    unsigned int    _pad:2;
                    int             prc2:16;
                    int             rrc2:8;
                    unsigned int    parity:6;
                } w5;

                struct {                        /* msg 1 word 6 */
                    unsigned int    _pad:2;
                    unsigned int    change2:1;
                    unsigned int    tod2:7;
                    unsigned int    scale3:1;
                    unsigned int    udre3:2;
                    unsigned int    satident3:5;        /* satellite ID */
                    int             prc3_h:8;
                    unsigned int    parity:6;
                } w6;

                struct {                        /* msg 1 word 7 */
                    unsigned int    _pad:2;
                    unsigned int    prc3_l:8;   // NOTE: unsigned for low byte
                    int             rrc3:8;
                    unsigned int    change3:1;
                    unsigned int    tod3:7;
                    unsigned int    parity:6;
                } w7;
            } corrections[(RTCM2_WORDS_MAX - 2) / 5];
        } type31;

        // msg 32 - Differential GLONASS reference station parameters, RTCM 2.3

        // msg 33 - GLONASS constellation health, RTCM 2.3

        // msg 34 - DGLONASS corrections, RTCM 2.3

        // msg 35 - GLONASS radiobeacon almanac, RTCM 2.3

        // msg 36 - GLONASS special message, RTCM 2.3

        // msg 37 - GNSS system time offset, RTCM 2.3

        /* msg 59 -  Proprietary messages, for transmission of any
         * required data */

        // msg 60-63 - Multipurpsoe usage

        /* unknown message */
        isgps30bits_t   rtcm2_msgunk[RTCM2_WORDS_MAX-2];
    } msg_type;
} __attribute__((__packed__));

#endif /* BIG ENDIAN */

#ifdef RTCM104V2_ENABLE

#define PREAMBLE_PATTERN 0x66

static unsigned int tx_speed[] = { 25, 50, 100, 110, 150, 200, 250, 300 };

#define DIMENSION(a) (unsigned)(sizeof(a)/sizeof(a[0]))

/* break out the raw bits into the content fields */
void rtcm2_unpack(struct gps_device_t *session, struct rtcm2_t *tp, char *buf)
{
    int len;
    unsigned int n, w;
    struct rtcm2_msg_t *msg = (struct rtcm2_msg_t *)buf;
    bool unknown = true;              // we don't know how to decode
    const char *msg_name = NULL;  // no decode, but maybe we know the name

    tp->type = msg->w1.msgtype;         // 1 to 64.  Zero means 64
    tp->refstaid = msg->w1.refstaid;    // 0 to 1023
    tp->length = msg->w2.frmlen;        // 0 to 31
    tp->zcount = msg->w2.zcnt * ZCOUNT_SCALE;  // 0 to 3599.4 sec
    tp->seqnum = msg->w2.sqnum;         // 0 to 7
    tp->stathlth = msg->w2.stathlth;

    // clear rtk struct
    memset(&tp->rtk, 0, sizeof(tp->rtk));

    // clear ref_sta struct
    memset(&tp->ref_sta, 0, sizeof(tp->ref_sta));
    tp->ref_sta.x = NAN;
    tp->ref_sta.y = NAN;
    tp->ref_sta.z = NAN;
    tp->ref_sta.dx = NAN;
    tp->ref_sta.dy = NAN;
    tp->ref_sta.dz = NAN;
    tp->ref_sta.ah = NAN;
    tp->ref_sta.dx2 = NAN;
    tp->ref_sta.dy2 = NAN;
    tp->ref_sta.dz2 = NAN;

    /* FIXME: test tp->length + 2, against session->lexer.isgps.buflen, */

    // len does not include 2 byte header
    len = (int)tp->length;
    n = 0;
    switch (tp->type) {
    case 1:
        // FALLTHROUGH
    case 9:
    {
        struct gps_correction_t *m = &msg->msg_type.type1.corrections[0];
        msg_name = "Differential GPS Corrections";
        unknown = false;

        while (len >= 0) {
            if (len >= 2) {
                tp->gps_ranges.sat[n].ident = m->w3.satident1;
                tp->gps_ranges.sat[n].udre = m->w3.udre1;
                tp->gps_ranges.sat[n].iod = m->w4.iod1;
                tp->gps_ranges.sat[n].prc = m->w3.prc1 *
                    (m->w3.scale1 ? PRCLARGE : PRCSMALL);
                tp->gps_ranges.sat[n].rrc = m->w4.rrc1 *
                    (m->w3.scale1 ? RRLARGE : RRSMALL);
                n++;
            }
            if (len >= 4) {
                tp->gps_ranges.sat[n].ident = m->w4.satident2;
                tp->gps_ranges.sat[n].udre = m->w4.udre2;
                tp->gps_ranges.sat[n].iod = m->w6.iod2;
                tp->gps_ranges.sat[n].prc = m->w5.prc2 *
                    (m->w4.scale2 ? PRCLARGE : PRCSMALL);
                tp->gps_ranges.sat[n].rrc = m->w5.rrc2 *
                    (m->w4.scale2 ? RRLARGE : RRSMALL);
                n++;
            }
            if (len >= 5) {
                tp->gps_ranges.sat[n].ident = m->w6.satident3;
                tp->gps_ranges.sat[n].udre = m->w6.udre3;
                tp->gps_ranges.sat[n].iod = m->w7.iod3;
                tp->gps_ranges.sat[n].prc =
                    ((m->w6.prc3_h << 8) | (m->w7.prc3_l)) *
                    (m->w6.scale3 ? PRCLARGE : PRCSMALL);
                tp->gps_ranges.sat[n].rrc =
                    m->w7.rrc3 * (m->w6.scale3 ? RRLARGE : RRSMALL);
                n++;
            }
            len -= 5;
            m++;
        }
        tp->gps_ranges.nentries = n;
    }
        break;

    case 2:
        msg_name = "Delta Differential GPS Corrections";
        break;

    case 3:
        {
            struct rtcm2_msg3 *m = &msg->msg_type.type3;
            msg_name = "Reference Station Parameters (GPS)";
            unknown = false;

            if ((tp->ref_sta.valid = len >= 4)) {
                tp->ref_sta.x = ((m->w3.x_h << 8) | (m->w4.x_l)) * XYZ_SCALE;
                tp->ref_sta.y = ((m->w4.y_h << 16) | (m->w5.y_l)) * XYZ_SCALE;
                tp->ref_sta.z = ((m->w5.z_h << 24) | (m->w6.z_l)) * XYZ_SCALE;
            }
        }
        break;

    case 4:
        msg_name = "Reference Station Datum";
        unknown = false;
        if ((tp->reference.valid = len >= 2)) {
            struct rtcm2_msg4 *m = &msg->msg_type.type4;

            tp->reference.system =
                (m->w3.dgnss == 0) ? NAVSYSTEM_GPS :
                ((m->w3.dgnss == 1) ? NAVSYSTEM_GLONASS : NAVSYSTEM_UNKNOWN);
            tp->reference.sense =
                (m->w3.dat != 0) ? SENSE_GLOBAL : SENSE_LOCAL;
            if (m->w3.datum_alpha_char1) {
                tp->reference.datum[n++] = (char)(m->w3.datum_alpha_char1);
            }
            if (m->w3.datum_alpha_char2) {
                tp->reference.datum[n++] = (char)(m->w3.datum_alpha_char2);
            }
            if (m->w4.datum_sub_div_char1) {
                tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char1);
            }
            if (m->w4.datum_sub_div_char2) {
                tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char2);
            }
            if (m->w4.datum_sub_div_char3) {
                tp->reference.datum[n++] = (char)(m->w4.datum_sub_div_char3);
            }
            /* we used to say n++ here, but scan-build complains */
            tp->reference.datum[n] = '\0';
            if (len >= 4) {
                tp->reference.dx = m->w5.dx * DXYZ_SCALE;
                tp->reference.dy =
                    ((m->w5.dy_h << 8) | m->w6.dy_l) * DXYZ_SCALE;
                tp->reference.dz = m->w6.dz * DXYZ_SCALE;
            } else
                tp->reference.sense = SENSE_INVALID;
        }
        break;

    case 5:
        msg_name = "Constellation health (GPS)";
        unknown = false;
        for (n = 0; n < (unsigned)len; n++) {
            struct consat_t *csp = &tp->conhealth.sat[n];
            struct b_health_t *m = &msg->msg_type.type5.health[n];

            csp->ident = m->sat_id;
            csp->iodl = m->issue_of_data_link != 0;
            csp->health = m->data_health;
            csp->snr = (int)(m->cn0 ? (m->cn0 + CNR_OFFSET) : SNR_BAD);
            csp->health_en = m->health_enable != 0;
            csp->new_data = m->new_nav_data != 0;
            csp->los_warning = m->loss_warn != 0;
            csp->tou = m->time_unhealthy * TU_SCALE;
        }
        tp->conhealth.nentries = n;
        break;

    case 6:
        msg_name = "Null frame (GPS)";
        unknown = false;
        break;

    case 7:
        msg_name = "Beacon Almanac (GPS)";
        unknown = false;
        for (w = 0; w < (unsigned)len; w++) {
            struct station_t *np = &tp->almanac.station[n];
            struct b_station_t *mp = &msg->msg_type.type7.almanac[w];

            np->latitude = mp->w3.lat * LA_SCALE;
            np->longitude = ((mp->w3.lon_h << 8) | mp->w4.lon_l) * LO_SCALE;
            np->range = mp->w4.range;
            np->frequency =
                (((mp->w4.freq_h << 6) | mp->w5.freq_l) * FREQ_SCALE) +
                FREQ_OFFSET;
            np->health = mp->w5.health;
            np->station_id = mp->w5.station_id,
                np->bitrate = tx_speed[mp->w5.bit_rate];
            n++;
        }
        tp->almanac.nentries = (unsigned)(len / 3);
        break;

    case 8:
        msg_name = "Pseudolite Almanac";
        break;

    // case 9 is handled above with case 1

    case 10:
        msg_name = "P-Code Differential Corrections";
        break;

    case 11:
        msg_name = "C/A Code L2 Corrections";
        break;

    case 12:
        msg_name = "Pseudolite Station Parameters";
        break;

    case 13:
        msg_name = "Ground Transmitter Parameters";
        unknown = false;
        tp->xmitter.status = (bool)msg->msg_type.type13.w1.status;
        tp->xmitter.rangeflag = (bool)msg->msg_type.type13.w1.rangeflag;
        tp->xmitter.lat = msg->msg_type.type13.w1.lat * LATLON_SCALE;
        tp->xmitter.lon = msg->msg_type.type13.w2.lon * LATLON_SCALE;
        tp->xmitter.range = msg->msg_type.type13.w2.range * RANGE_SCALE;
        if (tp->xmitter.range == 0)
            tp->xmitter.range = 1024;
        break;

    case 14:
        msg_name = "GPS Time Of Week";
        unknown = false;
        tp->gpstime.week = msg->msg_type.type14.w1.week;
        tp->gpstime.hour = msg->msg_type.type14.w1.hour;
        tp->gpstime.leapsecs = msg->msg_type.type14.w1.leapsecs;
        break;

    case 15:
        msg_name = "Ionospheric Delay Message";
        break;

    case 16:
        msg_name = "Special Message (GPS)";
        unknown = false;
        for (w = 0; w < (unsigned)len; w++) {
            if (!msg->msg_type.type16.txt[w].byte1) {
                break;
            }
            tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte1);
            if (!msg->msg_type.type16.txt[w].byte2) {
                break;
            }
            tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte2);
            if (!msg->msg_type.type16.txt[w].byte3) {
                break;
            }
            tp->message[n++] = (char)(msg->msg_type.type16.txt[w].byte3);
        }
        tp->message[n] = '\0';
        break;

    case 17:
        msg_name = "GPS Ephemeris";
        break;

    case 18:
        msg_name = "RTK Uncorrected Carrier-phase";
        unknown = false;
        // WIP: partial decode
        if (1 > len) {
            // too short
            break;
        }
        {
            struct rtcm2_msg18 *m = &msg->msg_type.type18;
            tp->rtk.nentries = (len - 1) / 2;
            unsigned i;

            tp->rtk.tom = m->tom;
            tp->rtk.f = m->f;
            for (i = 0; i < tp->rtk.nentries; i++) {
                tp->rtk.sat[i].m = m->sat[i].m;
                tp->rtk.sat[i].pc = m->sat[i].pc;
                tp->rtk.sat[i].g = m->sat[i].g;
                tp->rtk.sat[i].ident = m->sat[i].ident;
                tp->rtk.sat[i].dq = m->sat[i].dq;
                tp->rtk.sat[i].carrier_phase = m->sat[i].cp_l;
                tp->rtk.sat[i].carrier_phase |= m->sat[i].cp_h << 24;
                tp->rtk.sat[i].clc = m->sat[i].clc;
            }
        }
        break;

    case 19:
        msg_name = "RTK Corrected Pseudorange";
        // WIP: partial decode
        if (1 > len) {
            // too short
            break;
        }
        {
            struct rtcm2_msg19 *m = &msg->msg_type.type19;
            unsigned i;
            tp->rtk.tom = m->tom;
            tp->rtk.f = m->f;
            tp->rtk.sm = m->sm;
            tp->rtk.nentries = (len - 1) / 2;

            for (i = 0; i < tp->rtk.nentries; i++) {
                tp->rtk.sat[i].m = m->sat[i].m;
                tp->rtk.sat[i].pc = m->sat[i].pc;
                tp->rtk.sat[i].g = m->sat[i].g;
                tp->rtk.sat[i].ident = m->sat[i].ident;
                tp->rtk.sat[i].dq = m->sat[i].dq;
                tp->rtk.sat[i].pseudorange = m->sat[i].pr_l;
                tp->rtk.sat[i].pseudorange |= m->sat[i].pr_h << 24;
            }
        }
        break;

    case 20:
        msg_name = "RTK Carrier Phase Corrections";
        // WIP: partial decode
        if (3 > len) {
            // too short
            break;
        }
        {
            struct rtcm2_msg20 *m = &msg->msg_type.type20;
            tp->rtk.tom = m->tom;
            tp->rtk.f = m->f;
        }
        break;

    case 21:
        msg_name = "High-Accuracy Pseudorange Corrections";
        // WIP: partial decode
        if (3 > len) {
            // too short
            break;
        }
        {
            struct rtcm2_msg21 *m = &msg->msg_type.type21;
            tp->rtk.tom = m->tom;
            tp->rtk.f = m->f;
            tp->rtk.sm = m->sm;
        }
        break;

    case 22:
        // Extends types 3 and 34
        msg_name = "Extended Reference Station Parameters";
        unknown = false;
        // WIP: partial decode
        if (3 > len) {
            // too short
            break;
        }
        // May be length 3, 4 or 5!
        {
            struct rtcm2_msg22 *m = &msg->msg_type.type22;

            tp->ref_sta.dx = m->ecef_dx * EDXYZ_SCALE;
            tp->ref_sta.dy = m->ecef_dy * EDXYZ_SCALE;
            tp->ref_sta.dz = m->ecef_dz * EDXYZ_SCALE;
            if (3 < len) {
                // word 4 present
                tp->ref_sta.gs = m->gs;
                if (0 == m->nh) {
                    tp->ref_sta.ah = m->ah * EDXYZ_SCALE;
                }
            }
            if (4 < len) {
                // word 5 present
                tp->ref_sta.dx2 = m->ecef_dx2 * EDXYZ2_SCALE;
                tp->ref_sta.dy2 = m->ecef_dy2 * EDXYZ2_SCALE;
                tp->ref_sta.dz2 = m->ecef_dz2 * EDXYZ2_SCALE;
            }
        }
        break;

    case 23:
        msg_name = "Antenna Type Definition";
        // WIP
        unknown = false;
        if (1 > len) {
            // too short
            break;
        }
        {
            unsigned sf;
            unsigned nad = 0, nas = 0;
            unsigned char tbuf[RTCM2_WORDS_MAX * 3];
            struct rtcm2_msg23 *m = &msg->msg_type.type23;
            int i = 0, j = 0;

            tp->ref_sta.ar = (m->words[0].byte0 >> 6) & 1;
            sf = (m->words[0].byte0 >> 5) & 1;
            // nad is 0 to 31
            nad = m->words[0].byte0 & 0x1f;

            // crazy packing...  move to simple array first
            for (i = 0; i < len; i++) {
                tbuf[j++] = m->words[i].byte0;
                tbuf[j++] = m->words[i].byte1;
                tbuf[j++] = m->words[i].byte2;
            }
#if __UNUSED__
            // debug code
            {
                char tmpbuf[100];
                GPSD_LOG(LOG_SHOUT, &session->context->errout,
                         "RTCM2: len %d nad %d tbuf %s\n",
                         len, nad,
                         gpsd_hexdump(tmpbuf, sizeof(tmpbuf),
                                      (char *)tbuf, len * 3));
            }
#endif // __UNUSED__
            // skip first byte (AR, SF, NAD)
            memcpy(tp->ref_sta.ant_desc, &tbuf[1], nad);
            tp->ref_sta.ant_desc[nad] = '\0';

            tp->ref_sta.setup_id = tbuf[nad + 1];

            // get serial number
            if (1 == sf) {
                // nas is 0 to 31. just after last ant desc byte
                nas = tbuf[nad + 2] & 0x1f;
                memcpy(tp->ref_sta.ant_serial, &tbuf[nad + 3], nas);
            }
            tp->ref_sta.ant_serial[nas] = '\0';
        }
        break;

    case 24:
        msg_name = "Antenna Reference Point (arp)";
        // WIP
        unknown = false;
        {
            struct rtcm2_msg24 *m = &msg->msg_type.type24;
            // try to prevent int overflow
            long long temp;

            temp = ((long long)m->w3.x_h << 8) | (m->w4.x_l);
            tp->ref_sta.x = temp * EXYZ_SCALE;
            temp = ((long long)m->w4.y_h << 32) |
                    (m->w5.y_m << 6) |
                    (m->w6.y_l);
            tp->ref_sta.y = temp * EXYZ_SCALE;
            temp = ((long long)m->w6.z_h << 22) | (m->w7.z_l);
            tp->ref_sta.z = temp * EXYZ_SCALE;
            if (0 == m->w7.ah) {
                tp->ref_sta.ah = m->w8.ah * EDXYZ_SCALE;
            }
        }
        break;

    case 25:
        // FALLTHROUGH
    case 26:
        msg_name = "Undefined";
        break;

    case 27:
        msg_name = "Radio Beacon Almanac";
        break;

    case 28:
        // FALLTHROUGH
    case 29:
        // FALLTHROUGH
    case 30:
        msg_name = "Undefined";
        break;

    case 31:
        // FALLTHROUGH
    case 34:
    {
        // 32 and 34 are the same, except 31 is all sats, 34 is some sats
        struct glonass_correction_t *m = &msg->msg_type.type31.corrections[0];
        msg_name = "Differential GLONASS corrections";
        unknown = false;

        while (len >= 0) {
            if (len >= 2) {
                tp->glonass_ranges.sat[n].ident = m->w3.satident1;
                tp->glonass_ranges.sat[n].udre = m->w3.udre1;
                tp->glonass_ranges.sat[n].change = (bool)m->w4.change1;
                tp->glonass_ranges.sat[n].tod = m->w4.tod1;
                tp->glonass_ranges.sat[n].prc = m->w3.prc1 *
                    (m->w3.scale1 ? PRCLARGE : PRCSMALL);
                tp->glonass_ranges.sat[n].rrc = m->w4.rrc1 *
                    (m->w3.scale1 ? RRLARGE : RRSMALL);
                n++;
            }
            if (len >= 4) {
                tp->glonass_ranges.sat[n].ident = m->w4.satident2;
                tp->glonass_ranges.sat[n].udre = m->w4.udre2;
                tp->glonass_ranges.sat[n].change = (bool)m->w6.change2;
                tp->glonass_ranges.sat[n].tod = m->w6.tod2;
                tp->glonass_ranges.sat[n].prc = m->w5.prc2 *
                    (m->w4.scale2 ? PRCLARGE : PRCSMALL);
                tp->glonass_ranges.sat[n].rrc = m->w5.rrc2 *
                    (m->w4.scale2 ? RRLARGE : RRSMALL);
                n++;
            }
            if (len >= 5) {
                tp->glonass_ranges.sat[n].ident = m->w6.satident3;
                tp->glonass_ranges.sat[n].udre = m->w6.udre3;
                tp->glonass_ranges.sat[n].change = (bool)m->w7.change3;
                tp->glonass_ranges.sat[n].tod = m->w7.tod3;
                tp->glonass_ranges.sat[n].prc =
                    ((m->w6.prc3_h << 8) | (m->w7.prc3_l)) *
                    (m->w6.scale3 ? PRCLARGE : PRCSMALL);
                tp->glonass_ranges.sat[n].rrc =
                    m->w7.rrc3 * (m->w6.scale3 ? RRLARGE : RRSMALL);
                n++;
            }
            len -= 5;
            m++;
        }
        tp->glonass_ranges.nentries = n;
    }
        break;

    case 32:
        msg_name = "Reference Station Parameters (GLONASS)";
        break;

    case 33:
        msg_name = "Constellation health (GLONASS)";
        break;

    // case 34 is above with 31

    case 35:
        msg_name = "Beacon Almanac (GLONASS)";
        break;

    case 36:
        msg_name = "Special Message (GLONASS)";
        break;

    case 37:
        msg_name = "GNSS System Time Offset";
        break;

    case 59:
        msg_name = "Proprietary Message";
        break;

    default:
        msg_name = "Huh?";
        break;
    }

    if (unknown) {
        memcpy(tp->words, msg->msg_type.rtcm2_msgunk,
               tp->length * sizeof(isgps30bits_t));
        if (NULL == msg_name) {
	    GPSD_LOG(LOG_PROG, &session->context->errout,
		     "RTCM2: type %d (unknown), length %d\n",
		     tp->type, tp->length + 2);
        } else {
	    GPSD_LOG(LOG_PROG, &session->context->errout,
		     "RTCM2: type %d (%s), length %d\n",
                     tp->type, msg_name,tp->length + 2);
        }
    } else {
        GPSD_LOG(LOG_PROG, &session->context->errout,
                 "RTCM2: type %d (%s), length %d\n",
                 tp->type, msg_name, tp->length + 2);
    }
    GPSD_LOG(LOG_RAW, &session->context->errout,
             "RTCM 2: type %d (%s) length %d words "
             "from %zd bytes: %s\n",
             tp->type,
             msg_name,
             tp->length + 2,
             session->lexer.isgps.buflen,
             gpsd_hexdump(session->msgbuf, sizeof(session->msgbuf),
                             (char *)session->lexer.isgps.buf,
                             (tp->length + 2) * sizeof(isgps30bits_t)));
}

static bool preamble_match(isgps30bits_t * w)
{
    return (((struct rtcm2_msghw1 *)w)->preamble == PREAMBLE_PATTERN);
}

static bool length_check(struct gps_lexer_t *lexer)
{
    return lexer->isgps.bufindex >= 2
        && lexer->isgps.bufindex >=
        ((struct rtcm2_msg_t *)lexer->isgps.buf)->w2.frmlen + 2u;
}

enum isgpsstat_t rtcm2_decode(struct gps_lexer_t *lexer, unsigned int c)
{
    return isgps_decode(lexer,
                        preamble_match, length_check, RTCM2_WORDS_MAX, c);
}

#endif /* RTCM104V2_ENABLE */

// vim: set expandtab shiftwidth=4
