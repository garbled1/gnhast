#ifndef _BRULTECH_H_
#define _BRULTECH_H_

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define COLLECTOR_NAME		"brulcoll"
#define BRULCOLL_CONFIG_FILE	"brulcoll.conf"
#define BRULCOLL_LOG_FILE	"brulcoll.log"
#define BRULCOLL_PID_FILE	"brulcoll.pid"

#define BRUL_MODEL_GEM		1
#define BRUL_MODEL_ECM1240	2

#define BRUL_COMM_NET		1
#define BRUL_COMM_SERIAL	2

#define BRUL_TEMP_F		0
#define BRUL_TEMP_C		1

#define BRUL_MODE_NONE		0
#define BRUL_MODE_RTON		1
#define BRUL_MODE_RTOFF		2
#define BRUL_MODE_PKT		3
#define BRUL_MODE_IVL		4
#define BRUL_MODE_SRN		5
#define BRUL_MODE_TEMPTYPE	6
#define BRUL_MODE_TST		7
#define BRUL_MODE_PST		8
#define BRUL_MODE_CMX		9
#define BRUL_MODE_DATA		20
#define BRUL_MODE_WAITREADY	21
#define BRUL_MODE_ECMREADY	22
#define BRUL_MODE_WAITTOG	23
#define BRUL_MODE_ECMTOG	24
#define BRUL_MODE_ECMDATA	30

/* GEM packet formats */
#define BRUL_PKT_LIST		0	/* List format */
#define BRUL_PKT_ECMSIM		1	/* Multi-ECM Simulation */
#define BRUL_PKT_ASCII		2	/* ASCII with Wh */
#define BRUL_PKT_HTTPGET	3	/* HTTP GET */
#define BRUL_PKT_48NETTIME	4	/* Bin48-NET-Time */
#define BRUL_PKT_48NET		5	/* Bin48-NET */
#define BRUL_PKT_SEG		6	/* SEG Format */
#define BRUL_PKT_48ABS		7	/* Bin48 ABS */
#define BRUL_PKT_32NET		8	/* Bin32 Net */
#define BRUL_PKT_ISY		11	/* Universal Device ISY */
#define BRUL_PKT_COSM		13	/* COSM */
#define BRUL_PKT_NEWSEG		14	/* New SEG Format */

/* Output formats */
#define BRUL_FMT_ECM1220	1
#define BRUL_FMT_ECM1240	3	/* ecm1240 */
#define BRUL_FMT_32POLAR	7	/* 32 with polarized */
#define BRUL_FMT_32		8	/* 32 channels */
#define BRUL_FMT_48POLAR	5	/* 48 channels, polarized */

#define CONV_WATTSEC(x) (int64_t)x.byte[0] + ((int64_t)x.byte[1]<<8) + \
	((int64_t)x.byte[2]<<16) + ((int64_t)x.byte[3]<<24) + \
	((int64_t)x.byte[4]<<32)

#define CONV_THREE(x) (int64_t)x.byte[0] + ((int64_t)x.byte[1]<<8) + \
	((int64_t)x.byte[2]<<16)

#define MAX_WSEC	256LL*256LL*256LL*256LL*256LL
#define MAX_THREE	256*256*256

typedef struct _wattsec_t {
	uint8_t byte[5]; /**< \brief bytes of data */
} __packed wattsec_t;

typedef struct _threebyte_t {
	uint8_t byte[3]; /**< \brief bytes of data */
} __packed threebyte_t;

typedef struct _datetime_t {
	uint8_t year, month, day, hour, minute, second;
} __packed datetime_t;

/* XXX offbyone? */
#define BYTEL_GEM32_POLARIZED		429
#define BYTEL_GEM32			269
#define BYTEL_GEM48_POLARIZED		619
#define BYTEL_GEM48_POLARIZED_DT	625
#define BYTEL_ECM1240			65

/* Hold data bout the brultech */
typedef struct _brulconf_t {
	int32_t nrofchannels;	/**< \brief nrof valid channels */
	int validtemp;		/**< \brief bitmask of valid temp probes */
	int validpulse;		/**< \brief bitmask of valid pulse probes */
	int serial;		/**< \brief serial number */
} brulconf_t;

/* Hold converted data */
typedef struct _bruldata_t {
	int64_t channel[48];	/**< \brief Absolute wattseconds */
	int64_t polar[48];	/**< \brief Polarized wattseconds */
	double voltage;		/**< \brief voltage */
	int seconds;		/**< \brief seconds counter */
	double temp[8];		/**< \brief temperature probes */
	int pulse[4];		/**< \brief pulse counters */
	int serial;		/**< \brief serial number */
	double amps[2];		/**< \brief amps */
} bruldata_t;


/* 32 channels with polarized */
typedef struct _gem_polar32_t {
	uint8_t header[2];	/**< \brief header (FE FF for GEM) */
	uint8_t hformat;	/**< \brief header format (07) */
	uint16_t voltage;	/**< \brief voltage *10 */
	wattsec_t channel[32];	/**< \brief absolute wattsecs */
	wattsec_t polar[32];	/**< \brief polarized wattsecs */
	uint16_t serial;	/**< \brief serial number */
	uint8_t res1;		/**< \brief reserved */
	uint8_t devid;		/**< \brief device id */
	uint8_t res2[64];	/**< \brief reserved */
	threebyte_t seconds;	/**< \brief seconds */
	threebyte_t pulse[4];	/**< \brief pulse counters */
	uint16_t temp[8];	/**< \brief temp sensors */
	uint16_t res3;		/**< \brief reserved */
	uint8_t footer[2];	/**< \brief footer */
	uint8_t checksum;	/**< \brief checksum */
} __packed gem_polar32_t;

/* 32 channels, not polarized */
typedef struct _gem32_t {
	uint8_t header[2];	/**< \brief header (FE FF for GEM) */
	uint8_t hformat;	/**< \brief header format (08) */
	uint16_t voltage;	/**< \brief voltage *10 */
	wattsec_t channel[32];	/**< \brief absolute wattsecs */
	uint16_t serial;	/**< \brief serial number */
	uint8_t res1;		/**< \brief reserved */
	uint8_t devid;		/**< \brief device id */
	uint8_t res2[64];	/**< \brief reserved */
	threebyte_t seconds;	/**< \brief seconds */
	threebyte_t pulse[4];	/**< \brief pulse counters */
	uint16_t temp[8];	/**< \brief temp sensors */
	uint16_t res3;		/**< \brief reserved */
	uint8_t footer[2];	/**< \brief footer */
	uint8_t checksum;	/**< \brief checksum */
} __packed gem32_t;

/* 48 channels, polarized */
typedef struct _gem_polar48_t {
	uint8_t header[2];	/**< \brief header (FE FF for GEM) */
	uint8_t hformat;	/**< \brief header format (05) */
	uint16_t voltage;	/**< \brief voltage *10 */
	wattsec_t channel[48];	/**< \brief absolute wattsecs */
	wattsec_t polar[48];	/**< \brief polarized wattsecs */
	uint16_t serial;	/**< \brief serial number */
	uint8_t res1;		/**< \brief reserved */
	uint8_t devid;		/**< \brief device id */
	uint8_t res2[96];	/**< \brief reserved */
	threebyte_t seconds;	/**< \brief seconds */
	threebyte_t pulse[4];	/**< \brief pulse counters */
	uint16_t temp[8];	/**< \brief temp sensors */
	uint8_t footer[2];	/**< \brief footer */
	uint8_t checksum;	/**< \brief checksum */
} __packed gem_polar48_t;

/* 48 channels, polarized, with date and time */
typedef struct _gem_polar48dt_t {
	uint8_t header[2];	/**< \brief header (FE FF for GEM) */
	uint8_t hformat;	/**< \brief header format (05) */
	uint16_t voltage;	/**< \brief voltage *10 */
	wattsec_t channel[48];	/**< \brief absolute wattsecs */
	wattsec_t polar[48];	/**< \brief polarized wattsecs */
	uint16_t serial;	/**< \brief serial number */
	uint8_t res1;		/**< \brief reserved */
	uint8_t devid;		/**< \brief device id */
	uint8_t res2[96];	/**< \brief reserved */
	threebyte_t seconds;	/**< \brief seconds */
	threebyte_t pulse[4];	/**< \brief pulse counters */
	uint16_t temp[8];	/**< \brief temp sensors */
	datetime_t date;	/**< \brief date and time */
	uint8_t footer[2];	/**< \brief footer */
	uint8_t checksum;	/**< \brief checksum */
} __packed gem_polar48dt_t;

/* ECM1240 */

/* 2 channels, 2 amps, 5 aux polarized */
typedef struct _ecm1240_t {
	uint8_t header[2];	/**< \brief header (FE FF for ECM1240) */
	uint8_t hformat;	/**< \brief header format (03) */
	uint16_t voltage;	/**< \brief voltage *10 */
	wattsec_t channel[2];	/**< \brief absolute wattsecs */
	wattsec_t polar[2];	/**< \brief polarized wattsecs */
	int32_t res1;		/**< \brief reserved */
	uint16_t serial;	/**< \brief serial number */
	uint8_t flag;		/**< \brief unit info */
	uint8_t devid;		/**< \brief device id */
	uint16_t amps[2];	/**< \brief amps */
	threebyte_t seconds;	/**< \brief seconds */
	uint32_t aux[5];	/**< \brief AUX wattsec (4 bytes!) */
	int16_t dcin;		/**< \brief DC input voltage */
	uint8_t footer[2];	/**< \brief footer */
	uint8_t checksum;	/**< \brief checksum */
} __packed ecm1240_t;

/* WIZNet device struct */

#define WIZ_MODE_CLIENT	0x0
#define WIZ_MODE_SERVER	0x01
#define WIZ_MODE_MIXED	0x02

#define WIZ_BAUD_1200	0xa0
#define WIZ_BAUD_2400	0xd0
#define WIZ_BAUD_4800	0xe8
#define WIZ_BAUD_9600	0xf4
#define WIZ_BAUD_19200	0xfa
#define WIZ_BAUD_38400	0xfd
#define WIZ_BAUD_57600	0xfe
#define WIZ_BAUD_115200	0xff
#define WIZ_BAUD_230400	0xbb

#define WIZ_DHCP_STATIC	0x0
#define WIZ_DHCP_DHCP	0x1
#define WIZ_DHCP_PPPOE	0x2

#define WIZ_UDP_TCP	0x0
#define WIZ_UDP_UDP	0x1

#define WIZNET_PORT	1460
#define WIZNET_LISTEN	5001

typedef struct _wiznet_t {
	uint8_t macaddr[6];	/**< \brief MAC address */
	uint8_t opmode;		/**< \brief Operation mode (0,1,2) */
	uint8_t ipaddr[4];	/**< \brief IP Addr */
	uint8_t submask[4];	/**< \brief subnet mask */
	uint8_t gateway[4];	/**< \brief gateway */
	uint8_t port[2];	/**< \brief portnum */
	uint8_t remipaddr[4];	/**< \brief remote host ip for client mode */
	uint8_t rport[2];	/**< \brief remote port */
	uint8_t baud;		/**< \brief Baud rate */
	uint8_t dbits;		/**< \brief serial data bits 0x7|0x8 */
	uint8_t parity;		/**< \brief serial parity none/odd/even */
	uint8_t stop;		/**< \brief serial stop bits 0x01 */
	uint8_t flow;		/**< \brief serial flow control */
	uint8_t packingchar;	/**< \brief special packing char */
	uint8_t packlen[2];	/**< \brief packing char len */
	uint8_t packint[2];	/**< \brief packing char interval */
	uint8_t inactive[2];	/**< \brief inactivity timeout */
	uint8_t debug;		/**< \brief debug mode */
	uint8_t fwver[2];	/**< \brief firmware ver */
	uint8_t dhcp;		/**< \brief dhcp mode */
	uint8_t udp;		/**< \brief tcp/udp mode */
	uint8_t connstatus;	/**< \brief connected status */
	uint8_t dflag;		/**< \brief remote IP=0x0 or hostname=0x1 */
	uint8_t dnsaddr[4];	/**< \brief dns server addr */
	char rhostname[32];	/**< \brief remote host domainname */
	uint8_t scfg;		/**< \brief serial config mode trigger */
	uint8_t scfgstr[3];	/**< \brief serial cfg trigger cmd */
	uint8_t pppoeid[32];	/**< \brief PPPoE ID */
	uint8_t pppoepass[32];	/**< \brief PPPoE passwd */
	uint8_t enp;		/**< \brief enable passwd */
	uint8_t connpass[8];	/**< \brief remote host connect pass */
} __packed wiznet_t;



#endif /*_BRULTECH_H_*/
