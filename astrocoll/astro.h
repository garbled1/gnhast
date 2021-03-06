#ifndef _ASTRO_H_
#define _ASTRO_H_

#include "collector.h"

#define ASTROCOLL_CONFIG_FILE	"astrocoll.conf"
#define ASTROCOLL_LOG_FILE	"astrocoll.log"
#define ASTROCOLL_PID_FILE	"astrocoll.pid"

#define SUNRISE_SUNSET_URL	"http://api.sunrise-sunset.org"
#define SUNRISE_SUNSET_API	"/json?"
#define SUNRISE_SUNSET_PORT	80

#define USNO_URL	"http://api.usno.navy.mil"
#define USNO_API	"/rstt/oneday?ID=gnhast&"
#define USNO_PORT	80

#define SUNTYPE_SS	0
#define SUNTYPE_DEV	1
#define SUNTYPE_INT	2
#define SUNTYPE_USNO	3

enum SS_TIMES {
	SS_ASTRO_BEGIN, /* 0 */
	SS_NAUTICAL_BEGIN,
	SS_CIVIL_BEGIN,
	SS_SUNRISE_BEGIN,
	SS_SUNSET_BEGIN,
	SS_CIVIL_END,
	SS_NAUTICAL_END,
	SS_ASTRO_END,
	SS_SOLAR_NOON,
};

enum USNO_TIMES {
	USNO_SUN_CIVIL_BEGIN,
	USNO_SUN_SUNRISE,
	USNO_SUN_SUNSET,
	USNO_SUN_CIVIL_END,
	USNO_SUN_SOLAR_NOON,
};

enum USNO_LUNAR_TIMES {
	USNO_MOONRISE,
	USNO_MOONSET,
	USNO_MOON_UPPER_TRANSIT,
};

#endif /*_ASTRO_H_*/
