#ifndef _30303_DISC_H_
#define _30303_DISC_H_

#define D30303_DPORT		30303
#define D30303_DSTRING		"Discovery: Who is out there?"
#define MAX_D30303_DEVS		16
#define D30303_SCAN_TIMEOUT	5 /* seconds */

typedef struct _d30303_resp_t {
	char *ipaddr;
	struct in_addr sin_addr;
	char *uc_hostname;
	char *macaddr;
} d30303_resp_t;

typedef struct _d30303_dev_t {
	char *hostname;
	char *mac_prefix;
	char *url;
} d30303_dev_t;

void d30303_setup(char *mac_prefix, char *hostname);

#endif /*_30303_DISC_H_*/
