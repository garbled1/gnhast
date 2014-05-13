#!/bin/ksh

CURL="@CURL@"
RRDTOOL="@RRDTOOL@"
NOTIFY_LISTEN="@BINDIR@/notify_listen"

usage() {
    echo "Usage: $0 [-b] -r rrd_file -v venstar_name"
    echo " -b : build new rrd db for this device"
    exit 2
}

args=`getopt br:v: $*`
if [ $? -ne 0 ]; then
    usage
fi
set -- $args
while [ $# -gt 0 ]; do
    case "$1" in
	-b)
	    RRDNEW=1
	    ;;
	-r)
	    RRDFILE=$2; shift
	    ;;
	-v)
	    VENSTARNM=$2; shift
	    ;;
	--)
	    shift;
	    break
	    ;;
     esac
     shift
done

if [ -z "$RRDFILE" ]; then
    usage
fi
if [ -z "$VENSTARNM" ]; then
    usage
fi

# takes 90 seconds, sorry
URL=$($NOTIFY_LISTEN -t 90 2> /dev/null | grep -B 1 name:${VENSTARNM}: | grep ^Location | sort -u | awk '{print $2}')
#URL="http://192.168.10.197/"
sleep 2


# Build new rrd if requested
if [ -n "${RRDNEW}" ]; then
    STARTTIME=$($CURL -s ${URL}query/runtimes | awk -F { '{print $3}' | cut -d : -f 2 | cut -d , -f 1)
    sleep 2
    $RRDTOOL create $RRDFILE --start ${STARTTIME} --step 86400 \
	DS:heat1:GAUGE:172800:0:1440: \
	DS:heat2:GAUGE:172800:0:1440: \
	DS:cool1:GAUGE:172800:0:1440: \
	DS:cool2:GAUGE:172800:0:1440: \
	DS:aux1:GAUGE:172800:0:1440: \
	DS:aux2:GAUGE:172800:0:1440: \
	DS:fc:GAUGE:172800:0:1440: \
	RRA:AVERAGE:0.5:1:7300: \
	RRA:AVERAGE:0.5:7:1040: \
	RRA:AVERAGE:0.5:30:240: \
	RRA:MIN:0.5:1:7300: \
	RRA:MIN:0.5:7:1040: \
	RRA:MIN:0.5:30:240: \
	RRA:MAX:0.5:1:7300: \
	RRA:MAX:0.5:7:1040: \
	RRA:MAX:0.5:30:240: \
	RRA:LAST:0.5:1:1:

	TMPFILE=$(mktemp /tmp/${0##*/}.XXXXXX) || exit 1
	$CURL -s ${URL}query/runtimes | awk 'BEGIN{RS="{"}{print $1}' | grep '"ts"' >> $TMPFILE
	sleep 2
	Y=$(cat $TMPFILE | wc -l | awk '{print $1}')
	let X=${Y}-1
	for line in `head -n $X $TMPFILE`
	do
	    DATA=$(echo $line | sed -e 's/"//g' -e 's/,//g' -e 's/ts://' -e 's/}//g' -e 's/]//g' -e 's/heat[12]//g' -e 's/cool[12]//g' -e 's/aux[12]//g' -e 's/fc//')
	    $RRDTOOL update $RRDFILE $DATA
	done

	rm -f $TMPFILE
	exit 0
fi

# otherwise, we just want the data
DATALINE=$($CURL -s ${URL}query/runtimes | awk -F { '{print $(NF-1)}' | sed -e 's/"//g' -e 's/,//g' -e 's/ts://' -e 's/}//g' -e 's/]//g' -e 's/heat[12]//g' -e 's/cool[12]//g' -e 's/aux[12]//g' -e 's/fc//')

$RRDTOOL update $RRDFILE $DATALINE