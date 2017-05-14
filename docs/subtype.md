# Adding a new subtype

To add a new subtype, you need to edit a few files. I'm listing them here for future convenience, as well as the function or type in the file that must be edited.

gnhast.h
 subtype enum
 device union

commands.h
 SC_COMMANDS enum

devices.c
 store_data_dev()
 get_data_dev()

netparser.c
 argtable[]
 find_arg_bydev()

collcmd.c
 cmd_update()

confparser.c
 conf_parse_subtype()
 conf_print_subtype()

cmdhandler.c
 cmd_update()
 cmd_change()
