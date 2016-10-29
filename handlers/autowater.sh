#!/bin/ksh

#automatically water things based on feedback from moisture sensors

#setup values
NC="@NETCAT@ 127.0.0.1 2920"
WATERTRIGGER="21"
NIGHTTRIGGER="19"
LOGFILE="@LOCALSTATEDIR@/log/autowater.log"

#Yard Maintence day.  Start and end are in 24hr format, hour only.
# thursday, 6am->6pm
BLACKOUT_DAY="4"
BLACKOUT_START=6
BLACKOUT_END=18

#Zone sensors
SENSOR[0]="EF.F49020150000-m0"
SENSOR[1]="EF.F49020150000-m1"
SENSOR[2]="EF.F49020150000-m2"
SENSOR[3]="EF.F49020150000-m3"

#sprinkler rows
SPRINKLER_ROW[0]="MAIA-zone08"
SPRINKLER_ROW[1]="MAIA-zone04"
SPRINKLER_ROW[2]="MAIA-zone05"
SPRINKLER_ROW[3]="MAIA-zone06"
SPRINKLER_ROW[4]="MAIA-zone07"
SPRINKLER_PROGRAM="MAIA-progrunning"
SPRINKLER_PROGRAM_ENABLE="number:4"

#Affected zone runs in seconds per zone
SEN[0]="900 600   0   0   0"
SEN[1]="  0 900 600 600   0"
SEN[2]="  0 600 600 900   0"
SEN[3]="  0   0   0 600 900"

# If this is non-null, we have a night setting
LUX_SENSOR="26.A43328010000"
LIGHTLEVEL=1000

DAY=`date +"%u"`
HOUR=`date +"%k" | awk '{print $1}'`
TIME=`date +"%b %e %H:%M:%S"`

# first, is it a blackout time?
if [ "$DAY" = "$BLACKOUT_DAY" ]; then
    if [ $HOUR -ge $BLACKOUT_START -a $HOUR -le $BLACKOUT_END ]; then
	exit 0
    fi
fi

# lets gather the data
DATA=`( (for UID in ${SENSOR[*]}
  do
    echo "ask uid:$UID"
  done
  echo "disconnect" ) | $NC | \
    sed -e 's/.*uid:\(.*\) name:.*pres:\([0-9\.]*\).*/\2/' )`

HIGHVAL=0
n=0
for val in `echo $DATA`
do
   SENVAL[$n]=${val}
   if [ 1 -eq "$(echo "${SENVAL[$n]} > ${SENVAL[$HIGHVAL]}" | bc)" ]; then
      HIGHVAL=${n}
   fi
   let n=$n+1
done

# Lets see if it's dark out, if it is, prefer watering slightly more.
if [ -n "$LUX_SENSOR" ]; then
    LUX=`(echo "ask uid:$LUX_SENSOR" ; echo "disconnect") | $NC | \
	sed -e 's/.*lux://' -e 's/\.[0-9]*//'`
    if [ $LUX -lt $LIGHTLEVEL ]; then
	WATERTRIGGER="$NIGHTTRIGGER"
    fi
fi

#echo Sensor0:${SENVAL[0]}
#echo Sensor1:${SENVAL[1]}
#echo Sensor2:${SENVAL[2]}
#echo Sensor3:${SENVAL[3]}
#echo HighVal = $HIGHVAL

# if there is nothing to water, exit
if [ 1 -eq "$(echo "${SENVAL[$HIGHVAL]} < $WATERTRIGGER" | bc)" ]; then
    echo "${TIME} [INFO]: High Sensor=${HIGHVAL} reading=${SENVAL[$HIGHVAL]} Nothing to do" >> $LOGFILE
    exit 0
fi

echo "${TIME} [INFO]: Sensor=${HIGHVAL} reading=${SENVAL[$HIGHVAL]} needs water, running" >> $LOGFILE

ROW=0
(for runtime in ${SEN[$HIGHVAL]}
do
    if [ $runtime -gt 0 ]; then
	echo "${TIME} [DEBUG]: Run valve $ROW for $runtime seconds" >> $LOGFILE
	echo "chg uid:${SPRINKLER_ROW[$ROW]} timer:$runtime"
    fi
    let ROW=$ROW+1
done
echo "chg uid:$SPRINKLER_PROGRAM $SPRINKLER_PROGRAM_ENABLE"
echo "disconnect" ) | $NC
exit 0