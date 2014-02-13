#!/bin/ksh

# A simple tool to add a handler to a device
NC="@NETCAT@ 127.0.0.1 2920"

if [ -z "$2" ]; then
    echo "Usage:"
    echo "$0 DEVICE handler"
    exit 1
fi

if [ ! -f "$2" ]; then
    echo "Handler $2 not found!"
    exit 1
fi

(
echo "client client:handler"
echo "mod uid:$1 handler:$2"
echo "disconnect") | $NC
