#ifndef _INSTEON_H_
#define _INSTEON_H_

#define INSTEON_DB_FILE		"insteon.db"
#define INSTEONCOLL_LOG_FILE	"insteoncoll.log"
#define INSTEONCOLL_PID_FILE	"insteoncoll.pid"
#define INSTEONCOLL_CONF_FILE	"insteoncoll.conf"

/* PLM defines */

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
	#define STDCMD2_FXNAME_REQ		0x01
	#define STDCMD2_DTEXT_REQ		0x02
	#define EXTCMD2_SETTEXT			0x03
	#define EXTCMD2_SET_ALLLINK_ALIAS	0x04
#define BCTCMD_HEARTBEAT		0x04
#define STDCMD_LINKMODE			0x09
#define STDCMD_UNLINKMODE		0x0A
#define STDCMD_GETVERS			0x0D
#define STDCMD_PING			0x0F


#define addr_to_string(buf, addr) sprintf(buf, "%0.2X.%0.2X.%0.2X", addr[0], \
					  addr[1], addr[2])

/* General structures */
struct _cmdq_t;
typedef struct _cmdq_t {
	uint8_t cmd[25];	/**< \brief command to send */
	uint8_t sendcount;	/**< \brief nrof times send attempted */
	uint8_t msglen;		/**< \brief message length */
	uint8_t state;		/**< \brief state of entry */
	uint8_t wait;		/**< \brief things we wait for */
	struct timespec tp;	/**< \brief time entry got fired */
	SIMPLEQ_ENTRY(_cmdq_t) entries;  /**< \brief FIFO queue */
} cmdq_t;

typedef struct _insteon_devdata_t {
	uint8_t daddr[3];	/**< \brief Device addr decoded */
	uint8_t hopflag;	/**< \brief ideal nrof hops */
} insteon_devdata_t;

#define CMDQ_MAX_SEND	2

#define CMDQ_DONE	0
#define CMDQ_WAITACK	(1<<0)
#define CMDQ_WAITDATA	(1<<1)
#define CMDQ_WAITEXT	(1<<2)
#define CMDQ_WAITSEND	(1<<3)
#define CMDQ_WAITALINK	(1<<4)

#define CMDQ_WAITACKDATA (CMDQ_WAITACK|CMDQ_WAITDATA)
#define CMDQ_WAITACKEXT (CMDQ_WAITACK|CMDQ_WAITEXT)

#define CONN_TYPE_PLM		1
#define CONN_TYPE_GNHASTD	2

typedef struct _connection_t {
	int port;
	int type;
	int lastcmd;
	char *host;
	struct bufferevent *bev;
} connection_t;

/****************
	Funtions from insteon_common.c
****************/

void plm_enq_std(device_t *dev, uint8_t com1, uint8_t com2, uint8_t waitflags);
uint8_t plm_calc_cs(uint8_t com1, uint8_t com2, uint8_t *data);
void plm_enq_stdcs(device_t *dev, uint8_t com1, uint8_t com2,
		   uint8_t waitflags);
void plm_runq(int fd, short what, void *arg);
void plmcmdq_retry_cur(void);
void plmcmdq_got_data(int whatkind);
void plmcmdq_check_ack(char *data);
void plmcmdq_dequeue(void);
void plmcmdq_check_recv(char *fromaddr, char *toaddr, uint8_t cmd1,
			int whatkind);
void plm_getinfo(void);
void plm_all_link(uint8_t linkcode, uint8_t group);
void plm_handle_getinfo(uint8_t *data);
void plm_handle_stdrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, connection_t *conn);
void plm_handle_extrecv(uint8_t *fromaddr, uint8_t *toaddr, uint8_t flags,
			uint8_t com1, uint8_t com2, uint8_t *ext,
			connection_t *conn);
void plm_readcb(struct bufferevent *bev, void *arg);
int serial_connect(char *devnode, int speed, int cflags);
void serial_eventcb(struct bufferevent *bev, short events, void *arg);


#endif /*_INSTEON_H_*/
