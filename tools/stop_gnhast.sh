#!/bin/sh

# script to stop the gnhast daemon, and associated handlers

if [ -f "@SYSCONFDIR@/gnhastd.run" ]; then
    . @SYSCONFDIR@/gnhastd.run
else
    echo "Don't know what to run, populate @SYSCONFDIR@/gnhastd.run"
    exit 1
fi

echo "Stopping gnhastd system"

# We stop the moncoll first, to avoid insanity
if [ "${MONCOLL}" = "yes" -o "${MONCOLL}" = "YES" ]; then
    if [ -n "${MONCOLL_PID}" ]; then
	PIDFILE=${MONCOLL_PID}
    else
	PIDFILE=@LOCALSTATEDIR@/run/moncoll.pid
    fi
    if [ -f "${PIDFILE}" ]; then
	kill -TERM `cat ${PIDFILE}`
    else
	echo "No pidfile for moncoll, not running? checking PS.."
	PID=`ps -ax | grep moncoll | grep -v grep | awk '{print $1}'`
	if [ -n "$PID" ]; then
	    kill -TERM ${PID}
	else
	    echo "moncoll not running, ignoring"
	fi
    fi
fi

# now the collectors

echo -n "Stopping collectors: "

for COLL in COLL1 COLL2 COLL3 COLL4 COLL5 COLL6 COLL7 COLL8 COLL9 COLL10 \
    COLL11 COLL12 COLL13 COLL14 COLL15 COLL16 COLL17 COLL18 COLL19 COLL20
do
    CMD_NAME=`eval "echo \\$${COLL}_CMD"`
    if [ -n "${CMD_NAME}" ]; then
	CMD_PIDFILE=`eval "echo \\$${COLL}_PID"`
	echo -n " ${CMD_NAME}"
	if [ -n "${CMD_PIDFILE}" ]; then
	    PIDFILE=${CMD_PIDFILE}
	else
	    PIDFILE=@LOCALSTATEDIR@/run/${CMD_NAME}.pid
	fi
	if [ -f "${PIDFILE}" ]; then
	    kill -TERM `cat ${PIDFILE}`
	else
	    echo
	    echo "No pidfile for ${CMD_NAME}, not running? checking PS.."
	    PID=`ps -ax | grep ${CMD_NAME} | grep -v grep | awk '{print $1}'`
	    if [ -n "$PID" ]; then
		kill -TERM ${PID}
	    else
		echo "${CMD_NAME} not running, ignoring"
	    fi
	fi
    fi
done
echo "."

if [ "${GNHASTD}" = "yes" -o "${GNHASTD}" = "YES" ]; then
    echo "Stopping gnhastd"
    if [ -n "${GNHASTD_PID}" ]; then
	PIDFILE=${GNHASTD_PID}
    else
	PIDFILE=@LOCALSTATEDIR@/run/gnhastd.pid
    fi
    if [ -f "${PIDFILE}" ]; then
	kill -TERM `cat ${PIDFILE}`
    else
	echo "No pidfile for gnhastd, not running? checking PS.."
	PID=`ps -ax | grep gnhastd | grep -v grep | awk '{print $1}'`
	if [ -n "$PID" ]; then
	    kill -TERM ${PID}
	else
	    echo "gnhastd not running, ignoring"
	fi
    fi
fi
echo "Done."