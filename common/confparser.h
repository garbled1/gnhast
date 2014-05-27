#ifndef _CONFPARSER_H_
#define _CONFPARSER_H_

#define CONF_DUMP_DEVONLY	(1<<1)
#define CONF_DUMP_GROUPONLY	(1<<2)

int conf_parse_subtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result);
int conf_parse_type(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);
cfg_t *find_devconf_byuid(cfg_t *cfg, char *uid);
cfg_t *new_conf_from_devgrp(cfg_t *cfg, device_group_t *devgrp);
cfg_t *new_conf_from_dev(cfg_t *cfg, device_t *dev);
device_t *new_dev_from_conf(cfg_t *cfg, char *uid);
cfg_t *dump_conf(cfg_t *cfg, int flags, const char *filename);
int conf_validate_port(cfg_t *cfg, cfg_opt_t *opt);
int conf_parse_bool(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		    void *result);

#endif /*_CONFPARSER_H_*/
