#ifndef _ALARMCOLL_H_
#define _ALARMCOLL_H_

enum WTYPES {
	WTYPE_GT,
	WTYPE_LT,
	WTYPE_EQ,
	WTYPE_JITTER,
};

typedef struct _watch_t {
	char *uid;
	int wtype;
	int sev;
	uint32_t channel;
	char *aluid;
	char *msg;
	data_t val;
	int fired;
	time_t lastchg;
	uint32_t threshold;
} watch_t;
#endif /*_ALARMCOLL_H_*/
