#!/bin/sh

# Handler for responding to something, and turning a device on or off
# for a specific period of time.
#
# This version checks if the light is already in the deisred state first.
#
# Usage: timer time on|off device ...
# example:  timer '+15 minutes' on 4A:5C:44=switch 4A:5C:43=dimmer
# time must be a valid at(1) timespec

SAVEDARGS="$1 $2"
NC="@NETCAT@ 127.0.0.1 2920"

if [ $# -lt 3 ]; then
    exit 1
fi

DELAY=$1
ONOFF=`echo $2 | tr '[:lower:]' '[:upper:]'`
if [ "$ONOFF" = "ON" ]; then
    CMD=@GNHANDLERDIR@/switchon
    CMDX=@GNHANDLERDIR@/switchoff
else
    CMD=@GNHANDLERDIR@/switchoff
    CMDX=@GNHANDLERDIR@/switchon
fi

# shift off arg 1 and 2
shift
shift

for dev in $*
    do
    UID=`echo $i | cut -d = -f 1`
    TYPE=`echo $i | cut -d = -f 2`
    SKIP="NO"
    SKIPCMD="NO"
    TIMERRESET="0"

    # look for an existing timer, delete and reschedule
    for i in `atq | tail +2 | awk '{print $5}'`
    do
	JOB=`at -c $i | grep ^#GNTIMER | sed -e 's/^#GNTIMER //'`
	if [ "$JOB" = "$SAVEDARGS $dev" ]; then
	    atrm $i
	    TIMERRESET=1
	fi
    done

    # get the current state of the switch
    STATE=$((echo "ask uid:$i" ; \
	echo "disconnect") | $NC | sed -e "s/.*${TYPE}://")

    # should we skip this one?
    if [ "$TIMERRESET" = "0" ]; then
	# if it's on, and we want it on, leave it alone. and vice versa
	if [ "$ONOFF" = "ON" -a "$STATE" = "1" ];
	    SKIP="YES"
	fi
	if [ "$ONOFF" = "OFF" -a "$STATE" = "0" ];
	    SKIP="YES"
	fi
    else
	# TIMERRESET=1
	if [ "$ONOFF" = "ON" -a "$STATE" = "1" ];
	    SKIPCMD="YES"
	fi
	if [ "$ONOFF" = "OFF" -a "$STATE" = "0" ];
	    SKIPCMD="YES"
	fi
    fi

    if [ "$SKIP" = "NO" ]; then
	# set the timer
    
	at $DELAY <<EOF
#GNTIMER $SAVEDARGS
$CMDX $dev
exit 0
EOF
	# flip the light
	if [ "$SKIPCMD" = "NO" ]; then
	    $CMD $dev
	fi
    fi
done

exit 0