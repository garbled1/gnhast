#ifndef _CMDS_H_
#define _CMDS_H_

int cmd_register(pargs_t *args, void *arg);
int cmd_register_group(pargs_t *args, void *arg);
int cmd_update(pargs_t *args, void *arg);
int cmd_change(pargs_t *args, void *arg);
int cmd_modify(pargs_t *args, void *arg);
int cmd_feed(pargs_t *args, void *arg);
int cmd_cfeed(pargs_t *args, void *arg);
int cmd_client(pargs_t *args, void *arg);
int cmd_list_devices(pargs_t *args, void *arg);
int cmd_list_groups(pargs_t *args, void *arg);
int cmd_cactiask_device(pargs_t *args, void *arg);
int cmd_ask_device(pargs_t *args, void *arg);
int cmd_ask_full_device(pargs_t *args, void *arg);
int cmd_disconnect(pargs_t *args, void *arg);
int cmd_ping(pargs_t *args, void *arg);
int cmd_imalive(pargs_t *args, void *arg);
int cmd_setalarm(pargs_t *args, void *arg);
int cmd_listen_alarms(pargs_t *args, void *arg);
int cmd_dump_alarms(pargs_t *args, void *arg);

int parsed_command(char *command, pargs_t *args, void *arg);

#endif /*_CMDS_H_*/
