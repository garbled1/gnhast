#ifndef _GNHASTD_H_
#define _GNHASTD_H_

#define GNHASTD_CONFIG_FILE	"gnhastd.conf"
#define GNHASTD_LOG_FILE	"gnhastd.log"
#define GNHASTD_PID_FILE	"gnhastd.pid"
#define GNHASTD_DEVICE_FILE	"devices.conf"

void init_netloop(void);
void buf_read_cb(struct bufferevent *in, void *arg);
void buf_write_cb(struct bufferevent *in, void *arg);
void buf_error_cb(struct bufferevent *ev, short what, void *arg);
void network_shutdown(void);

#endif /*_GNHASTD_H_*/
