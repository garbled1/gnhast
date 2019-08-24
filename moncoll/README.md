# Moncoll #

This "collector" talks to gnhastd, and monitors collectors on the machine.
This is primarily useful with certain devices that are a bit... sketchy.  For
example, certian little wifi IOT devices sometimes just stop responding, or
bounce up and down off wifi, killing your connection to them. The collector
usually tries to deal with this, but sometimes just needs to be restarted to
do the right thing.  Or maybe I've made a coding error and the collector
segfaults. Who knows?

Moncall talks to gnhast, and if gnhast thinks the collector is dead, it springs
into action and tries to fix it.  Usually by killing and restarting it.

An example config file is provided in "moncoll.conf".  In order to get your
collector uid, connect to gnhast with telnet <ghnast-host> 2920, and issue the
command:  "ldevs subt:30",  this will print out a list of collector uid's.

For linux, moncoll can also use the service command to stop/start collectors,
however, it needs root privledges to do so.  An example config for a service
is shown below:

`moncoll {
  instance = 2
  monitored "astrocoll" {
    name = "astrocoll"
    uid = "astrocoll-001-192.168.10.71"
    instance = 1
    use_systemd = 1
    service = "astrocoll.service"
  }
}`

Note: Without moncoll, the instance number on a collector is mostly
decorative.  However, once you use moncoll, it becomes vital.  For example,
if you have 2 venstarcoll's running, and they both have an instance of 1, they
will both have a uid of venstarcoll-001-IPADDR.  This means gnhast can't tell
the difference, and if one dies, moncoll has no choice other than to kill them
both.  It will also make things super unreliable if one is flaky.

Finally:  It is highly reccomended to start moncoll about 2-5 minutes after
everything else.  Some collectors take a long time to fire up and become
useful. Let things settle in before you start killing them down.
