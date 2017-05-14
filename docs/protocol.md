# Protocol Design

## Example session
The below sequence, tells gnhastd that the dimmer device 1B.D4.4C is currently set to 0.5, or 50%, and then disconnects from gnhastd.
```
upd uid:1B.D4.4C dimmer:0.5
disconnect
```

The below sequence tells gnhastd to find the collector for the 1B.D4.4C device, and to request it to change the status of the switch to ON, and then disconnects from gnhastd.  This is a typical sequence you could use in a script via netcat, to turn a light on.
```
chg uid:1B.D4.4C dimmer:1.0
disconnect
```

## Server Commands

### reg
Register a device with the server, or, tell the collector about a device

### regg
Register a group with the server, or, tell the collector about a group

### upd
Update the status of a device.  Sent to, and by the server

### mod
Modify a setting of a device.  (Currently only name, rrdname, hargs, and handler).  Sent to and by the server.

### chg
Request a change in the status of a device.  For example, ask for an outlet to be turned off.  Sent to and by the server.

### ldevs
Request a list of devices from the server.  Can give it arguments such as protocol, type, etc, to narrow the list.  Devices are sent back to the collector as reg commands.

### endldevs
Server sends this to let the client know we are done sending it device names from an ldevs.

### lgrps
Request a list of groups from the server.  Groups are sent back as regg commands.

### enlgrps
Server sends this to let the client know we are done sending it group names from an lgrps.

### feed
Request a continuous stream of updates from the server for a particular device.  rate argument sets the update speed in seconds.  Updates are sent via the upd comamnd.

### ask
Ask for a single upd on a device, or devices

### cactiask
Ask for a single upd on a device, or devices, but reply in cacti format.

### disconnect
Disconnect form the gnhastd server

### client
Tell the server the name of our client (needs client arg)

## Arguments

### uid
The unique identifier for this device

### name
The human readable name for this device

### rrdname
The name of the field to store this value in, in an rrd file

### rate
The rate in seconds of an update (used by feed) (integer)

### devt
The device type.  (integer)

### proto
The device protocol. Integer

### subt
The device subtype.  Integer

### switch
The status of a switch.  0=off, 1=on. Integer

### dimmer
Status of a dimmer.  0.0-1.0 float

### temp
Temperature.  Float

### humid
Humidity.  Float

### lux
Light level in LUX. Float

### pres
Pressure.  (air pressure)  Float.

### speed
A speed value, used for windspeed for now. Float.

### dir
Wind direction

### count
A count, for a counter type device.  Integer.

### wet
Wetness, for leaf wetness sensors.  Float.

### moist
Moisture.  For soil moisture sensors.  Float.

### wsec
Wattseconds.  (aka joules)  Long long.

### volts
Voltage.  Float.

### watt
Wattage. Float.

### amps
Amperage.  Float.

### rain
Rain rate.  Float.

### weather
For weather stations that report cloudy/overcast/etc.  Integer, reports values of:
0 - sunny
1 - partly cloudy
2 - cloudy
3 - rainy

### client
Client name.  Used by collectors so gnhastd knows which collector is on which connection.

### scale
Used to determine scale of incoming data, or set scale of outgoing data. Currently supports the following scale values:  temperature, barometer, length, speed, light.  All scale values cont from zero, so, for temperature in celcius, send a "1" as the value.  Integer.

#### temperature scales
Currently F, C, K, R. (count from 0)

#### Barometer scales
Currently Inches, millimeters, millibars (of mercury for the in/mm).

#### Length scales
Currently inches or millimeters

#### Speed scales
MPH, Knots, Meters/Sec, KPH.

#### Light scales
Lux, Watts/Meter squared

### hiwat
Sensor high water mark. Float.

### lowat
Sensor Low water mark.  Float

### handler
Path to a handler routine.  String.

### hargs
Handler arguments.  comma separated string of arguments.  String.  Example:
argument1,argument2

### flow
Flow rate. Float

### distance
Distance. Float

### alarm
For alarm devices.  Integer, reports values of:
0 - Alarm Ready
1 - Alarm Stay
2 - Alarm Night/Stay
3 - Alarm Instant Max
4 - Alarm Away
5 - Alarm Fault

### number
int64 number

### pct
Percentage. Float

### volume
Sound volume.  Float

### timer
Countdown timer.  Counts to zero automatically in seconds.  Unsigned int

### thmode
Thermostat mode.  Integer, reports values of:
0 - Off
1 - Heat
2 - Cool
3 - Auto

### thstate
Themostat state.  Integer, reports values of:
0 - Idle
1 - Heating
2 - Cooling
3 - Lockout
4 - Error

### smnum
Small number. 0-255

### glist
Group list.  Comma separated list of group UID's

### dlist
Device list.  Comma separated list of device UID's