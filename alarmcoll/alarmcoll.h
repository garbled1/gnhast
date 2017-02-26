#ifndef _ALARMCOLL_H_
#define _ALARMCOLL_H_

enum WTYPES {
	WTYPE_GT,
	WTYPE_LT,
	WTYPE_GTE,
	WTYPE_LTE,
	WTYPE_EQ,
	WTYPE_NE,
	WTYPE_JITTER,
	WTYPE_AND,
	WTYPE_OR,
	WTYPE_HANDLER,
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
	int comparison;
	char *handler;
} watch_t;
#endif /*_ALARMCOLL_H_*/
