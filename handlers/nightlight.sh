#!/bin/sh

# Handler for listening to a LUX sensor, and turning on/off a light
# in response.  Hand it a list of devices to turn on/off.
# Additionally, look in @SYSCONFDIR@/*.list files
# for a list of devices to turn on/off at day/night.

# hargs format is uid=type (22:22:22=switch)
# list format is same

NC="@NETCAT@ 127.0.0.1 2920"
LIGHTLEVEL=250
OFFLISTNIGHT="@SYSCONFDIR@/nightoff.list"
ONLISTNIGHT="@SYSCONFDIR@/nighton.list"
OFFLISTDAY="@SYSCONFDIR@/dayoff.list"
ONLISTDAY="@SYSCONFDIR@/dayon.list"

# pull in the handler functions
. @GNHANDLERDIR@/functions

#$UID is the uid of the sensor that fired
#$update is the update data
UID="$1"
read update
shift 1

# find lux val, strip off decimal point and detail
VAL=`echo $update | sed -e 's/.*lux://' -e 's/\.[0-9]*//'`

(
echo "client client:handler"
while [ "$1" != "" ]
do
    SWITCH="$1"
    shift 1
    # if it's light out, turn the light off
    if [ $VAL -gt $LIGHTLEVEL ]; then
	switchoff $SWITCH
    fi

    if [ $VAL -le $LIGHTLEVEL ]; then
	switchon $SWITCH
    fi
done
# It's morning
if [ $VAL -gt $LIGHTLEVEL ]; then
    if [ -f "$OFFLISTDAY" ]; then
	for DEV in `cat $OFFLISTDAY`
	do
	    switchoff $DEV
	done
    fi
    if [ -f "$ONLISTDAY" ]; then
	for DEV in `cat $ONLISTDAY`
	do
	    switchon $DEV
	done
    fi
fi
# it's night
if [ $VAL -le $LIGHTLEVEL ]; then
    if [ -f "$OFFLISTNIGHT" ]; then
	for DEV in `cat $OFFLISTNIGHT`
	do
	    switchoff $DEV
	done
    fi
    if [ -f "$ONLISTNIGHT" ]; then
	for DEV in `cat $ONLISTNIGHT`
	do
	    switchon $DEV
	done
    fi
fi

echo "disconnect") | $NC