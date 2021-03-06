Getting Started
===============

# Compile and install
First, you will need to compile and install gnhast.  You should just need to run ./configure and make, followed by make install.  The default install prefix is /usr/local. You may want to create /usr/local/var/log, and /usr/local/etc, if they do not exist.

# Build config files for the collectors

## General config file format
The general config file format is pretty simple.  An example config is pasted below:
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
```

* [device section](config/device.md) - Details on the device section of the config file
* [gnhastd section](config/gnhastd_sec.md) - Details on the gnhastd section of the config file

Other sections are detailed in the documentation for the collector config files below.

## Generate basic gnhastd config file
The next thing you need to do, is build configuration files for all of your collectors, and gnhastd itself. You should start with a very basic config for gnhastd.  Decide where your gnhastd server will run, and if it needs to be accessable from outside it's host. By default, gnhastd listens on port 2920, localhost (127.0.0.1).  If you wish for it to accept connections from other hosts, you will probably want to set the listen address to 0.0.0.0.

[gnhastd config file format](config/gnhastd.md)

## Generate owsrvcoll config file

If you have one-wire devices connected to your system, you will want to run the owsrvcoll collector.  First, install and configure OWFS, and run the owserver.  http://www.owfs.org
Once you have that running, you can connect to it, and generate an initial owsrvcoll config file.  owsrvcoll assumes that the owserver process is on the same host (127.0.0.1) and is running on the standard owserver port (4304).  If you run "owsrvcoll -m file.conf", the collector will connect to a local owserver, issue a command to get a list of devices from the server, and write the config file to "file.conf".  You can then copy this generated config file to $PREFIX/etc/owsrvcoll.conf and edit it.  Note that for certain devices (counters, humidity probes, pressure probes, etc) you will need to edit the conf file so the collector knows how to get the data.

[owsrvcoll config file format](config/owsrvcoll.md)

## Generate brulcoll config file

For the brultech GEM, you will need to create a basic config file with the hostname/IP of the brultech GEM, and then use that to generate the initial config file.  Create a config file similar to:

```
brultech {
  hostname = 192.168.1.45
}
```
Then run brulcoll -c conffile -m file.conf

This will connect to the brultech GEM, probe all the devices connected to it, and write out a new config at file.conf.  You may then copy this to $PREFIX/etc/brulcoll.conf and edit to taste.

[brulcoll config file format](config/brulcoll.md)

## Setup the insteon stuff

Setting up the insteon devices is a bit more complex, and it has it's own section.
[insteon collector](config/insteon.md)

## Setup the web interface

This also has it's own section.
[gnhastweb](gnhastweb.md)