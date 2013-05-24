#ifndef _COLLCMD_H_
#define _COLLCMD_H_


void init_commands(void);
int parsed_command(char *command, pargs_t *args, void *arg);
int cmd_register(pargs_t *args, void *arg);
int cmd_update(pargs_t *args, void *arg);
int cmd_change(pargs_t *args, void *arg);
void gnhastd_read_cb(struct bufferevent *in, void *arg);


#endif /*_COLLCMD_H_*/
