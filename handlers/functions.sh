#!/bin/ksh

# helper functions


# Handle a switch argument set of the form UID=TYPE
# example  switchoff 22:22:22=dimmer

switchoff() {
    TYPE=`echo $1 | cut -d = -f 2`
    DEVICE=`echo $1 | cut -d = -f 1`
    case $TYPE in
    switch)
	echo "chg uid:$DEVICE switch:0";;
    dimmer)
	echo "chg uid:$DEVICE dimmer:0.0";;
    esac
}

switchon() {
    TYPE=`echo $1 | cut -d = -f 2`
    DEVICE=`echo $1 | cut -d = -f 1`
    case $TYPE in
    switch)
	echo "chg uid:$DEVICE switch:1";;
    dimmer)
	echo "chg uid:$DEVICE dimmer:1.0";;
    esac
}