#!/bin/sh

# Handler for listening to a LUX sensor, and turning on/off a light
# in response.  Hand it a list of devices to turn on.

NC="@NETCAT@ 127.0.0.1 2920"
LIGHTLEVEL=500

#$UID is the uid of the sensor that fired
#$update is the update data
UID="$1"
read update
shift 1

# find lux val, strip off decimal point and detail
VAL=`echo $update | sed -e 's/.*lux://' -e 's/\.[0-9]*//`

(while [ "$1" != "" ]
do
    SWITCH="$1"
    shift 1
    # if it's light out, turn the light off
    if [ $VAL -gt $LIGHTLEVEL ]; then
	echo "chg uid:$SWITCH switch:0"
    fi

    if [ $VAL -le $LIGHTLEVEL ]; then
	echo "chg uid:$SWITCH switch:1"
    fi
done
echo "disconnect") | $NC