#ifndef _COMMON_H_
#define _COMMON_H_

/**
	\file common.h
	\author Tim Rightnour
	\brief Common utility functions
*/

#include "config.h"
#include "gnhast.h"
#include "commands.h"
#include "confuse.h"

/** \brief A quick and simple safer malloc */
#define smalloc(foo) _safer_malloc(sizeof(foo), __FILE__, __LINE__)

/** \brief a macro interface to safer_malloc */
#define safer_malloc(foo) _safer_malloc(foo, __FILE__, __LINE__)

/** \brief a macro interface to bailout */
#define bailout() _bailout(__FILE__, __LINE__)

#define LOG_DEBUG	1
#define LOG_NOTICE	2
#define LOG_WARNING	3
#define LOG_ERROR	4
#define LOG_FATAL	5

#define LITTLE_E (*(char *)(void *)&indian)

#define LETOH32(x)	((LITTLE_E) ? x : bswap32(x))
#define LETOH16(x)	((LITTLE_E) ? x : bswap16(x))
#define BETOH32(x)	((LITTLE_E) ? bswap32(x) : x)
#define BETOH16(x)	((LITTLE_E) ? bswap16(x) : x)

#define BCDHI(x)	((x & 0xF0)/16)
#define BCDLO(x)	((x & 0xF)%16)

#define CTOF(x)		((x * 1.8) + 32.0)
#define FTOC(x)		((x - 32.0) / 1.8)
#define CTOK(x)		(x + 273.15)
#define KTOC(x)		(x - 273.15)
#define KTOF(x)		(CTOF(KTOC(x)))
#define FTOR(x)		(x + 459.67)
#define RTOF(x)		(x - 459.67)

#define BARO_INTOMB(x)	(x * 33.8639)
#define BARO_MBTOIN(x)	(x * 0.02953)
#define BARO_MMTOMB(x)	(x * 1.333224)
#define BARO_MBTOMM(x)	(x * 0.750062)

#define INTOMM(x)	(x * 25.4)
#define MMTOIN(x)	(x * 0.0393701)

#define KPHTOMPH(x)	(x * 0.621371)
#define MSTOMPH(x)	(x * 2.23694)
#define KNOTSTOMPH(x)	(x * 1.15078)
#define MPHTOKPH(x)	(x * 1.60934)
#define MPHTOMS(x)	(x * 0.44704)
#define MPHTOKNOTS(x)	(x * 0.868976)

#define LIGHT_LUXTOWM2(x)	(x * 0.0079)
#define LIGHT_WM2TOLUX(x)	(x / 0.0079)

/* from common.c */
FILE *openlog(char *logf);
void closelog(void);
void LOG(int severity, const char * s, ...);
void _bailout(char *file, int line);
void *_safer_malloc(size_t size, char *file, int line);
char *mk_rrdname(char *orig);
int lcm(int a,int b);
int gcd(int a, int b);
void writepidfile(char *filename);
void delete_pidfile(void);
void cb_sighup(int fd, short what, void *arg);
void cb_sigusr1(int fd, short what, void *arg);

/* from netparser.c */
int compare_command(const void *a, const void *b);

/* from devices.c */
device_t *find_device_byuid(char *uid);
device_group_t *find_devgroup_byuid(char *uid);
void add_wrapped_device(device_t *dev, client_t *client, int rate, int scale);
void insert_device(device_t *dev);
device_group_t *new_devgroup(char *uid);
void add_dev_group(device_t *dev, device_group_t *devgrp);
void add_group_group(device_group_t *group1, device_group_t *group2);
int dev_in_group(device_t *dev, device_group_t *devgrp);
int group_in_group(device_group_t *grp, device_group_t *devgrp);
void init_devtable(cfg_t *cfg, int readconf);
void get_data_dev(device_t *dev, int where, void *data);
void store_data_dev(device_t *dev, int where, void *data);
int datatype_dev(device_t *dev);
int device_watermark(device_t *dev);
void cb_timerdev_update(int fd, short what, void *arg);

/* from confparser.c */
cfg_t *parse_conf(const char *filename);
int conf_parse_bool(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);
int conf_parse_tscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		      void *result);
void conf_print_tscale(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_lscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		      void *result);
void conf_print_lscale(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_speedscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		      void *result);
void conf_print_speedscale(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_baroscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
			 void *result);
void conf_print_baroscale(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_lightscale(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		      void *result);
void conf_print_lightscale(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_subtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result);
void conf_print_subtype(cfg_opt_t *opt, unsigned int index, FILE *fp);

void parse_devgroups(cfg_t *cfg);
void print_group_table(int devs);

/* From serial_common.c */
int serial_connect(char *devnode, int speed, int cflags);
void serial_eventcb(struct bufferevent *bev, short events, void *arg);

#endif /* _COMMON_H_ */
