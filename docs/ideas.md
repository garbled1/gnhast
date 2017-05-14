### Timers

For example, you want a light to turn on every night at 1am, and turn off at 2 am.  Rather than build a timer system into gnhast, the UNIX way to accomplish this, is to simply setup a cronjob. The handlers directory provides a switchon/switchoff handler.  So we might setup a cron like so:
```
    0 1 * * * /usr/local/libexec/gnhast/switchon 4A:5C:44=switch
    0 2 * * * /usr/local/libexec/gnhast/switchoff 4A:5C:44=switch
```
But what if we want to do something more complex?  Like maybe we want a light to be activated by a handler, like a motion sensor, but then shut off in 15 minutes.  (I'll have to write a good example handler for this, it's easy enough to do).  We write a handler script that turns the light on, and then immediately schedules an atjob 15 minutes out to run the "switchoff" handler.

### Example handlers

The handlers directory contains a few example, and perfectly usable handlers.  One such handler is the "nightlight".  This is a simple handler that responds to a lux sensor, and is used to turn lights on and off.  You can set this handler up very simply.

First, find your lux sensor in the devices.conf file, and add the full path to the nightlight script to the handler config entry.  Then, set hargs to a comma separated list of the devices you wish to turn on/off with the result of the sensor.  Finally, set the lux level that will activate the threshold in hiwat and lowat.  Make sure you edit the threshold in the nightlight script to match this.
```
    device "26.A43328010000" {
    name = "Outside LUX"
    handler = "/usr/local/libexec/gnhast/nightlight"
    hargs = {"25.98.9B", "25.9B.6D"}
    spamhandler = no
    hiwat = 250.000000
    lowat = 250.000000
    }
```
The spamhandler directive, when set to yes, will call the handler every time the device updates above or below the water mark. When set to no, it will only call when a device crosses a water mark.

So, in the above example, when the light level crosses 250, nightlight will be called, and if it's above 250, it will turn the devices in hargs off, and if below, it will turn them on.

This script could easily be rewritten to accomodate other sensor types. For example, you could use a wind sensor's speed to determine if you should raise an outdoor shade to prevent damage to it.  Set a high watermark, of say 10 (mph) and then have it activate the device that raises the shade.