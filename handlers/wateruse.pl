#!@PERL@

#use Data::Dump qw[ pp ];
use Fcntl qw(:flock);
use IO::Socket::INET;
use Text::ParseWords;
# auto-flush on socket
$| = 1;

# Config stuff here:
$debug = 0;
$logfile = "@LOCALSTATEDIR@/log/wateruse.log";
@valves = ("MAIA-zone01",
	   "MAIA-zone02",
	   "MAIA-zone03",
	   "MAIA-zone04",
	   "MAIA-zone05",
	   "MAIA-zone06",
	   "MAIA-zone07",
	   "MAIA-zone08",
	   "MAIA-zone09",
	   "MAIA-zone10");
$gphcounter = "1D.03A70D000000";
$gallonpertick = 0.1;
$zonequery = "MAIA-zonerunning";
$zoneadj = -1; # it will say zone1 when we want $valves[0]
$zones = 10;
$gnhastdhost = "electra";
$gnhastdport = "2920";
# end configs

# prevent multiple simultaneous runs
unless (flock(DATA, LOCK_EX|LOCK_NB)) {
  exit 0;
}

$time=`date +"%b %e %H:%M:%S"`;
chomp($time);
open(LOG, ">>", "$logfile");
LOG->autoflush(1);
$uid = $ARGV[0]; # who fired?
# pull in the upd from gnhast for the device
$update = <STDIN>;
chomp($update);
$zonerunning = 0;

&parse_devdata($update);
$zonerunning = $devices{$uid}{'number'};

if ($zonerunning == 0) {
  exit 0;
  # we can get called when the collector first reports, as well as at the end
  # of the water cycle.  Punt when that happens.
}
print LOG "$time [INFO]:wateruse starting\n";

# Now, we have something to actually do.  lets do a thing!

# Fire up the gnhastd connection
$sock = conn_gnhastd($gnhastdhost, $gnhastdport);

while ($zonerunning != 0) {
  $curcount = 0;
  $zn = 0;
  $timetorun = 0;
  $newcount = 0;

  $zn = $zonerunning + $zoneadj;
  @ask = &send_gn_cmd($sock, "ask uid:$valves[$zn]", "\n");
  &parse_devdata(@ask);
  @ask = &send_gn_cmd($sock, "ask uid:$gphcounter", "\n");
  &parse_devdata(@ask);

  $curcount = $devices{$gphcounter}{'count'};
  $timetorun = $devices{$valves[$zn]}{'timer'};
  $timetorun = 5 if ($timetorun < 5); #sanity
  print LOG "$time [DEBUG]:valve=$valves[$zn] count=$curcount timetorun=$timetorun\n" if ($debug);
  sleep($timetorun - 5); # stop 5 seconds before valve does
  @ask = &send_gn_cmd($sock, "ask uid:$gphcounter", "\n");
  &parse_devdata(@ask);
  $newcount = $devices{$gphcounter}{'count'};
  $gpv[$zn] = ($newcount - $curcount) * $gallonpertick;
  $gph[$zn] = (3600.0/$timetorun) * $gpv[$zn];
  print LOG "$time [DEBUG]:newcount=$newcount gpv=$gpv[$zn] gph=$gph[$zn]\n" if ($debug);

  sleep(10); # sleep off the 5 seconds and ask again
  @ask = &send_gn_cmd($sock, "ask uid:$zonequery", "\n");
  &parse_devdata(@ask);
  $zonerunning = $devices{$uid}{'number'};
}

$time=`date +"%b %e %H:%M:%S"`;
chomp($time);
print LOG "$time [INFO]:GPV:";
for ($i=0; $i < $zones; $i++) {
  if ($gpv[$i] != 0) {
    printf(LOG " zone%d=%0.2f", $i+1, $gpv[$i]);
  }
}
print LOG "\n";
print LOG "$time [INFO]:GPH:";
for ($i=0; $i < $zones; $i++) {
  if ($gph[$i] != 0) {
    printf(LOG " zone%d=%0.2f", $i+1, $gph[$i]);
  }
}
print LOG "\n";
exit;

sub parse_devdata {
  my @dd = @_;

  foreach $line (@dd) {
    chomp($line);
    @data = quotewords(" ", 0, $line);
    shift @data if ($data[0] eq "reg"); # discard reg
    shift @data if ($data[0] eq "upd"); # discard upd
    ($junk, $uid) = split(/:/, $data[0]);
    foreach $kv (@data) {
      ($key, $value) = split(/:/, $kv);
      $devices{$uid}{$key} = $value;
    }
  }
}

sub send_gn_cmd {
  local($sock, $cmd, $end) = ($_[0], $_[1], $_[2]);
  local @lines;

  $size = $sock->send("$cmd\n");
  my $line = '';
  while ($line !~ /$end$/) {
    $line = $sock->getline;
    push @lines, $line;
  }
  return @lines;
}

sub conn_gnhastd {
  local($host, $port) = ($_[0], $_[1]);

  # create a connecting socket
  my $socket = new IO::Socket::INET (
				     PeerHost => $host,
				     PeerPort => $port,
				     Proto => 'tcp',
				    );
  die "cannot connect to the server $!\n" unless $socket;
  #print "connected to the server\n";

  return $socket;
}

##### Needed for flock to work, do not edit

__DATA__
This exists to allow the locking code at the beginning of the file to work.
DO NOT REMOVE THESE LINES!
