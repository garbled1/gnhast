```
#
owserver {
  hostname = "127.0.0.1"
  port = 4304
}
gnhastd {
  hostname = "127.0.0.1"
  port = 2920
}
owsrvcoll {
  tscale = "F"
  update = 60
  rescan = 30
}
device "10.4ED0A0020800" {
  name = "Big Aquarium LED Wall Temp"
  # loc = ""
  rrdname = "BAQ_WallTemp"
  subtype = temp
  type = sensor
  proto = sensor-owfs
  # multimodel = ""
}
device "1D.7EC20F000000-B" {
  name = "Lightning Detector B"
  loc = "1D.7EC20F000000"
  rrdname = "LightStrikeB"
  subtype = "counter"
  type = sensor
  proto = sensor-owfs
  multimodel = "counters.B"
}
```

# owserver section
## hostname (host)
IP or hostname of OWFS owserver, defaults to 127.0.0.1
## port (port)
Port number of OWFS owserver, defaults to 4304

# gnhastd section
[gnhastd section]

# device section
[device section]

# owsrvcoll section
## tscale (F|C|K|R)
```
Temperature scale one of:
F = Farenheit
C = Celcius
K = Kelvin
R = Rankine
```
## update (seconds)
Seconds between device queries.  Defaults to 60
## rescan (loops)
Every X updates, the system will ask the owserver for a list of devices, and if new ones are found, start probing them.  Defaults to 15.

# general options
## logfile (file)
You can override the default path of the logfile here. $PREFIX/var/log/owsrvcoll.log
## pidfile (file)
You can override the default path of the pid file here. $PREFIX/var/run/owsrvcoll.pid
