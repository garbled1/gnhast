```
network {
  listen = "0.0.0.0"
  port = 2920
}
infodump = 600
```

# network section
## listen (ip)
The listen address.  The IP of an interface, or 127.0.0.1, or 0.0.0.0 to listen on all.
## sslport (port)
For future use of SSL, defaults to 2921
## port (port)
For the unsecured port, defaults to 2920
## certchain (file)
file containing the certchain. (for future SSL use)
## privkey (file)
file containing the private key. (for future SSL use)
## usessl (true/false)
Set to true if you want to enable the SSL port (currently broken, default false)
## usenonssl (true/false)
Set to true to enable the unsecured port. (default true)

# device section
You may include a device section with the standard [device section] definitions.  You may define as many devices as you like here.

# general options
## devconf (file)
Pathname to the devices.conf file, defaults to $PREFIX/etc/devices.conf
## devconf_update (seconds)
Gnhastd will auto-save the devices.conf file every 300 seconds by default.  This way you can set up collectors, have them tell gnhastd about the devices, and have it just magically work.
## infodump (seconds)
By default, every 600 seconds gnhastd will dump statistics to the logfile, you may change that value here.
## include(file)
You may include a config file here.  Generally you want to include the devices.conf file here, so it is loaded automatically.  Format is: include(/usr/local/etc/devices.conf)
## logfile (file)
You can override the default path of the logfile here. $PREFIX/var/log/gnhastd.log
## pidfile (file)
You can override the default path of the pid file here. $PREFIX/var/run/gnhastd.pid
