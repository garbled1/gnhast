#ifndef _INSTEON_H_
#define _INSTEON_H_

#include "collector.h"

#define INSTEON_DB_FILE		"insteon.db"
#define INSTEONCOLL_LOG_FILE	"insteoncoll.log"
#define INSTEONCOLL_PID_FILE	"insteoncoll.pid"
#define INSTEONCOLL_CONF_FILE	"insteoncoll.conf"

/* PLM defines */

#define PLM_TYPE_SERIAL		1
#define PLM_TYPE_HUBPLM		2
#define PLM_TYPE_HUBHTTP	3

/* send/recv */
#define PLM_START	0x02
#define PLM_RECV_STD	0x50
#define PLM_RECV_EXT	0x51
#define PLM_RECV_X10	0x52
#define PLM_SEND	0x62
#define PLM_SEND_X10	0x63

/* Nak/ack stuff */
#define PLM_SET_ACK1	0x68
#define PLM_SET_ACK2	0x71
#define PLM_SET_NAK1	0x70

/* All link stuffs */
#define PLM_ALINK_SEND		0x61
#define PLM_ALINK_CFAILREP	0x56
#define PLM_ALINK_CSTATUSREP	0x58
#define PLM_ALINK_START		0x64
#define PLM_ALINK_CANCEL	0x65
#define PLM_ALINK_COMPLETE	0x53
#define PLM_ALINK_GETFIRST	0x69
#define PLM_ALINK_GETNEXT	0x6A
#define PLM_ALINK_GETLASTRECORD	0x6C
#define PLM_ALINK_RECORD	0x57
#define PLM_ALINK_MODIFY	0x6F

/* General bits */
#define PLM_FULL_RESET	0x67
#define PLM_RESET	0x55
#define PLM_GETCONF	0x73
#define PLM_SETCONF	0x6B
#define PLM_GETINFO	0x60
#define PLM_SETHOST	0x66
#define PLM_SLEEP	0x72
#define PLM_BUTTONEVENT	0x54
#define PLM_LEDON	0x6D
#define PLM_LEDOFF	0x6E

/* Bits and bobs */
#define PLMCMD_ACK	0x06
#define PLMCMD_NAK	0x15

/* Flag bits */
#define PLMFLAG_MAXHOP1	(1<<0)
#define PLMFLAG_MAXHOP2	(1<<1)
#define PLMFLAG_REMHOP1	(1<<2)
#define PLMFLAG_REMHOP2	(1<<3)
#define PLMFLAG_EXT	(1<<4)
#define PLMFLAG_ACK	(1<<5)
#define PLMFLAG_GROUP	(1<<6)
#define PLMFLAG_BROAD	(1<<7)

#define PLMFLAGSET_STD3HOPS	(PLMFLAG_MAXHOP1|PLMFLAG_MAXHOP2|PLMFLAG_REMHOP1|PLMFLAG_REMHOP2)
#define PLMFLAGSET_EXT3HOPS	(PLMFLAGSET_STD3HOPS|PLMFLAG_EXT)

/* Config bits */
#define PLMCONF_BUSYNAK	(1<<3> /* 1: hub, nak if busy */
#define PLMCONF_DEADMAN	(1<<4) /* 1: disable 240ms timeout between bytes */
#define PLMCONF_LED	(1<<5) /* 1: disable automatic LED operation */
#define PLMCONF_MONITOR	(1<<6) /* 1: Monitor mode (promisc) */
#define PLMCONF_LINK	(1<<7) /* 1: Disable autolink on SET button push */

/* Command defines */
/*	STD Standard direct commands
	GRP Standard group commands
	EXT Extended data commands
	BCT Standard Broadcast comamnds
*/
/* From the table at:
   http://www.madreporite.com/insteon/commands.htm
*/
#define GRPCMD_ASSIGN_GROUP		0x01
#define GRPCMD_DEL_GROUP		0x02
#define STDCMD_PDATA_REQ       		0x03
	#define EXTCMD2_PDR_REQ			0x00
	#define STDCMD2_FXNAME_REQ		0x01
	#define STDCMD2_DTEXT_REQ		0x02
	#define EXTCMD2_SETTEXT			0x03
	#define EXTCMD2_SET_ALLLINK_ALIAS	0x04
#define BCTCMD_HEARTBEAT		0x04
#define STDCMD_LINKMODE			0x09
#define STDCMD_UNLINKMODE		0x0A
#define STDCMD_GETVERS			0x0D
#define STDCMD_PING			0x0F
#define STDCMD_IDREQ			0x10
#define STDCMD_ON			0x11
#define STDCMD_FASTON			0x12
#define STDCMD_OFF			0x13
#define STDCMD_FASTOFF			0x14
#define STDCMD_BRIGHT			0x15
#define STDCMD_DIM			0x16
#define STDCMD_MANUALDIM		0x17
#define STDCMD_MANUALDIMSTOP		0x18

#define STDCMD_STATUSREQ		0x19

#define EXTCMD_RWALDB			0x2F

#define addr_to_string(buf, addr) sprintf(buf, "%0.2X.%0.2X.%0.2X", addr[0], \
					  addr[1], addr[2])
#define addr_to_groupuid(buf, addr, group) sprintf(buf, \
        "%0.2X.%0.2X.%0.2X-%0.2X", addr[0], addr[1], addr[2], group)

/* General structures */
struct _cmdq_t;
typedef struct _cmdq_t {
	uint8_t cmd[25];	/**< \brief command to send */
	uint8_t sendcount;	/**< \brief nrof times send attempted */
	uint8_t msglen;		/**< \brief message length */
	uint8_t state;		/**< \brief state of entry */
	uint8_t wait;		/**< \brief things we wait for */
	struct timespec tp;	/**< \brief time entry got fired */
	char *uid;		/**< \brief uid of device who initiated */
	SIMPLEQ_ENTRY(_cmdq_t) entries;  /**< \brief FIFO queue */
} cmdq_t;

/**
   \brief A work queue for incoming data
*/
struct _workq_t;
typedef struct _workq_t {
	uint8_t cmd[30];	/**< \brief Body of command */
	SIMPLEQ_ENTRY(_workq_t) entries; /**< \brief FIFO queue */
} workq_t;


typedef struct _aldb_t {
	uint16_t addr;		/**< \brief address of record */
	uint8_t lflags;		/**< \brief link control flags ALDBLINK_* */
	uint8_t group;		/**< \brief group number */
	uint8_t devaddr[3];	/**< \brief addr of linked device */
	uint8_t ldata1;		/**< \brief linkdata1 (on level) */
	uint8_t ldata2;		/**< \brief linkdata2 (ramprate) */
	uint8_t ldata3;		/**< \brief unused? */
} aldb_t;

#define ALDB_MAXSIZE	64
#define IQUIRK_FANLINC_LIGHT	1	/* fanlinc light */
#define IQUIRK_FANLINC_FAN	2	/* fanlinc fan */
#define IQUIRK_DUALOUTLET_TOP	3	/* dual on/off outlet (top) */
#define IQUIRK_DUALOUTLET_BOT	4	/* dual on/off outlet (bottom) */

typedef struct _insteon_devdata_t {
	uint8_t daddr[3];	/**< \brief Device addr decoded */
	uint8_t hopflag;	/**< \brief ideal nrof hops */
	aldb_t aldb[ALDB_MAXSIZE];	/**< \brief aldb records */
	int aldblen;		/**< \brief length of aldb */
	uint8_t devcat;		/**< \brief store devcat here if needed */
	uint8_t subcat;		/**< \brief store subcat here if needed */
	uint8_t productkey[2];	/**< \brief Product Key */
	uint8_t firmware;	/**< \brief Firmware revision */
	uint8_t proto;		/**< \brief protocol */
	uint8_t ramprate;	/**< \brief ramprate */
	uint8_t ledbright;	/**< \brief LED brightness */
	uint8_t group;		/**< \brief group code */
	uint8_t quirk;		/**< \brief quirk type */
} insteon_devdata_t;

#define ALDBLINK_USED	(1<<1)
#define ALDBLINK_ACKREQ	(1<<5)
#define ALDBLINK_MASTER	(1<<6)
#define ALDBLINK_INUSE	(1<<7)

#define ALINK_IS_USED(flg)	(flg & ALDBLINK_USED)
#define ALINK_IS_MASTER(flg)	(flg & ALDBLINK_MASTER)

#define CMDQ_MAX_SEND	2

#define CMDQ_NOPWAIT	0xFF

#define CMDQ_DONE	0
#define CMDQ_WAITACK	(1<<0)
#define CMDQ_WAITDATA	(1<<1)
#define CMDQ_WAITEXT	(1<<2)
#define CMDQ_WAITSEND	(1<<3)
#define CMDQ_WAITALINK	(1<<4)
#define CMDQ_WAITANY	(1<<5) /* take any stdmsg as having worked */
#define CMDQ_WAITALDB	(1<<6) /* wait for all aldb records */

#define CMDQ_WAITACKDATA (CMDQ_WAITACK|CMDQ_WAITDATA)
#define CMDQ_WAITACKEXT (CMDQ_WAITACK|CMDQ_WAITEXT)
#define CMDQ_WAITING(x) (x & CMDQ_WAITACK || x & CMDQ_WAITDATA || \
			 x & CMDQ_WAITEXT || x & CMDQ_WAITALINK || \
			 x & CMDQ_WAITANY || x & CMDQ_WAITALDB)
#define CMDQ_FREE_ENTRY(x) if (x->uid != NULL) free(x->uid); free(x)

#define CONN_TYPE_PLM		1
#define CONN_TYPE_GNHASTD	2
#define CONN_TYPE_HUBPLM	3
#define CONN_TYPE_HUBHTTP	4

/* HTTP Hub stuff */

#define BUFFSTATUS_SUFX		"/buffstatus.xml"
#define BUFFSTATUS_BUFSIZ	256
#define WORKBUFSIZ		4096
#define CLEARIMBUFF		"1?XB=M=1"
#define HUB_TYPE_OLD		1
#define HUB_TYPE_NEW		2
/* HTTP Hub states */
#define HUBHTMLSTATE_IDLE	0
#define HUBHTMLSTATE_WCLEAR	1 /* waiting for clear */
#define HUBHTMLSTATE_WCMD	2 /* waiting for command */
#define HUBHTMLSTATE_WSEND	3 /* waiting for send confirmation */
#define CHECKHTMLSTATE(x) \
	if (x == PLM_TYPE_HUBHTTP) { \
		if (hubhtmlstate == HUBHTMLSTATE_WCMD) \
			hubhtmlstate = HUBHTMLSTATE_IDLE; \
	}


/****************
	Funtions from insteon_common.c
****************/
void connect_server_cb(int nada, short what, void *arg);
void connect_event_cb(struct bufferevent *ev, short what, void *arg);
void hubplm_connect_event_cb(struct bufferevent *ev, short what, void *arg);
int plmtype_connect(int plmtype, char *device, char *host, int portnum);
int conf_parse_plmtype(cfg_t *cfg, cfg_opt_t *opt, const char *value,
		       void *result);
void conf_print_plmtype(cfg_opt_t *opt, unsigned int index, FILE *fp);

void plm_enq_std(device_t *dev, uint8_t com1, uint8_t com2, uint8_t waitflags);
void plm_enq_ext(device_t *dev, uint8_t com1, uint8_t com2, uint8_t *data,
		 uint8_t waitflags);
uint8_t plm_calc_cs(uint8_t com1, uint8_t com2, uint8_t *data);
void plm_enq_stdcs(device_t *dev, uint8_t com1, uint8_t com2,
		   uint8_t waitflags);
void plm_enq_wait(int howlong);
void plm_check_proper_delay(uint8_t *devaddr);
void plm_runq(int fd, short what, void *arg);
void plmcmdq_retry_cur(void);
void plmcmdq_got_data(int whatkind);
void plmcmdq_check_ack(char *data);
void plmcmdq_dequeue(void);
void plmcmdq_flush(void);
void plmcmdq_check_recv(char *fromaddr, char *toaddr, uint8_t cmd1,
			int whatkind);
void plm_getinfo(void);
void plm_getconf(void);
void plm_setconf(uint8_t cfgflags);
void plm_getplm_aldb(int fl);
void plm_req_aldb(device_t *dev);
void plm_switch_on(device_t *dev, uint8_t level);
void plm_switch_off(device_t *dev);
void plm_all_link(uint8_t linkcode, uint8_t group);
void plm_handle_getinfo(uint8_t *data);
int plm_handle_aldb(device_t *dev, uint8_t *data);
void plm_handle_stdrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2);
void plm_handle_extrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, uint8_t *ext);
void plm_readcb(struct bufferevent *bev, void *arg);
void plm_run_workq(int fd, short what, void *arg);
void plm_set_hops(device_t *dev, uint8_t flag);
void plm_write_aldb(device_t *dev);

#endif /*_INSTEON_H_*/
