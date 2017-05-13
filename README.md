gnhast
======

Garbled's Nifty Home Automation Scripting Tools
-----------------------------------------------

Gnhast is an event based home automation suite of tools.  It relies on a
central daemon, which handles all the coordination of work, and collectors
which handle all the actual work.  While the primary development
environment is NetBSD, it should also work on any other version of UNIX or
Linux. 

The gnhast daemon itself runs on a server, and feeds data to/is fed data
from collectors.  It can respond to a change in the state of a device, by
executing an external program.  This program, is itself, a form of
collector. 

The collectors perform all the actual work in gnhast. A collector can
perform a number of different functions: 

1) Monitor a device, and tell gnhastd about the changes in that device. 
For example, monitoring a one-wire temperature probe, and continually
updating gnhastd with the current temperature. 

2) Make changes to a device.  For example, the central daemon might tell
the collector to turn off a light switch.  The collector will issue the
command to the light switch it controls, and update gnhastd with the
status. 

3) Simply collect data.  A collector (such as rrdcoll) can simply receive
data from gnhastd, and use it to update rrd files for making pretty
graphs. 

4) Make decisions.  An external script (which is a form of collector)
could be informed that the temperature of a one-wire device has changed. 
It can look at this new value, and decide that it's time to turn on a fan. 
It would tell gnhastd that the fan device should be turned on, and gnhastd
will contact the appropriate collector. 

The core system of gnhast is written in C.  However, because the
collectors are simply separate processes that communicate with gnhastd,
they can be written in any language.  Additionally, collectors that are
not directly launched by gnhastd (scripts) com municate to gnhastd via a
simple TCP API. Because of this, they could even reside on a different
machine, or machines, than the master gnhastd. 

Design Philosophy
-----------------

Gnhast is based on UNIX, and attempts to make home automation more
UNIX-like.  If you like the way UNIX works, you will like gnhast.  If you
despise all the fiddly bits of UNIX, this is not for you. 

There is no built-in pseudo-coding language for making decisions based on
sensor data.  Writing a good language parser is hard, and many people are
better at it than I am.  They wrote languages, alot of them even.  Some
people like perl, some people despi se it.  Gnhast works similarly to a
UNIX kernel, it handles all the hardware, and provides a set API for
working with it.  If you want to write code that says "when it's light
out, turn the outside lights off", then just do that.  Write it in perl,
python , whatever, and have gnhast just run your executable. 

Do you want your garage door to open every night at 18:00? (yikes) Write a
script to open the door, and put it in *cron*. 

Finally, it's distributed.  Each collector runs on a machine, and gnhastd
runs on a machine.  The machines do not have to be the same machine. 
Perhaps you wish to run the core gnhastd on a big server with a massive
UPS, but you want to run the insteon co llector on a Raspberry PI next to
a power outlet in the laundry room.  Maybe you have a dedicated machine
running Cacti, and you want to run the rrd collector locally there. 
Gnhast lets you do all this. 

Gnhast does not need to run as root.  The only caveat, is that some of the
collectors will need access to the serial devices connected to hardware,
log files, and conf files.  Use chown/chmod. 

More Documentation
------------------

http://sourceforge.net/projects/gnhast/
