/* $Id$ */

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

/* from common.c */
FILE *openlog(char *logf);
void closelog(void);
void LOG(int severity, const char * s, ...);
void _bailout(char *file, int line);
void *_safer_malloc(size_t size, char *file, int line);
int lcm(int a,int b);
int gcd(int a, int b);
void writepidfile(char *filename);
void cb_sighup(int fd, short what, void *arg);

/* from netparser.c */
int compare_command(const void *a, const void *b);

/* from devices.c */
device_t *find_device_byuid(char *uid);
void add_wrapped_device(device_t *dev, client_t *client, int rate);
void insert_device(device_t *dev);
void init_devtable(cfg_t *cfg, int readconf);
void get_data_dev(device_t *dev, int where, void *data);
void store_data_dev(device_t *dev, int where, void *data);
int datatype_dev(device_t *dev);
int device_watermark(device_t *dev);

/* from confparser.c */
cfg_t *parse_conf(const char *filename);
int conf_parse_bool(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);

#endif /* _COMMON_H_ */
