#!/bin/sh

# Build a config entry for an ad2usb RFX device.  We do not automatically
# build these inside the collector, because you tend to pick up your
# neighbor's house with them, and it would make a mess of things.
#
# Will generate a set of config file entries to stdout.  Append to a config
# file and restart ad2usb.
#
# Usage: mk_rfx_ad2usb <serial #> <Sensor Names>
# Enclose long names in double quotes.

if [ -z "$1" ]; then
    echo "No serial # given"
    echo "Usage: mk_rfx_ad2usb <serial #> <Sensor Names>"
    exit 1
fi

for i in 1 2 3 4 lowbat
do
    echo "device \"ad2usb-rfx-$1-$i\" {"
    if [ -z "$2" ]; then
	echo "  #name = \"\""
    else
	echo "  name = \"$2\""
    fi
    echo "  loc = \"ad2usb-rfx-$1-$i\""
    echo "  #rrdname = \"\""
    echo "  subtype = switch"
    echo "  type = sensor"
    echo "  proto = ad2usb"
    echo "}"
    echo
done