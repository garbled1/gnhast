```
brultech {
  hostname = "wezen"
  port = 80
  model = gem
  connection = net
  serialdev = "/dev/dty01"
}
gnhastd {
  hostname = "127.0.0.1"
  port = 2920
}
brulcoll {
  tscale = "F"
  update = 10
  pkttype = 8
}
```

# brulcoll section
## tscale (F|C)
Temperature scale, Farenhiet or Celcius
## update (seconds)
Update speed of the GEM in seconds
## pkttype (integer)
The packet format for the GEM.  Valid formats are: 8 for a 32-device GEM, or 4 for a 48-device GEM.

# gnhastd section
[gnhastd section](gnhastd_sec.md)

# brultech section
## hostname (IP)
IP or hostname of the GEM, defaults to 127.0.0.1
## port (port)
Port number of the GEM, defaults to 80
## model (GEM|ecm1240)
Only the GEM is currently supported
## connection (net|serial)
Currently only "net" is supported, future versions may include serial support for ecm1240's
## serialdev (path)
Device name for the serial device.  Currently unused, but future versions may support for ecm1240's

# device section
[device section](device.md)

# general options
## logfile (file)
You can override the default path of the logfile here. $PREFIX/var/log/brulcoll.log
## pidfile (file)
You can override the default path of the pid file here. $PREFIX/var/run/brulcoll.pid
