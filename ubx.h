/* $Id$ */

#define UBX_MESSAGE_BASE_SIZE 6
#define UBX_MESSAGE_DATA_OFFSET UBX_MESSAGE_BASE_SIZE

typedef enum {
    UBX_CLASS_ACK = 0x05,     /**< (Not) Acknowledges for cfg messages */
    UBX_CLASS_AID = 0x0b,     /**< AGPS */
    UBX_CLASS_CFG = 0x06,     /**< Configuration requests */
    UBX_CLASS_INF = 0x04,     /**< Informative text messages */
    UBX_CLASS_MON = 0x0a,     /**< System monitoring */
    UBX_CLASS_NAV = 0x01,     /**< Navigation */
    UBX_CLASS_RXM = 0x02,     /**< Receiver Manager */
    UBX_CLASS_TIM = 0x0d,     /**< Time */
    UBX_CLASS_UPD = 0x09,     /**< Firmware updates */
} ubx_classes_t;

#define UBX_MSGID(cls_, id_) (((cls_)<<8)|(id_))

typedef enum {
    UBX_ACK_NAK	    = UBX_MSGID(UBX_CLASS_ACK, 0x00),
    UBX_ACK_ACK	    = UBX_MSGID(UBX_CLASS_ACK, 0x01),
    UBX_AID_REQ	    = UBX_MSGID(UBX_CLASS_AID, 0x00),
    UBX_AID_DATA    = UBX_MSGID(UBX_CLASS_AID, 0x10),
    UBX_AID_INI	    = UBX_MSGID(UBX_CLASS_AID, 0x01),
    UBX_AID_HUI	    = UBX_MSGID(UBX_CLASS_AID, 0x02),
    UBX_AID_ALM	    = UBX_MSGID(UBX_CLASS_AID, 0x30),
    UBX_AID_EPH	    = UBX_MSGID(UBX_CLASS_AID, 0x31),

    UBX_NAV_SOL	    = UBX_MSGID(UBX_CLASS_NAV, 0x06),
    UBX_NAV_POSLLH  = UBX_MSGID(UBX_CLASS_NAV, 0x02),
    UBX_NAV_STATUS  = UBX_MSGID(UBX_CLASS_NAV, 0x03),
    UBX_NAV_SVINFO  = UBX_MSGID(UBX_CLASS_NAV, 0x30),
    UBX_NAV_X	    = UBX_MSGID(UBX_CLASS_NAV, 0x40),

    UBX_MON_SCHED   = UBX_MSGID(UBX_CLASS_MON, 0x01),
    UBX_MON_IO	    = UBX_MSGID(UBX_CLASS_MON, 0x02),
    UBX_MON_TXBUF   = UBX_MSGID(UBX_CLASS_MON, 0x08),

    UBX_INF_WARNING = UBX_MSGID(UBX_CLASS_INF, 0X01),
    UBX_INF_NOTICE  = UBX_MSGID(UBX_CLASS_INF, 0x02),

    UBX_CFG_PRT  = UBX_MSGID(UBX_CLASS_CFG, 0x00),
} ubx_message_t;

typedef enum {
    UBX_MODE_NOFIX  = 0x00,	/* no fix available */
    UBX_MODE_DR	    = 0x01,	/* Dead reckoning */
    UBX_MODE_2D	    = 0x02,	/* 2D fix */
    UBX_MODE_3D	    = 0x03,	/* 3D fix */
    UBX_MODE_GPSDR  = 0x04,	/* GPS + dead reckoning */
} ubx_mode_t;

#define UBX_SOL_FLAG_GPS_FIX_OK 0x01
#define UBX_SOL_FLAG_DGPS 0x02
#define UBX_SOL_VALID_WEEK 0x04
#define UBX_SOL_VALID_TIME 0x08

