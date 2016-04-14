#ifndef _CONFPARSER_H_
#define _CONFPARSER_H_

#define CONF_DUMP_DEVONLY	(1<<1)
#define CONF_DUMP_GROUPONLY	(1<<2)

cfg_t *parse_conf(const char *filename);
cfg_t *find_devconf_byuid(cfg_t *cfg, char *uid);
cfg_t *new_conf_from_devgrp(cfg_t *cfg, device_group_t *devgrp);
cfg_t *new_conf_from_dev(cfg_t *cfg, device_t *dev);
device_t *new_dev_from_conf(cfg_t *cfg, char *uid);
cfg_t *dump_conf(cfg_t *cfg, int flags, const char *filename);
int conf_validate_port(cfg_t *cfg, cfg_opt_t *opt);


int conf_parse_bool(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);
void conf_print_bool(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_spamhandler(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);
void conf_print_spamhandler(cfg_opt_t *opt, unsigned int index, FILE *fp);
int conf_parse_subtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result);
int conf_parse_type(cfg_t *cfg, cfg_opt_t *opt, const char *value,
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
void conf_print_subtype(cfg_opt_t *opt, unsigned int index, FILE *fp);


void parse_devgroups(cfg_t *cfg);
void print_group_table(int devs);

#endif /*_CONFPARSER_H_*/
