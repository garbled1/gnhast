#ifndef _BALBOA_H_
#define _BALBOA_H_

#define BALBOA_MAC_PREFIX	"00-15-27"
#define BALBOA_PORT		4257

#define M_START		0x7e
#define M_END		0x7e

#define C_PUMP1		0x04
#define C_PUMP2		0x05
#define C_PUMP3		0x06
#define C_PUMP4		0x07
#define C_PUMP5		0x08
#define C_PUMP6		0x09
#define C_LIGHT1	0x11
#define C_LIGHT2	0x12
#define C_MISTER	0x0e
#define C_AUX1		0x16
#define C_AUX2		0x17
#define C_BLOWER	0x0c
#define C_TEMPRANGE	0x50
#define C_HEATMODE	0x51

#define MAX_PUMPS	6

#define CFGDONE_CONF	0
#define CFGDONE_PANEL	1
#define CFGDONE_STATUS	2

typedef struct _spaconfig_t {
	int pump_array[6];
	int light_array[2];
	int mister;
	int aux_array[2];
	int blower;
	int tscale;
	int trange;
	int circpump;
	int cfgdone[3];		/**< \brief Did we get all cfg responses? */
} spaconfig_t;

enum BALBOA_MTYPES {
   BMTR_STATUS_UPDATE,
   BMTR_FILTER_CONFIG,
   BMTS_CONFIG_REQ,
   BMTR_CONFIG_RESP,
   BMTS_FILTER_REQ,
   BMTS_CONTROL_REQ,
   BMTS_SET_TEMP,
   BMTS_SET_TIME,
   BMTS_SET_WIFI,
   BMTS_PANEL_REQ,
   BMTS_SET_TSCALE,
   BMTR_PANEL_RESP,
   BMTR_PANEL_NOCLUE1,
   BMTR_PANEL_NOCLUE2,
   NROF_BMT,
};

#endif /*_BALBOA_H_*/
