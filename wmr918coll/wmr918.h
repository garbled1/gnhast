#ifndef _WMR918_H_
#define _WMR918_H_

#define WMR918COLL_CONFIG_FILE	"wmr918coll.conf"
#define WMR918COLL_LOG_FILE	"wmr918coll.log"
#define WMR918COLL_PID_FILE	"wmr918coll.pid"
#define COLLECTOR_NAME		"wmr918coll"

#define CONN_TYPE_WX200D       	1
#define CONN_TYPE_WX200SERIAL	2
#define CONN_TYPE_WMRSERIAL	3
#define CONN_TYPE_GNHASTD	4

#define GROUP_HUMID		0x8F
#define GROUP_HUMID_SIZE	35
#define GROUP_TEMP		0x9F
#define GROUP_TEMP_SIZE		34
#define GROUP_BARO		0xAF
#define GROUP_BARO_SIZE		31
#define GROUP_RAIN		0xBF
#define GROUP_RAIN_SIZE		14
#define GROUP_WIND		0xCF
#define GROUP_WIND_SIZE		27
#define GROUP_TIME		0xFF
#define GROUP_TIME_SIZE		5

#define DGROUP_HUMID	(1<<0)
#define DGROUP_TEMP	(1<<1)
#define DGROUP_BARO	(1<<2)
#define DGROUP_RAIN	(1<<3)
#define DGROUP_WIND	(1<<4)
#define DGROUP_ALL	0x1F

#define WMR_TYPE_WIND		0x00
#define WMR_TYPE_WIND_SIZE	11
#define WMR_TYPE_RAIN		0x01
#define WMR_TYPE_RAIN_SIZE	16
#define WMR_TYPE_TH		0x02
#define WMR_TYPE_TH_SIZE	9
#define WMR_TYPE_MUSH		0x03
#define WMR_TYPE_MUSH_SIZE	9
#define WMR_TYPE_THERM		0x04
#define WMR_TYPE_THERM_SIZE	7
#define WMR_TYPE_THB		0x05
#define WMR_TYPE_THB_SIZE	13
#define WMR_TYPE_MIN		0x0e
#define WMR_TYPE_MIN_SIZE	5
#define WMR_TYPE_CLOCK		0x0f
#define WMR_TYPE_CLOCK_SIZE	9
#define WMR_TYPE_EXTTHB		0x06
#define WMR_TYPE_EXTTHB_SIZE	14

#define DWMR_WIND		(1<<0)
#define DWMR_RAIN		(1<<1)
#define DWMR_TH0		(1<<2)
#define DWMR_TH1		(1<<3)
#define DWMR_TH2		(1<<4)
#define DWMR_TH3		(1<<5)
#define DWMR_MUSH		(1<<6)
#define DWMR_T0			(1<<7)
#define DWMR_T1			(1<<8)
#define DWMR_T2			(1<<9)
#define DWMR_T3			(1<<10)
#define DWMR_THB		(1<<11)
#define DWMR_MIN		(1<<12)
#define DWMR_CLOCK		(1<<13)
#define DWMR_EXTTHB		(1<<14)

#define DWEATHER_SUNNY		0xC
#define DWEATHER_PARTCLOUD	0x6
#define DWEATHER_CLOUDY		0x2
#define DWEATHER_RAINY		0x3

#endif /*_WMR918_H_*/
