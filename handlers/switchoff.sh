#!/bin/sh

# Accept a file or list of devices
# which is a list of devices we want to switch off in the form
# UID=<device|switch>
# example:
# 4A:5C:44=switch
# Usage: switchoff.sh [ -f <listfile> ] [device ...]
# example: switchoff.sh -f file
# example: switchoff.sh 4A:5C:44=switch 4A:5C:43=dimmer
# you may combine -f and devices in the same call

NC="@NETCAT@ 127.0.0.1 2920"

while getopts f: ARG
do
    case $ARG in
    f) FILENAME=$OPTARG;;
    esac
done
shift $(expr $OPTIND - 1)

if [ -n "$FILENAME" -a -r "$FILENAME" ]; then
    DEVS=`(cat $FILENAME ; echo $*)`
else
    DEVS=$*
fi

(for DEV in $DEVS
do
    TYPE=`echo $DEV | cut -d = -f 2`
    DEVICE=`echo $DEV | cut -d = -f 1`
    case $TYPE in
    switch)
	echo "chg uid:$DEVICE switch:0";;
    dimmer)
	echo "chg uid:$DEVICE dimmer:0.0";;
    esac
done
echo "disconnect") | $NC