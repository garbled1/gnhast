#ifndef _CONFPARSER_H_
#define _CONFPARSER_H_

cfg_t *find_devconf_byuid(cfg_t *cfg, char *uid);
cfg_t *new_conf_from_dev(cfg_t *cfg, device_t *dev);
device_t *new_dev_from_conf(cfg_t *cfg, char *uid);
cfg_t *dump_conf(cfg_t *cfg, const char *filename);
int conf_validate_port(cfg_t *cfg, cfg_opt_t *opt);

#endif /*_CONFPARSER_H_*/
