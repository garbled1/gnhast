#ifndef _CMDS_H_
#define _CMDS_H_

int cmd_register(pargs_t *args, void *arg);
int cmd_update(pargs_t *args, void *arg);
int cmd_change(pargs_t *args, void *arg);
int cmd_feed(pargs_t *args, void *arg);
int cmd_list_devices(pargs_t *args, void *arg);
int cmd_cactiask_device(pargs_t *args, void *arg);
int cmd_ask_device(pargs_t *args, void *arg);

int parsed_command(char *command, pargs_t *args, void *arg);

#endif /*_CMDS_H_*/
