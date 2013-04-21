#ifndef _GNCOLL_H_
#define _GNCOLL_H_

#define GNC_UPD_NAME	(1<<1)
#define GNC_UPD_RRDNAME	(1<<2)
#define GNC_UPD_CACTI	(1<<3)

void gn_register_device(device_t *dev, struct bufferevent *out);
void gn_update_device(device_t *dev, int what, struct bufferevent *out);

#endif /*_GNCOLL_H_*/
