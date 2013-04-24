#ifndef _GNHASTD_H_
#define _GNHASTD_H_

void init_netloop(void);
void buf_read_cb(struct bufferevent *in, void *arg);
void buf_error_cb(struct bufferevent *ev, short what, void *arg);

#endif /*_GNHASTD_H_*/
