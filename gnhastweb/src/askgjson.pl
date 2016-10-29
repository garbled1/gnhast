#!@PERL@

use IO::Socket::INET;
use Text::ParseWords;
&ReadParse;

$port = $in{'port'};
$gnhastd = $in{'gnhastd'};
$uid = $in{'uid'};

#$uid = "bonkers";
#$port = 2920;
#$gnhastd = "127.0.0.1";

print "Access-Control-Allow-Origin: *\n";
print "Fake-Header: $file\n";
print "Content-Type: application/json\n\n";

# Fire up the gnhastd connection
$sock = conn_gnhastd($gnhastd, $port);
#$junk = $sock->send("askf uid:$uid\n");
#$data = $sock->getline;

@groupdata = &send_gn_cmd($sock, "lgrps", "endlgrps");
pop(@groupdata); #nuke trailing endl

foreach $grp (@groupdata) {
  chomp($grp);
  @data = quotewords(" ", 0, $grp);
  shift @data if ($data[0] eq "regg"); # discard regg
  ($junk, $guid) = split(/:/, $data[0]);
  push(@groups, $guid);
  if ($guid eq $uid) {
    $data = $grp;
  }
  foreach $kv (@data) {
    if ($kv =~ /$uid/ && $guid ne $uid) {
      push(@members, $guid);
    }
  }
}

$junk = $sock->send("disconnect\n");
$sock->close();

#print $data;

chomp($data);
@vals = quotewords(" ", 0, $data);

#map { print $_ , "\n"} @vals;
print "{ ";

for ($i=1; $i <= $#vals; $i++) {
  ($key, $value) = split(/:/, $vals[$i]);

  if ($key eq "glist") {
    print ", " if ($i != 1);
    print "\"$key\" : [ ";
    $first = 0;
    @glist = split(/,/, $value);
    foreach $grp (@glist) {
      print ", " if ($first > 0);
      print "\"$grp\"";
      $first++;
    }
    print " ] ";
  } elsif ($key ne "") {
    print ", " if ($i != 1);
    print " \"$key\" : \"$value\" ";
  }
}

print ", \"groups\" : [ ";
$first = 0;
foreach $grp (@groups) {
  $found = 0;
  foreach $entry (@glist) {
    $found++ if ($grp eq $entry)
  }
  if ($found == 0) {
    print ", " if ($first > 0);
    print "\"$grp\"";
    $first++;
  }
}
print " ] ";
print " }";

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

sub ReadParse {
  local (*in) = @_ if @_;
  local ($i, $key, $val);
  # Read in text
  if ($ENV{'REQUEST_METHOD'} eq "GET") {
    $in = $ENV{'QUERY_STRING'};
  } elsif ($ENV{'REQUEST_METHOD'} eq "POST") {
    read(STDIN,$in,$ENV{'CONTENT_LENGTH'});
  }
  @in = split(/[&;]/,$in);
  foreach $i (0 .. $#in) {
    # Convert plus's to spaces
    $in[$i] =~ s/\+/ /g;
    # Split into key and value.
    ($key, $val) = split(/=/,$in[$i],2); # splits on the first =.
    # Convert %XX from hex numbers to alphanumeric
    $key =~ s/%(..)/pack("c",hex($1))/ge;
    $val =~ s/%(..)/pack("c",hex($1))/ge;
    # Associate key and value
    $in{$key} .= "\0" if (defined($in{$key})); # \0 is the multiple separator
    $in{$key} .= $val;
  }
  return scalar(@in);
}
