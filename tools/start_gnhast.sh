#!/bin/sh

# script to start the gnhast daemon, and associated handlers

if [ -f "@SYSCONFDIR@/gnhastd.run" ]; then
    . @SYSCONFDIR@/gnhastd.run
else
    echo "Don't know what to run, populate @SYSCONFDIR@/gnhastd.run"
    exit 1
fi

echo "Starting gnhastd system"

if [ "${GNHASTD}" = "yes" -o "${GNHASTD}" = "YES" ]; then
    @BINDIR@/gnhastd ${GNHASTD_ARGS}
    echo -n "Starting gnhastd, sleeping 10 seconds ..."
    sleep 10
    echo " done"
fi

# now the collectors

echo -n "Starting collectors: "

for COLL in COLL1 COLL2 COLL3 COLL4 COLL5 COLL6 COLL7 COLL8 COLL9 COLL10 \
    COLL11 COLL12 COLL13 COLL14 COLL15 COLL16 COLL17 COLL18 COLL19 COLL20
do
    CMD_NAME=`eval "echo \\$${COLL}_CMD"`
    if [ -n "${CMD_NAME}" ]; then
	CMD_ARGS=`eval "echo \\$${COLL}_ARGS"`
	echo -n " ${CMD_NAME}"
	@BINDIR@/${CMD_NAME} ${CMD_ARGS}
	sleep 1
    fi
done
echo "."

if [ "${MONCOLL}" = "yes" -o "${MONCOLL}" = "YES" ]; then
    echo -n "Sleeping 60 seconds before starting moncoll ..."
    sleep 60
    @BINDIR@/moncoll ${MONCOLL_ARGS}
    echo " done"
fi