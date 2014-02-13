#!/bin/ksh

# A simple tool to show or modify hargs

NC="@NETCAT@ 127.0.0.1 2920"

if [ -z "$2" ]; then
    echo "Usage:"
    echo "$0 DEVICE ask|mod [hargs,hargs,...]"
    exit
fi

if [ "$2" = "ask" ]; then
    (
    echo "client client:handler"
    echo "ask uid:$1 hargs:x"
    echo "disconnect") | $NC | sed -e 's/.* hargs:\(.*\) .*/\1/'
    exit
fi

if [ "$2" = "mod" ]; then
    if [ -z "$3" ]; then
	echo "Need hargs list with mod command"
	exit
    fi
    (
    echo "client client:handler"
    echo "mod uid:$1 hargs:$3"
    echo "disconnect") | $NC
fi