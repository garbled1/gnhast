Setting up the insteon devices is a bit more complex, as they are inherently more complex devices.  There are a few steps to this process.

# "discover" insteon devices.
There is no way to actually probe a network for insteon devices.  You must write down the addresses of all the devices as you install them.  Make a list of device addresses, in the form:
```
ABCDEF
123456
AC12FC
```

And save that in a file.  Then run insteon_discover -f (file from above) -s /dev/serialport -m file.conf
This will connect to each device in the file, send an ALL LINK request to that device, linking it to the PLM, and then write a conf file out that can be used as an initial insteoncoll.conf file.

The insteon_discover process needs to find the insteon.db configuration file in $PREFIX/etc/insteon.db in order to properly identify devices.  If you have a device it cannot identify, you may add an entry to this file.  An example insteon.db is below:

```
model "2477D" {
        name = "SwitchLinc Dimmer (Dual-Band) (600W)"
        devcat = 0x01
        subcat = 0x20
        type = "dimmer"
        subtype = "switch"
}
model "2475DA1" {
        name = "In-LineLinc Dimmer"
        devcat = 0x01
        subcat = 0x1A
        type = "dimmer"
        subtype = "switch"
}
model "2486DWH6" {
        name = "KeypadLinc Dimmer, 6-button"
        devcat = 0x01
        subcat = 0x1B
        type = "dimmer"
        subtype = "switch"
}
```

# edit generated insteoncoll.conf file, an install

You now need to edit the conf file the previous program generated, and install it as $PREFIX/insteoncoll.conf.  You will likely only need to edit the [gnhastd section].

```
gnhastd {
  hostname = "127.0.0.1"
#  hostname = "192.168.10.1"
  port = 2920
}
insteoncoll {
  device = "/dev/ttyU2"
}
device "1C.AF.58" {
  name = "Spa Room - Spa Lights SwitchLinc Dimmer"
  loc = "1C.AF.58"
  # rrdname = ""
  subtype = switch
  type = dimmer
  proto = insteon-v2
  # multimodel = ""
  # handler = ""
  hiwat = 0.000000
  lowat = 0.000000
}
```

# Conf file format
## gnhastd section
[gnhastd section]
## device section
[device section]
## insteoncoll section
### device (path)
Pathname of serial device PLM is connected to
## general options
## logfile (file)
You can override the default path of the logfile here. $PREFIX/var/log/insteoncoll.log
## pidfile (file)
You can override the default path of the pid file here. $PREFIX/var/run/insteoncoll.pid


# insteon_aldb program

The insteon_aldb program lets you directly edit the ALDB of a device.  The first mode of operation queries the device, and dumps the ALDB to a file:

  insteon_aldb -a AB12CD -s /dev/serial -f dumpfile

This will query device AB12CD, and generate a file named dumpfile with the ALDB.  Example:
```
1C.17.A0 03 32 18 00 A2
1C.17.A0 06 FE 1C 00 A2
1C.17.A0 04 7D 18 00 A2
1C.17.A0 01 00 1A 00 A2
1C.17.A0 05 C8 18 00 A2
1C.17.A0 03 32 18 00 A2
1B.D4.4C 01 00 1A 00 E2
1B.FA.84 75 00 1A 00 A2
1B.FA.84 76 00 1A 00 E2
00.00.00 00 00 00 00 00
```

The format is:
address of linked device, group number, link data 1, link data 2, link data 3, link flags.
The last line should be all zeros.

Note, that when editing the ALDB file, you should keep the number of lines the same. If you wanted to delete line 4, add an additional line of all zeros at the end, so the DB is properly wiped.  The program simply writes the new DB to the device, and does not attempt to figure out what you meant for it to do.

To write the new ALDB to your device:

  insteon_aldb -a AB12CD -s /dev/serial -w -f aldbfile

This will write the new ALDB record to your device.