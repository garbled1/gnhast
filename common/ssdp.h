#ifndef _SSDP_H_
#define _SSDP_H_

/**
   \file ssdp.h
   \author Tim Rightnour
   \brief UPnP SSDP code
*/

#include "config.h"

#define SSDP_MULTICAST	"239.255.255.250"
#define SSDP_PORT	1900
#define SSDP_PORT_STR	"1900"
#define SSDP_WAIT_STR	"5"

void cb_ssdp_msearch_send(int fd, short what, void *arg);

#endif /* _SSDP_H_ */
