#ifndef _GENCONN_H_
#define _GENCONN_H_

typedef struct _connection_t {
	int port;
	int type;
	int lastcmd;
	char *host;
	struct bufferevent *bev;
	device_t *current_dev;
	time_t lastdata;
	int shutdown;
} connection_t;

void generic_connect_server_cb(int nada, short what, void *arg);
void generic_connect_event_cb(struct bufferevent *ev, short what, void *arg);
void generic_cb_shutdown(int fd, short what, void *arg);
void generic_cb_sigterm(int fd, short what, void *arg);

#endif/*_GENCONN_H_*/
