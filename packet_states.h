/* edit packet_states.h to add new packet types. */
   GROUND_STATE,	/* we don't know what packet type to expect */

   COMMENT_BODY,	/* pound comment for a test load */
   COMMENT_RECOGNIZED,	/* comment recognized */

#ifdef NMEA_ENABLE
   NMEA_DOLLAR,		/* we've seen first character of NMEA leader */
   NMEA_BANG,		/* we've seen first character of an AIS message '!' */
   NMEA_PUB_LEAD,	/* seen second character of NMEA G leader */
   NMEA_VENDOR_LEAD,	/* seen second character of NMEA P leader */
   NMEA_LEADER_END,	/* seen end char of NMEA leader, in body */
   NMEA_PASHR_A,	/* grind through recognizing $PASHR */
   NMEA_PASHR_S,	/* grind through recognizing $PASHR */
   NMEA_PASHR_H,	/* grind through recognizing $PASHR */
   NMEA_BINARY_BODY,	/* Ashtech-style binary packet body, skip until \r\n */ 
   NMEA_BINARY_CR,	/* \r on end of Ashtech-style binary packet */ 
   NMEA_BINARY_NL,	/* \n on end of Ashtech-style binary packet */ 
   NMEA_CR,	   	/* seen terminating \r of NMEA packet */
   NMEA_RECOGNIZED,	/* saw trailing \n of NMEA packet */

   SIRF_ACK_LEAD_1,	/* seen A of possible SiRF Ack */
   SIRF_ACK_LEAD_2,	/* seen c of possible SiRF Ack */
   AIS_LEAD_1,		/* seen A of possible marine AIS message */
   AIS_LEAD_2,		/* seen I of possible marine AIS message */

   SEATALK_LEAD_1,	/* SeaTalk/Garmin packet leader 'I' */
#endif /* NMEA_ENABLE */

   DLE_LEADER,		/* we've seen the TSIP/EverMore leader (DLE) */

#ifdef TRIPMATE_ENABLE
   ASTRAL_1,		/* ASTRAL leader A */
   ASTRAL_2,	 	/* ASTRAL leader S */
   ASTRAL_3,		/* ASTRAL leader T */
   ASTRAL_4,		/* ASTRAL leader R */
   ASTRAL_5,		/* ASTRAL leader A */
#endif /* TRIPMATE_ENABLE */

#ifdef EARTHMATE_ENABLE
   EARTHA_1,		/* EARTHA leader E */
   EARTHA_2,		/* EARTHA leader A */
   EARTHA_3,		/* EARTHA leader R */
   EARTHA_4,		/* EARTHA leader T */
   EARTHA_5,		/* EARTHA leader H */
#endif /* EARTHMATE_ENABLE */

#ifdef SIRF_ENABLE
   SIRF_LEADER_1,	/* we've seen first character of SiRF leader */
   SIRF_LEADER_2,	/* seen second character of SiRF leader */
   SIRF_LENGTH_1,	/* seen first byte of SiRF length */
   SIRF_PAYLOAD,	/* we're in a SiRF payload part */
   SIRF_DELIVERED,	/* saw last byte of SiRF payload/checksum */
   SIRF_TRAILER_1,	/* saw first byte of SiRF trailer */ 
   SIRF_RECOGNIZED,	/* saw second byte of SiRF trailer */
#endif /* SIRF_ENABLE */

#ifdef ZODIAC_ENABLE
   ZODIAC_EXPECTED,	/* expecting Zodiac packet */
   ZODIAC_LEADER_1,	/* saw leading 0xff */
   ZODIAC_LEADER_2,	/* saw leading 0x81 */
   ZODIAC_ID_1, 	/* saw first byte of ID */
   ZODIAC_ID_2, 	/* saw second byte of ID */
   ZODIAC_LENGTH_1,	/* saw first byte of Zodiac packet length */
   ZODIAC_LENGTH_2,	/* saw second byte of Zodiac packet length */
   ZODIAC_FLAGS_1, 	/* saw first byte of FLAGS */
   ZODIAC_FLAGS_2, 	/* saw second byte of FLAGS */
   ZODIAC_HSUM_1, 	/* saw first byte of Header sum */
   ZODIAC_PAYLOAD,	/* we're in a Zodiac payload */
   ZODIAC_RECOGNIZED,	/* found end of the Zodiac packet */
#endif /* ZODIAC_ENABLE */

#if defined(TNT_ENABLE) || defined(GARMINTXT_ENABLE) || defined(ONCORE_ENABLE)
   AT1_LEADER,		/* saw True North status leader '@' */
			/* Garmin Simple Text starts with @ leader */
			/* Oncore starts with @ leader */
   GTXT_RECOGNIZED,     /* */
#endif

#ifdef EVERMORE_ENABLE
   EVERMORE_LEADER_1,	/* a DLE after having seen EverMore data */
   EVERMORE_LEADER_2,	/* seen opening STX of EverMore packet */
   EVERMORE_PAYLOAD,	/* in payload part of EverMore packet */
   EVERMORE_PAYLOAD_DLE,/* DLE in payload part of EverMore packet */
   EVERMORE_RECOGNIZED,	/* found end of EverMore packet */
#endif /* EVERMORE_ENABLE */

#ifdef ITRAX_ENABLE
   ITALK_LEADER_1,	/* saw leading < of iTalk packet */
   ITALK_LEADER_2,	/* saw leading ! of iTalk packet */
   ITALK_LENGTH,	/* saw packet length */
   ITALK_PAYLOAD,	/* in payload part of iTalk Packet */
   ITALK_DELIVERED,	/* seen end of payload */
   ITALK_TRAILER,	/* saw iTalk trailer byte */
   ITALK_RECOGNIZED,	/* found end of the iTalk packet */
#endif /* ITRAX_ENABLE */

#ifdef NAVCOM_ENABLE
   NAVCOM_EXPECTED,	/* expecting Navcom packet */
   NAVCOM_LEADER_1,	/* saw leading 0x02 */
   NAVCOM_LEADER_2,	/* saw leading 0x99 */
   NAVCOM_LEADER_3,	/* saw leading 0x66 */
   NAVCOM_ID,   	/* saw message ID */
   NAVCOM_LENGTH_1,	/* saw first byte of Navcom packet length */
   NAVCOM_LENGTH_2,	/* saw second byte of Navcom packet length */
   NAVCOM_PAYLOAD,	/* we're in a Navcom payload */
   NAVCOM_CSUM, 	/* saw checksum */
   NAVCOM_RECOGNIZED,	/* found end of the Navcom packet */
#endif /* NAVCOM_ENABLE */

#ifdef UBX_ENABLE
   UBX_LEADER_1,        /* first constant leader byte found */
   UBX_LEADER_2,        /* second constant leader byte found */
   UBX_CLASS_ID,        /* classid read */
   UBX_MESSAGE_ID,      /* message id read */
   UBX_LENGTH_1,        /* first length byte read (le) */
   UBX_LENGTH_2,        /* second length byte read (le) */
   UBX_PAYLOAD,         /* payload eating */
   UBX_CHECKSUM_A,      /* checksum A byte (tcp checksum) */
   UBX_RECOGNIZED,      /* this is also UBX_CHECKSUM_B */
#endif

#ifdef SUPERSTAR2_ENABLE
   SUPERSTAR2_LEADER,	/* leading SOH */
   SUPERSTAR2_ID1,	/* message type */
   SUPERSTAR2_ID2,	/* message type xor 0xff */
   SUPERSTAR2_PAYLOAD,	/* length of the actual packet data */
   SUPERSTAR2_CKSUM1,
   SUPERSTAR2_CKSUM2,
   SUPERSTAR2_RECOGNIZED,
#endif

#ifdef ONCORE_ENABLE
   ONCORE_AT2,		/* second @ */
   ONCORE_ID1,		/* first character of command type */
   ONCORE_PAYLOAD,	/* payload eating */
   ONCORE_CHECKSUM,	/* checksum byte */
   ONCORE_CR,		/* closing CR */
   ONCORE_RECOGNIZED,	/* closing LF */
#endif

#ifdef GEOSTAR_ENABLE
   GEOSTAR_LEADER_1,        /* first constant leader byte found */
   GEOSTAR_LEADER_2,        /* second constant leader byte found */
   GEOSTAR_LEADER_3,        /* third constant leader byte found */
   GEOSTAR_LEADER_4,        /* forth constant leader byte found */
   GEOSTAR_MESSAGE_ID_1,    /* first message id read */
   GEOSTAR_MESSAGE_ID_2,    /* second message id read */
   GEOSTAR_LENGTH_1,        /* first length byte read */
   GEOSTAR_LENGTH_2,        /* second length byte read */
   GEOSTAR_PAYLOAD,         /* payload eating */
   GEOSTAR_CHECKSUM_A,      /* checksum A byte (xor checksum) */
   GEOSTAR_CHECKSUM_B,      /* checksum B byte (xor checksum) */
   GEOSTAR_CHECKSUM_C,      /* checksum C byte (xor checksum) */
   GEOSTAR_RECOGNIZED,      /* this is also GEOSTAR_CHECKSUM_D */
#endif

/*
 * Packet formats without checksums start here.  We list them last so
 * that if a format with a conflicting structure *and* a checksum can
 * be recognized, that will be preferred.
 */

#if defined(TSIP_ENABLE) || defined(GARMIN_ENABLE)
   TSIP_LEADER,		/* a DLE after having seen TSIP data */
   TSIP_PAYLOAD,	/* we're in TSIP payload */
   TSIP_DLE,		/* we've seen a DLE in TSIP payload */
   TSIP_RECOGNIZED,	/* found end of the TSIP packet */
   GARMIN_RECOGNIZED,	/* found end of Garmin packet */
#endif /* TSIP_ENABLE GARMIN_ENABLE */

#ifdef RTCM104V2_ENABLE
   RTCM2_SYNC_STATE,	/* we have sync lock */
   RTCM2_SKIP_STATE,	/* we have sync lock, but this character is bad */
   RTCM2_RECOGNIZED,	/* we have an RTCM packet */
#endif /* RTCM104V2_ENABLE */

#ifdef RTCM104V3_ENABLE
   RTCM3_LEADER_1,      /* constant leader byte found */
   RTCM3_LEADER_2,      /* second leader byte found (high 6 bits zero) */
   RTCM3_PAYLOAD,       /* gathering payload */
   RTCM3_RECOGNIZED,    /* RTCM3 packet recognized */
#endif

#ifdef PASSTHROUGH_ENABLE
   JSON_LEADER,         /* JSON leading { found */
   JSON_STRINGLITERAL,  /* start of JSON string literal seen */
   JSON_STRING_SOLIDUS, /* backslash in string */
   JSON_END_ATTRIBUTE,  /* end of JSON attribute */
   JSON_EXPECT_VALUE,   /* just after colon */
   JSON_END_VALUE,      /* end of JSON value */
   JSON_NUMBER,         /* inside a JSON numeric literal */
   JSON_SPECIAL,        /* inside a JSON special literal (true,false,null) */
   JSON_RECOGNIZED,     /* JSON packet recognized */
#endif

/* end of packet_states.h */
