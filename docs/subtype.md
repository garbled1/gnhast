# Adding a new subtype

To add a new subtype, you need to edit a few files. I'm listing them here for future convenience, as well as the function or type in the file that must be edited.

common/gnhast.h
* SUBTYPE_TYPES enum
* device_t union
* bump GHNHASTD_PROTO_VERS

common/commands.h
* SC_COMMANDS enum

common/devices.c
* store_data_dev()
* get_data_dev()

common/netparser.c
* argtable[]
* find_arg_bydev()

common/collcmd.c
* cmd_update()

common/confparser.c
* conf_parse_subtype()
* conf_print_subtype()

gnhastd/cmdhandler.c
* cmd_update()
* cmd_change()

common/gncoll.c
* Only if a scaled type, like pressure
