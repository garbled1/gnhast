#!/bin/sh

# Handler for listening to a sunrise sensor, and turning on/off a light
# in response.  Hand it a list of devices to turn on/off.
# Additionally, look in @SYSCONFDIR@/*.list files
# for a list of devices to turn on/off at day/night.

# hargs format is uid=type (22:22:22=switch)
# list format is same

# note, this is the newer script, which listens to a device of type
# SUBTYPE_DAYLIGHT

#enum DAYLIGHT_TYPES {
#	DAYL_NIGHT, /* 0 */
#	DAYL_DAY, /* 1*/
#	DAYL_SOLAR_NOON,   2
#	DAYL_DAWN_ASTRO_TWILIGHT,  3
#	DAYL_DAWN_NAUTICAL_TWILIGHT,  4
#	DAYL_DAWN_CIVIL_TWILIGHT,  5
#	DAYL_DUSK_CIVIL_TWILIGHT,  6
#	DAYL_DUSK_NAUTICAL_TWILIGHT,  7
#	DAYL_DUSK_ASTRO_TWILIGHT,  8
#};
# Overridden by @SYSCONFDIR@/nightlight2.conf  format MORNING:NIGHT (3:6)
MORNING=3
NIGHT=6
if [ -f "@SYSCONFDIR@/nightlight2.conf" ]; then
    MORNING=$(cat @SYSCONFDIR@/nightlight2.conf | cut -d : -f 1)
    NIGHT=$(cat @SYSCONFDIR@/nightlight2.conf | cut -d : -f 2)
fi

NC="@NETCAT@ 127.0.0.1 2920"
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

# find daylight val, strip off decimal point and detail
VAL=`echo $update | sed -e 's/.*daylight://' -e 's/\.[0-9]*//'`
DAY=0
# If it's day, or noon, or between morning and night, it's day
if [ ${VAL} -eq 1 -o ${VAL} -eq 2 -o ${VAL} -ge ${MORNING} \
    -a ${VAL} -lt 6 -o ${VAL} -gt 5 -a ${VAL} -lt ${NIGHT} ]; then
        DAY=1
fi
# if it's night, or between night and morning, it's night.
if [ ${VAL} -eq 0 -o ${VAL} -ge ${NIGHT} -o ${VAL} -gt 2 \
    -a ${VAL} -lt ${MORNING} ]; then
        DAY=0
fi

(
echo "client client:handler"
# loop through the hargs
while [ "$1" != "" ]
do
    SWITCH="$1"
    shift 1
    # if it's light out, turn the light off
    if [ ${DAY} -eq 1 ]; then
	switchoff $SWITCH
    fi
    # night, turn it on
    if [ ${DAY} -eq 0 ]; then
	switchon $SWITCH
    fi
done

# now do the files
# It's morning
if [ ${DAY} -eq 1 ]; then
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
if [ ${DAY} -eq 0 ]; then
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