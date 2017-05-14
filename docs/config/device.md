# Example devices
```
device "26.BCA421010000" {
  name = "Big Aquarium Humidity"
  # loc = ""
  rrdname = "BAQ_Humid"
  subtype = humid
  type = sensor
  proto = sensor-owfs
  multimodel = "HIH4000"
}
device "1D.7EC20F000000-A" {
  name = "Lightning Detector A"
  loc = "1D.7EC20F000000"
  rrdname = "LightStrikeA"
  subtype = "counter"
  type = sensor
  proto = sensor-owfs
  multimodel = "counters.A"
}
```

# The device section

The device section starts with device "UID".  UID is a Unique ID.  This must be an ID number of some kind that will uniquely identify the device to gnhastd.  For most devices, you can leave this alone, but in a few certain types of devices, you may need to edit it.

## name
The friendly name of the device.  Enclose in quotes if you embed spaces.
## loc
Certain devices might have multiple sensors on the same address.  For example, a HobbyBoards lightning sensor has two counter chips, but only one one-wire address.  For gnhastd, you want each counter to be a separate device.  To do this, we create two entries for the lightning strike detector, with different UID's, and then in the loc field, we give the real address of the one-wire device.
## rrdname
If you wish to store your data in an rrd eventually, you can set rrdnames in the config file.  This will assist gnhastd/rrdcoll in creating config files automatically.  The rrdname does not need to be unique.  It corresponds to the DS in an rrd.
## subtype
The subtype of the device.  Examples: "counter", "temp", "lux".

[list of valid subtypes]

## type
The type of device.  Currently, switch, sensor, or dimmer.
## proto
The communications protocol for the device.  This is usually unique to the collector. For example, a one-wire device connected via owsrvcoll would use protocol "sensor-owfs".
## multimodel
Used in certain collectors, can be used to give additional information to the collector about the device.  In the example of the humidity sensor, it is used to decide which humidity node to read from the one-wire sensor.
## handler
Used only in gnhastd.  This is the path to a script, which will be executed when data is recieved from a collector pertaining to this device.  The script can be any executable, in any language.
## hiwat / lowat
High water / Low water marks.  A value used by gnhastd only, to determine if a handler should be fired. If the value of the sensed device exceeds hiwat, or is below lowat, the handler will be executed.  If both hiwat and lowat are 0.0, the handler will fire on every update.
