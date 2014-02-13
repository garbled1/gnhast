#!/bin/sh

# Handler for responding to something, and turning a device on or off
# for a specific period of time.
#
# Usage: timer time on|off device ...
# example:  timer '+15 minutes' on 4A:5C:44=switch 4A:5C:43=dimmer
# time must be a valid at(1) timespec

SAVEDARGS="$*"

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

# look for an existing timer, delete and reschedule
for i in `atq | tail +2 | awk '{print $5}'`
do
    JOB=`at -c $i | grep ^#GNTIMER | sed -e 's/^#GNTIMER //'`
    if [ "$JOB" = "$SAVEDARGS" ]; then
	atrm $i
    fi
done

# shift off arg 1 and 2
shift
shift

at $DELAY <<EOF
#GNTIMER $SAVEDARGS
$CMDX $*
exit 0
EOF
# flip the light
$CMD $*
exit 0