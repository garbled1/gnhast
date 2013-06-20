#ifndef _COMMAND_H_
#define _COMMAND_H_

/*
	\file command.h
	\author Tim Rightnour
	\brief Commands understood by the server
*/

#include "gnhast.h"

/** \brief Command argument record */

typedef struct _pargs_t {
	int cword;		/**< \brief command word */
	int type;		/**< \brief data type, see PTDOUBLE etc */
	union {
		double d;	/**< \brief PTDOUBLE */
		float f;	/**< \brief PTFLOAT */
		char *c;	/**< \brief PTCHAR */
		int i;		/**< \brief PTINT */
		long l;		/**< \brief PTLONG */
		uint32_t u;	/**< \brief PTUINT */
		int64_t ll;	/**< \brief PTLL */
	} arg;			/**< \brief argument data */
} pargs_t;

typedef struct _argtable_t {
	char *name;	/**< \brief Argument keyword */
	int num;	/**< \brief argument subcommand number */
	int type;	/**< \brief data type see PTDOUBLE etc */
} argtable_t;

/** \brief Function to call to process a command */
typedef int (*command_func_t)(pargs_t *args, void *arg); 

/** \brief The command structure */ 
typedef struct _commands_t {
 	char *name;		/**< \brief command word */
 	command_func_t func;	/**< \brief function to call */
 	int queue;		/**< \brief queue priority */
} commands_t;

#define ARGNM(foo)	argtable[find_arg_byid(foo)].name
#define ARGDEV(foo)	argtable[find_arg_bydev(foo)].name

enum PT_TYPES {
	PTDOUBLE,
	PTFLOAT,
	PTCHAR,
	PTINT,
	PTLONG,
	PTUINT,
	PTLL,
};

/* subcommands */

enum SC_COMMANDS {
	SC_UID,		/**< \brief UID (string type) */
	SC_NAME,	/**< \brief name */
	SC_RATE,	/**< \brief feeder rate */
	SC_RRDNAME,	/**< \brief rrd_name */
	SC_DEVTYPE,	/**< \br	ief device type */
	SC_PROTO,	/**< \brief device proto	col */
	SC_SUBTYPE,	/**< \brief device subtype */
	SC_SWITCH,	/**< \brief switch data */
	SC_DIMMER,	/**< \brief dimmer data */
	SC_TEMP,	/**< \brief temperature data */
	SC_HUMID,	/**< \brief humidity data */
	SC_LUX,		/**< \brief lux data */
	SC_PRESSURE,	/**< \brief pressure data */
	SC_COUNT,	/**< \brief count */
	SC_SPEED,	/**< \brief speed */
	SC_DIR,		/**< \brief direction */
	SC_WETNESS,	/**< \brief wetness */
	SC_MOISTURE,	/**< \brief moisture */
	SC_VOLTAGE,	/**< \brief voltage */
	SC_WATTSEC,	/**< \brief watt seconds */
	SC_WATT,	/**< \brief wattage */
	SC_AMPS,	/**< \brief amperage */
	SC_RAINRATE,	/**< \brief rainrate */
	SC_CLIENT,	/**< \brief client name */
	SC_SCALE,	/**< \brief scale (temp scale, baro, etc) */
	SC_WEATHER,	/**< \brief weather status */
};

/* commands */

#define C_REGISTER		1	/**< \brief Register device */
#define C_UPDATE		2	/**< \brief Update device status */
#define C_CHANGE		3	/**< \brief Change device status */
#define C_NEED			4	/**< \brief Need device status upds */

void init_argcomm(void);
pargs_t *parse_command(char **words, int count); 
char **parse_netcommand(char *buf, int *arg_count);
int find_arg_by_id(pargs_t *args, int id);
int find_arg_bydev(device_t *dev);

#endif /*_COMMAND_H_*/
