#ifndef _GNCOLL_H_
#define _GNCOLL_H_

#define GNC_NOSCALE	(1<<0)
#define GNC_UPD_NAME	(1<<1)
#define GNC_UPD_RRDNAME	(1<<2)
#define GNC_UPD_CACTI	(1<<3)
#define GNC_UPD_HANDLER	(1<<4)
#define GNC_UPD_HARGS	(1<<5)
#define GNC_UPD_WATER	(1<<6) /* watermarks */
#define GNC_UPD_FULL	(1<<7)
#define GNC_UPD_TAGS	(1<<17) /*sigh*/

/* GNC_UPD_XXX bits 8-16 are reserved for scale */
#define GNC_UPD_SCALE(x)	(1<<(8+x))

/* reverse out the scale from the bitfield */
#define GNC_GET_SCALE(x)	(ffs((x & 0xff00)>>9))

double gn_scale_temp(double temp, int cur, int new);
double gn_scale_pressure(double press, int cur, int new);
double gn_scale_speed(double speed, int cur, int new);
double gn_scale_length(double length, int cur, int new);
double gn_scale_light(double light, int cur, int new);
double gn_maybe_scale(device_t *dev, int scale, double val);
void gn_modify_device(device_t *dev, struct bufferevent *out);
void gn_register_device(device_t *dev, struct bufferevent *out);
void gn_register_devgroup_nameonly(device_group_t *devgrp,
				   struct bufferevent *out);
void gn_register_devgroup(device_group_t *devgrp, struct bufferevent *out);
void gn_update_device(device_t *dev, int what, struct bufferevent *out);
void gn_disconnect(struct bufferevent *bev);
void gn_client_name(struct bufferevent *bev, char *name);
void gn_ping(struct bufferevent *bev);
void gn_imalive(struct bufferevent *bev);
void gn_get_apiv(struct bufferevent *bev);
void gn_setalarm(struct bufferevent *bev, char *aluid, char *altext,
		 int alsev, uint32_t alchan);
char **build_tags(int num, ...);
void generic_build_device(cfg_t *cfg, char *uid, char *name, char *rrdname,
			  int proto, int type, int subtype, char *loc,
			  int tscale, char **tags, int nroftags,
			  struct bufferevent *bev);

#endif /*_GNCOLL_H_*/
