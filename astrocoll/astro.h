#ifndef _ASTRO_H_
#define _ASTRO_H_

#include "collector.h"

#define ASTROCOLL_CONFIG_FILE	"astrocoll.conf"
#define ASTROCOLL_LOG_FILE	"astrocoll.log"
#define ASTROCOLL_PID_FILE	"astrocoll.pid"

#define SUNRISE_SUNSET_URL	"http://api.sunrise-sunset.org"
#define SUNRISE_SUNSET_API	"/json?"
#define SUNRISE_SUNSET_PORT	80

#define SUNTYPE_SS	0
#define SUNTYPE_DEV	1
#define SUNTYPE_INT	2

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

#endif /*_ASTRO_H_*/
