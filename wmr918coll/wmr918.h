#ifndef _WMR918_H_
#define _WMR918_H_

#define WMR918COLL_CONFIG_FILE	"wmr918coll.conf"
#define WMR918COLL_LOG_FILE	"wmr918coll.log"
#define WMR918COLL_PID_FILE	"wmr918coll.pid"
#define COLLECTOR_NAME		"wmr918coll"

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

#endif /*_WMR918_H_*/
