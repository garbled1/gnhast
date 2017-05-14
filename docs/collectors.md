#Overview of the gnhast collectors

##fakecoll - Generic fake collector

Fakecoll is the most basic collector.  It emulates a switch, a dimmer, and a temperature probe, and randomly sends status updates to the server for them.  It is designed to be used to test the system.  It is not particularly complex, so you could easily edit it to add more devices, so you could test scripts out with it.

##owsrvcoll - One Wire collector

The one wire collector collects data from one-wire devices attached to a owfs owserver.  owserver handles all the hardware interfacing, and owsrvcoll polls the owserver for status updates.  It takes all of this data, and hands it off to the gnhastd core at a per-device configurable time interval.

##rrdcoll - RRDtool collector

The RRD collector contacts the server, and asks for a feed of devices it wishes to record data for.  It then takes the data given to it by gnhastd at regular intervals, and inserts it into rrd databases for each device.

##brulcoll - Brultech collector

The Brultech collector is used to collect power usage data, temperature information, and pulse counter data from a Brultech GreenEye Monitor (GEM).  It also has preliminary support for an ECM1240. http://www.brultech.com/

##insteoncoll - Insteon collector

The insteon collector is used to collect state data from insteon devices, and control them.  It does so via a PLM. Currently only switches/dimmers/outlets are supported, and it has only been tested on a serial PLM.  Version 2 and Version 2 CS devices are supported and working.
[insteon collector] - Documentation on the insteon tools

##wmr918coll - wx200 / wmr918 collector

Collects data from a wx200 weatherstation.  Currently handles a wx200 via serial, or, a network connection to wx200d for collection from any device that program supports.  Native WMR918 serial support is also supported, and includes low-battery sensors for the probes.

##wupwscoll - Personal Weather Station collector

Submits data from gnhast to a PWS site.  Currently supports http://pwsweather.com and http://weatherunderground.com.  Can handle rapid fire on weather underground, assuming your sensors are that fast.

##ad2usbcoll - AD2USB Collector

Connects to the AD2USB device from Nutech that allows programming and monitoring of a Honeywell Vista alarm system.  Can read all alarm states, as well as wireless devices.

##icaddycoll - Irrigation Caddy collector

Polls the Irrigation Caddy to determine which zones are running.  Can activate zones and programs on-demand.

##venstarcoll - Venstar T5800/T5900 collector

Polls the Venstar Thermostat and collects temperature data.  Can turn the thermostat on/off, control the fan, set scheduling on/off, set away state, and modify the setpoints.  Also receives the alert statuses from filter/uv/service alarms.