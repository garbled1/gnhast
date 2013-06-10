#ifndef _GNCOLL_H_
#define _GNCOLL_H_

#define GNC_NOSCALE	(1<<0)
#define GNC_UPD_NAME	(1<<1)
#define GNC_UPD_RRDNAME	(1<<2)
#define GNC_UPD_CACTI	(1<<3)

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
void gn_register_device(device_t *dev, struct bufferevent *out);
void gn_update_device(device_t *dev, int what, struct bufferevent *out);
void gn_disconnect(struct bufferevent *bev);
void gn_client_name(struct bufferevent *bev, char *name);

#endif /*_GNCOLL_H_*/
