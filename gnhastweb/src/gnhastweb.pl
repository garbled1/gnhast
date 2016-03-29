#!@PERL@

#use Data::Dump qw[ pp ];
use IO::Socket::INET;
use Text::ParseWords;
# auto-flush on socket
$| = 1;

$conffile = "gnhastweb.conf";
$header = "header.html";

@devtype = ("none",
	    "switch",
	    "dimmer",
	    "sensor",
	    "timer",
	    "blind",
	   );

@subtype = ("none",
	    "switch",
	    "outlet",
	    "temp",
	    "humid",
	    "counter",
	    "pressure",
	    "speed",
	    "dir",
	    "ph",
	    "wetness",
	    "hub",
	    "lux",
	    "voltage",
	    "wattsec",
	    "watt",
	    "amps",
	    "rainrate",
	    "weather",
	    "alarmstatus",
	    "number",
	    "percentage",
	    "flowrate",
	    "distance",
	    "volume",
	    "timer",
	    "thmode",
	    "thstate",
	    "smnumber",
	    "blind",
	    "collector",
	    "bool",
	   );

@ststorage = ("none", "switch", "switch", "temp", "humid", "count", "pres",
	      "speed", "dir", "ph", "wet", "hub", "lux", "volts", "wsec",
	      "watt", "amps",
	      "rain", "weather", "alarm", "number", "pct", "flow",
	      "distance", "volume", "timer", "thmode", "thstate", "smnum",
	      "blind", "collector");

@sthasscale = ("", "", "", "temp", "", "", "baro", "speed", "", "", "",
	       "", "light", "", "", "", "", "length", "", "", "",
	       "", "length", "length", "", "", "", "", "", "", "", "");

@tscale = ('F', 'C', 'K', 'R');
@tscalenm = ("Farenheit", "Celcius", "Kelvin", "Rankine");
@baroscale = ('in', 'mm', 'mb', 'cb');
@lengthscale = ('in', 'mm');
@speedscale = ('mph', 'knots', 'ms', 'kph');
@speedscalenm = ('MPH', 'Knots', 'm/s', 'KPH');
@lightscale = ('lux', 'wm2');
@lightscalenm = ('LUX', 'Wm^2');
@weathertypes = ('Sunny', 'Partly Cloudy', 'Cloudy', 'Rainy');
@alarmtypes = ('Ready', 'Stay', 'Stay Night', 'InstantMAX', 'Away', 'Fault');
@thermomodes = ('Off', 'Heat', 'Cool', 'Auto');
@thermostate = ('Idle', 'Heating', 'Cooling', 'Lockout', 'Error');

%scales = ( 'temp' => 0,
	    'baro' => 2,
	    'length' => 0,
	    'speed' => 0,
	    'light' => 0,
	  );

# contents of array:
# cardname, DisplayName, Number(0=off,1=normal,2=big),
#  measurestring, icon1, icon2

%subtypedata = (
		"none" => [ 'cardname', 'displayname', 0,
			    'measure', '','' ],
		"switch" => [ 'switch', 'SWITCH', 1,
			      "", 'fa fa-power-off', '' ],
		"outlet" => [ 'switch', 'OUTLET', 1,
			      "", 'fa fa-plug', '' ],
		"temp" => [ 'temp', 'TEMPERATURE', 1,
			    $tscalenm[$scales{'temp'}],
			    "wi wi-thermometer", '' ],
		"humid" => [ 'humid', 'HUMIDITY', 1,
			     '', 'wi wi-humidity', '' ],
		"counter" => [ 'counter', 'COUNTER', 2,
			       '', 'fa fa-hashtag', '' ],
		"pressure" => [ 'pressure', 'PRESSURE', 1,
				$baroscale[$scales{'baro'}],
				"wi wi-barometer", '' ],
		"speed" => [ 'windspeed', 'SPEED', 1,
			     $speedscalenm[$scales{'speed'}],
			     "fa fa-mixcloud", '' ],
		"dir" => [ 'winddir', 'DIRECTION', 1,
			   'Degrees', 'fa fa-safari', '' ],
		"ph" => [ 'ph', "ph", 1,
				"", "fa fa-balance-scale", "" ],
		"wetness" => [ 'wetness', "WETNESS", 1,
			       "", "fa fa-tint fa-flip-horizontal", "" ],
		"hub" => [ 'hub', "HUB", 1, '', 'notyet', ""],
		"lux" => [ 'lux', "LIGHT LEVEL", 2,
			   $slightscalenm[$scales{'light'}],
			   "fa fa-sun-o", "" ],
		"voltage" => [ 'volts', "VOLTAGE", 1,
			       "Volts", "wi wi-earthquake", '' ],
		"wattsec" => [ 'wattsec', "WATTSECONDS", 2,
			       "Watt Seconds", "fa fa-bolt", "fa fa-hourglass-o" ],
		"watt" => [ 'watt', "WATTS", 1,
			    "Watts", "wi wi-lightning", "" ],
		"amps" => [ 'amps', "AMPS", 1,
			    "Amps", "fa fa-bolt fa-flip-horizontal", "" ],
		"rainrate" => [ 'rainrate', "RAIN RATE", 1,
				$lengthscale[$scales{'length'}],
				"fa fa-umbrella", '' ],
		"weather" => [ 'weather', "WEATHER", 2,
			       '', 'fa fa-cloud', '' ],
		"alarmstatus" => [ 'alarmstatus', "ALARM STATUS", 2,
				   '', 'fa fa-bell', '' ],
		"number" => [ 'num', "NUMBER", 2,
			      '', "fa fa-slack", '' ],
		"percentage" => [ 'percentage', "PERCENTAGE", 1,
				  '', 'fa fa-percent', '' ],
		"flowrate" => [ 'flowrate', "FLOW RATE", 1,
				$lengthscale[$scales{'length'}],
				"fa fa-random", "" ],
		"distance" => [ 'distance', "DISTANCE", 1,
				$lengthscale[$scales{'length'}],
				"fa fa-arrows-h", "" ],
		"volume" => [ 'volume', "VOLUME", 1,
			      "FIXME", "fa fa-hourglass-end", "" ],
		"timer" => [ 'timer', "TIMER", 1,
			     "Seconds", "fa fa-clock-o", "" ],
		"thmode" => [ 'thmode', "THERMOSTAT MODE", 2,
			      "", "fa fa-fire", "" ],
		"thstate" => [ 'thstate', "THERMOSTAT STATE", 2,
			       "", "fa fa-fire fa-flip-horizontal", "" ],
		"smnumber" => [ 'smnumber', "SMALL NUMBER", 1,
				"", "fa fa-slack fa-flip-horizontal", "" ],
		"blind" => [ 'blind', "BLIND", 0,
			     "", "fa fa-arrow-down", "fa fa-arrow-up" ],
		"collector" => [ 'generic', "COLLECTOR", 0,
				 "", "fa fa-shopping-bag", "" ],
	       );

open(CONF, $conffile);
$conf = do { local $/; <CONF> };
$conf =~ s/\"//g;
close(CONF);

@confarray = &confparse($conf);

$actionurl = $confarray[0]{'gnhastweb'}{'actionurl'};
$gnhastdhost = $confarray[0]{'gnhastd'}{'hostname'};
$gnhastdport = $confarray[0]{'gnhastd'}{'port'};
$refreshrate = $confarray[0]{'gnhastweb'}{'refresh'};

$baseaction = $actionurl;
$baseaction =~ s/(.*\/).*/\1/;

if ($confarray[0]{'gnhastweb'}{'graphurl'} ne undef) {
  $graphaction = $confarray[0]{'gnhastweb'}{'graphurl'};
}

$instance = $confarray[0]{'gnhastweb'}{'actionurl'};
if ($instance < 1) {
  $instance = 1;
}

# check form contents
&ReadParse;

# check for settings changes

if (&Cookie("listmode") ne "") {
  $listmode = &Cookie("listmode");
}

if (&Cookie("abbreviate") ne "" && &Cookie("abbreviate") ne "0") {
  $numbermode = "YES";
}

$cookies = "";
if ($in{'mode'} eq "settings") {
  $listmode = $in{'listmode'};
  $cookies .= "Set-Cookie: listmode=$in{'listmode'}\n";
  $numbermode = $in{'number'};
  $cookies .= "Set-Cookie: abbreviate=$in{'number'}\n";
}

# Fire up the gnhastd connection
$sock = conn_gnhastd($gnhastdhost, $gnhastdport);

open(HEADER, $header);
$header = do { local $/; <HEADER> };
if ($confarray[0]{'widgets'}{'forecast'}{'zip'} ne undef) {
  $zip = $confarray[0]{'widgets'}{'forecast'}{'zip'};
  $header =~ s/localzip=\'85383\'/localzip=\'$zip\'/;
}
close(HEADER);

print "Content-type: text/html\n";
print "Access-Control-Allow-Origin: *\n";
print $cookies; # if any
print "\n";

if ($in{'mode'} eq "switch") {     # switch
  # we communicate with gnhast first, so the deed is done before the reload
  # happens.

  if ($in{$in{'uid'}} eq undef) {
    $junk = $sock->send("chg uid:$in{'uid'} switch:0\n");
  } else {
    $junk = $sock->send("chg uid:$in{'uid'} switch:1\n");
  }
  exit; # meh, why bother replying?

} elsif ($in{'mode'} eq "dimmer") {     # dimmer
  $url = $actionurl . "#" . $in{'anchor'};

  # we communicate with gnhast first, so the deed is done before the reload
  # happens.

  $newval = sprintf("%0.2f", $in{$in{'uid'}} / 100.0);
  # sanity
  $newval = "0.0" if ($newval < 0.03);
  $newval = "1.0" if ($newval > 0.97);
  $junk = $sock->send("chg uid:$in{'uid'} dimmer:$newval\n");

  exit;
}

#@junk = &send_gn_cmd($sock, "client client:gnhastweb-$instance", "\n");
$junk = $sock->send("client client:gnhastweb-$instance\n");
@devdata = &send_gn_cmd($sock, "ldevs", "endldevs");
@groupdata = &send_gn_cmd($sock, "lgrps", "endlgrps");
pop(@devdata); #nuke trailing endl
pop(@groupdata);

my %devices;
my %groups;
parse_devdata(@devdata);
parse_groupdata(@groupdata);

if ($in{'mode'} eq "editdev") {  # handle a device edit

  if ($in{'edit-submit'} eq "Modify Device") {
    $modstr = "mod uid:$in{'uid'}";
    if ($in{'name'} ne "" && $in{'name'} ne $devices{$in{'uid'}}{'name'}) {
      $modstr .= " name:\"$in{'name'}\"";
    }
    if ($in{'rrdname'} ne "" &&
	$in{'rrdname'} ne $devices{$in{'uid'}}{'rrdname'}) {
      $modstr .= " rrdname:\"$in{'rrdname'}\"";
    }
    $modstr .= " lowat:\"$in{'lowat'}\"" if ($in{'lowat'} ne "");
    $modstr .= " hiwat:\"$in{'hiwat'}\"" if ($in{'hiwat'} ne "");
    $modstr .= " handler:\"$in{'handler'}\"" if ($in{'handler'} ne "");
    if ($in{'hargs'} ne "") {
      @args = split(/,/, $in{'hargs'});
      $modstr .= " hargs:";
      $first = 0;
      foreach $arg (@args) {
	$modstr .= "," if ($first > 0);
	$modstr .= "\"$arg\"";
	$first++;
      }
    }
    $modstr .= "\n";
    $junk = $sock->send($modstr);
    #print "<body><html><pre>$modstr</pre></html></body>";
  } elsif ($in{'edit-submit'} eq "Modify Group Membership") {
    @groups_to_edit = split("\0", $in{'newgroup'});
    foreach $grp (@groups_to_edit) {
      $exist = 0;
      foreach $dv (@{$groups{$grp}{'dlist'}}) {
	$exist++ if ($dv eq $in{'uid'});
      }
      #print "working on $grp, exist == $exist <BR>\n";
      if ($exist == 0) {
	push(@{$groups{$grp}{'dlist'}}, $in{'uid'});
	$modstr .= "regg uid:$grp name:\"$groups{$grp}{'name'}\" dlist:";
	$first = 0;
	foreach $dev (@{$groups{$grp}{'dlist'}}) {
	  $modstr .= "," if ($first > 0);
	  $modstr .= "$dev";
	  $first++;
	}
	$modstr .= "\n";
      }
    }
    # madness
    foreach $key (keys(%groups)) {
      foreach $dev (@{$groups{$key}{'dlist'}}) {
	if ($dev eq $in{'uid'}) {
	  $ingrp = 0;
	  foreach $grp (@groups_to_edit) {
	    $ingrp++ if ($grp eq $key);
	  }
	  if ($ingrp == 0) {
	    #print "DELETE $in{'uid'} FROM $key<br>\n";
	    $modstr .= "regg uid:$key name:\"$groups{$key}{'name'}\" dlist:";
	    $first = 0;
	    foreach $dev (@{$groups{$key}{'dlist'}}) {
	      if ($dev ne $in{'uid'}) {
		$modstr .= "," if ($first > 0);
		$modstr .= "$dev";
		$first++;
	      }
	    }
	    $modstr .= "\n";
	  }
	}
      }
    }
    $junk = $sock->send($modstr);

    #print "<body><html><pre>$modstr</pre></html></body>";
  }
  $junk = $sock->send("disconnect\n");
  $sock->close();
  exit;
}

foreach $key (keys(%groups)) {
  foreach $subgroup (@{$groups{$key}{'glist'}}) {
    $groups{$subgroup}{'ischild'} = 1;
  }
}
foreach $key (keys(%groups)) {
  if ($groups{$key}{'ischild'} eq undef) {
    push @toplevelgroups, $key;
  }
}

foreach $key (keys(%groups)) {
  foreach $dev (@{$groups{$key}{'dlist'}}) {
    $devices{$dev}{'ischild'} = 1;
  }
}
foreach $key (keys(%devices)) {
  if ($devices{$key}{'ischild'} eq undef) {
    push @topleveldevices, $key;
  }
  if ($sthasscale[$devices{$key}{'subt'}] ne "") {
    local $sc = $scales{$sthasscale[$devices{$key}{'subt'}]};
    @ask = &send_gn_cmd($sock, "ask uid:$key scale:$sc", "\n");
  } else {
    @ask = &send_gn_cmd($sock, "ask uid:$key", "\n");
  }
  push @newdata, @ask;
}
&parse_devdata(@newdata);

# do we have fake groups?

foreach $grp (keys(%{$confarray[0]{'fakegroups'}})) {
  if ($grp eq "toplevel") { # handle these slightly different
    for ($i=1; $i < 100; $i++) {
      if ($confarray[0]{'fakegroups'}{$grp}{"uid".$i} ne undef) {
	$dev = $confarray[0]{'fakegroups'}{$grp}{"uid".$i};
	push @topleveldevices, $dev;
      } else {
	$i = 999;
      }
    }
  } else {
    if ($confarray[0]{'fakegroups'}{$grp}{'parent'} ne undef) {
      $parent = $confarray[0]{'fakegroups'}{$grp}{'parent'};
      push(@{$groups{$parent}{'glist'}}, $grp);
    } else {
      push(@toplevelgroups, $grp);
    }
    for ($i=1; $i < 100; $i++) {
      if ($confarray[0]{'fakegroups'}{$grp}{"uid".$i} ne undef) {
	$dev = $confarray[0]{'fakegroups'}{$grp}{"uid".$i};
	push(@{$groups{$grp}{'dlist'}}, $dev);
      } else {
	$i = 999;
      }
    }
  }
}

# Boom.  Ok, now we know the toplevel devices and groups.  Now we just start
# printing crapola.  Yayz.

print $header;

# Make the popups

&settings_popup;
&edit_device_popup; # for testing
&graph_popup if ($graphaction ne undef);

# First we built the toplevel, by hand.

print " <!-- Toplevel -->\n";
print " <div id=\"Toplevel\" class=\"content\">\n";
&build_topbar;
print "  <div class=\"scroll-pane\">\n";
if ($listmode == 1) {
  $odd = 0;
  print "<table><col id=valuec><col><col id=valuec><col><col id=valuec>\n";
  print "<thead><tr>";
  print "<th></th><th>Type</th><th>Name</th><th>UID</th><th>Value</th></tr>\n";
  print "</thead>\n";
  print "<tbody>\n";
} else {
  print "  <ul id=\"works\">\n";
}

for $group (sort(@toplevelgroups)) {
  &print_group($group);
}

# widgets:

&print_widgets();

# fake it for the uncategorized group:
&print_group('uncategorized');

if ($listmode == 1) {
  print "</tbody></table>\n";
} else {
  print "  </ul>\n";
}
print " </div></div>\n";

# build an uncategorized fake group for uncategorized devices
print " <!-- uncategorized -->\n";
print " <div id=\"uncategorized\" class=\"panel\">\n";
&build_topbar;
print "  <div class=\"scroll-pane\">\n";
if ($listmode == 1) {
  $odd = 0;
  print "<table><col><col id=valuec><col><col id=valuec><col>\n";
  print "<thead><tr>";
  print "<th></th><th>Type</th><th>Name</th><th>UID</th><th>Value</th></tr>\n";
  print "</thead>\n";
  print "<tbody>\n";
} else {
  print "  <ul id=\"works\">\n";
}
# Print a back link
&back_link;
for $dev (sort(@topleveldevices)) {
  &print_device($dev, "");
}
if ($listmode == 1) {
  print "</tbody></table>\n";
} else {
  print "  </ul>\n";
}
print " </div></div>\n";

# now the rest of the groups

foreach $group (sort(keys(%groups))) {
  print " <!-- $group -->\n";
  print " <div id=\"$group\" class=\"panel\">\n";
  &build_topbar;
  print "  <div class=\"scroll-pane\">\n";
  if ($listmode == 1) {
    $odd = 0;
    print "<table><col><col id=valuec><col><col id=valuec><col>\n";
    print "<thead><tr>";
    print "<th></th><th>Type</th><th>Name</th><th>UID</th><th>Value</th></tr>\n";
    print "</thead>\n";
    print "<tbody>\n";
  } elsif ($listmode == 2 && $confarray[0]{'groupreplace'}{$group} ne undef) {
    open(DATA, $confarray[0]{'groupreplace'}{$group});
    $data = do { local $/; <DATA> };
    print $data;
    close(DATA);
  } else{
    print "  <ul id=\"works\">\n";
  }
  if ($listmode != 2 || $confarray[0]{'groupreplace'}{$group} eq undef) {
    # Print a back link
    &back_link;

    # now groups
    foreach $subgroup (sort(@{$groups{$group}{'glist'}})) {
      &print_group($subgroup);
    }
    &print_widgets($group);
    foreach $dev (sort(@{$groups{$group}{'dlist'}})) {
      &print_device($dev, $group);
    }
    if ($listmode == 1) {
      print "</tbody></table>\n";
    } else {
      print "  </ul>\n";
    }
  }
  print " </div></div>\n";
}

print "</body></html>\n";
$junk = $sock->send("disconnect\n");
$sock->close();





##################################################################
# subs





sub print_widgets {
  local ($group) = ($_[0]);

  return if ($listmode > 0); # Widgets are for button mode only

  foreach $widget (keys(%{$confarray[0]{'widgets'}})) {
    if (($confarray[0]{'widgets'}{$widget}{'parent'} eq undef && $group eq undef) || ($confarray[0]{'widgets'}{$widget}{'parent'} eq $group)) {
      #make the widget
      if ($widget eq "localweather") {
        &widget_localweather;
      } elsif ($widget eq "forecast") {
	print "<li><div class=\"weatherwidget\"></div></li>\n";
      }
    }
  }
}

sub widget_localweather {
  return if ($listmode == 1);
  return if ($confarray[0]{'widgets'} eq undef);
  return if ($confarray[0]{'widgets'}{'localweather'} eq undef);

  print "<li><div class=\"widgetcontainer\">\n";
  print "<div class=\"widgetcard\">\n";
  print "<div class=\"header\">Local Weather</div>\n";
  print "<div class=\"container\">";
  print "<div class=\"row\">";
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'forecast'},
		    "fcbox");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'temp'},
		    "bigtext");
  print "<div class=\"smalltext\">";
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'dew'},
		    "sidetext", "Dewpoint");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'windchill'},
		    "sidetext", "Wind Chill");
  print "</div></div>";
  print "<div class=\"row\">";
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'humid'},
		    "medtext", "Humidity");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'baro'},
		    "medtext", "Pressure");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'rain'},
		    "medtext", "Rainfall");
  print "</div>";
  print "<div class=\"row\">";
  print "<div class=\"smalltext\">";
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'windgust'},
		    "sidetext", "Gust Speed");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'windgustdir'},
		    "sidetext", "Direction");
  print "</div>";
  print "<div class=\"smalltext\">";
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'windavg'},
		    "sidetext", "Average Speed");
  &print_widget_bit($confarray[0]{'widgets'}{'localweather'}{'windavgdir'},
		    "sidetext", "Direction");
  print "</div></div>";
  print "</div></div></div></li>\n";
  #pp @confarray;
}

sub print_widget_bit {
  local ($uid, $class, $title) = ($_[0], $_[1], $_[2]);

  print "<div class=\"$class\">";
  if ($title ne undef) {
    print "<div class=\"title\">$title</div>";
  }
  if ($uid ne undef) {
    print "<div name=\"$uid" . "-val\">";
    &print_devdata($uid, $devices{$uid}{'subt'});
    print "</div></div>\n";
  } else {
    print "</div>\n";
  }
}

sub edit_device_popup {
  print "<div id=\"editdev_popup\" class=\"overlay\">\n";
  print " <div class=\"popup editdevpop\">\n";
  print "  <h2>Edit properties for name</h2>\n";
  print "  <h3>uid goes here</h3>\n";
  print "  <a class=\"close\" href=\"#\">&times;</a>\n";
  print "  <div class=\"pcontent\">\n";
  print "   <form autocomplete=off method=\"POST\" class=\"editdevform\" id=\"editform\" action=\"$actionurl\">\n";
  print "    <div id=\"ed-left\">\n";
  print "     <input type=\"hidden\" name=\"mode\" value=\"editdev\">\n";
  print "     <label><span>Device UID:</span><input id=\"edit-uid\" class=\"smin\" type=\"text\" name=\"uid\" value=\"none\" readonly></label>\n";
  print "     <label><span>Name:</span><input id=\"edit-name\" class=\"medin\" type=\"text\" name=\"name\" value=\"none\"></label>\n";
  print "     <label><span>RRD Name:</span><input id=\"edit-rrdname\" class=\"smin\" type=\"text\" name=\"rrdname\" value=\"none\"></label>\n";
  print "     <label><span>Low Water Mark:</span><input id=\"edit-lowat\" class=\"smin\" type=\"text\" name=\"lowat\" value=\"none\"></label>\n";
  print "     <label><span>High Water Mark:</span><input id=\"edit-hiwat\" class=\"smin\" type=\"text\" name=\"hiwat\" value=\"none\"></label>\n";
  print "    </div><div id=\"ed-right\">";
  print "     <label>Device Type: <span id=\"edit-devt\" class=\"ed-num\">3</span></label><br>\n";
  print "     <label>Device SubType: <span id=\"edit-subt\" class=\"ed-num\">3</span></label><br>\n";
  print "     <label>Device Prototype: <span id=\"edit-proto\" class=\"ed-num\">3</span></label><br>\n";
  print "    </div><div id=\"ed-bottom\">\n";
  print "     <label><span>Handler:</span><input id=\"edit-handler\" class=\"lgin\" type=\"text\" name=\"handler\" value=\"none\"></label>\n";
  print "     <label><span>Handler Arguments:</span><input id=\"edit-hargs\" class=\"lgin\" type=\"text\" name=\"hargs\" value=\"none\"></label>\n";
  print "    <br><input id=\"edit-chgdev\" type=\"submit\" class=\"button blue\" value=\"Modify Device\" name=\"edit-submit\">";
  print "  <span class=\"redspan\" id=\"edit-dev-post\"></span><br>\n";
  # the multi-thing
  print "     <div id=\"ed-ag\">\n";
  print "      <label><span>Available Groups</span></label><br>\n";
  print "      <select multiple class=\"edit-sel\" id=\"edit-availgroups\">\n";
  print "      </select><br>\n";
  print "      <a href=\"#\" id=\"edit-add\">add &gt;&gt;</a>\n";
  print "     </div>\n";
  print "     <div id=\"ed-sg\">\n";
  print "      <label><span>Selected Groups</span></label><br>\n";
  print "      <select multiple name=\"newgroup\" class=\"edit-sel\" id=\"edit-groups\">\n";
  print "      </select><br>\n";
  print "      <a href=\"#\" id=\"edit-remove\">&lt;&lt; remove</a>\n";
  print "     </div>\n";
  print "    <br><input id=\"edit-chggrp\" type=\"submit\" class=\"button blue\" value=\"Modify Group Membership\" name=\"edit-submit\">";
  print "  <span class=\"redspan\" id=\"edit-grp-post\"></span>\n";
  print "    </div>\n";
  print "    </div>\n";
  print "   </form>\n";
  print "  </div>\n";
  print " </div>\n";
  print "</div>\n";
}

sub graph_popup {
  print "<div id=\"graph_popup\" class=\"overlay\">\n";
  print " <div class=\"popup graphpop\">\n";
  print "  <h2>Graph</h2>\n";
  print "  <h3>Device UID</h3>\n";
  print "  <a class=\"close\" href=\"#\">&times;</a>\n";
  print "  <div class=\"pcontent\">\n";
  print "   <div class=\"graphdata\">\n";
  print "   </div>";
  print "   <div class=\"graphdatalinks\">\n";
  print "      <a data-uid=\"XUIDX\" class=\"bluebtn graphbutton ingraph\" href=\"" . $graphaction . "?res=3600&start=-24h&end=now-1h&uid=XUIDX\">DAY</a>";
  print "      <a data-uid=\"XUIDX\" class=\"bluebtn graphbutton ingraph\" href=\"" . $graphaction . "?res=3600&start=-48h&end=now-1h&uid=XUIDX\">48H</a>";
  print "      <a data-uid=\"XUIDX\" class=\"bluebtn graphbutton ingraph\" href=\"" . $graphaction . "?res=3600&start=-1w&end=now-1h&uid=XUIDX\">WEEK</a>";
  print "      <a data-uid=\"XUIDX\" class=\"bluebtn graphbutton ingraph\" href=\"" . $graphaction . "?res=3600&start=-1m&end=now-1h&uid=XUIDX\">MONTH</a>";
  print "      <a data-uid=\"XUIDX\" class=\"bluebtn graphbutton ingraph\" href=\"" . $graphaction . "?res=3600&start=-1y&end=now-1h&uid=XUIDX\">YEAR</a>";
  print "   </div>";
  print "  </div>";
  print " </div>";
  print "</div>\n";
}


sub settings_popup {
  print "<div id=\"settings_popup\" class=\"overlay\">\n";
  print " <div class=\"popup\">\n";
  print "  <h2>Global Settings</h2>\n";
  print "  <a class=\"close\" href=\"#\">&times;</a>\n";
  print "  <div class=\"pcontent\">\n";
  print "   <form method=\"GET\" id=\"form\" action=\"$actionurl\">\n";
  print "    <input type=\"hidden\" name=\"mode\" value=\"settings\">\n";
  print "    <p><label>Display Mode</label>";
  print "<div class=\"styled-select\"><select name=\"listmode\">\n";
  print " <option value=0>Buttons</option>\n";
  print " <option value=1";
  print " selected" if ($listmode == 1);
  print ">List</option>\n";
  print " <option value=2";
  print " selected" if ($listmode == 2);
  print ">Custom</option>\n";
  print "</select></div></p>\n";
  print "<label for=\"number\"><p>Humanize Numbers</p></label>";
  print "    <input id=\"number\" value=\"on\" type=\"checkbox\" name=\"number\" class=\"normalcheck\"";
  print " checked=\"on\"" if ($numbermode ne "");
  print ">";
  print "    <br><input type=\"submit\" class=\"button green\" value=\"Change\" >\n";
  print "   </form>\n";
  print "  </div></div></div>\n";
}

sub back_link {
  if ($listmode == 1) {
    $odd++;
    print "<tr";
    print " class=odd" if (($odd % 2) == 0);
    print ">";
    print "<td class=\"icon\"><a class=\"rowlink\" id=\"link-$group-back\" href=\"javascript:history.back()\">";
    print "<i class=\"fa fa-arrow-circle-left fa-2x\"></i></a></td>\n";
    print "<td>BACK</td><td></td></tr>\n";
  } else {
    print "<li>\n";
    print " <a id=\"link-$group-back\" href=\"javascript:history.back()\">\n";
    print " <div class=\'container\'>\n";
    print "  <div class=\'card group\'>\n";
    print "   <div class=\'inner\'>\n";
    print "    <div class=\'icon\'>\n";
    print "     <i class=\"fa fa-arrow-circle-left fa-3x\"></i>\n";
    print "    </div>\n";
    print "    <div class=\'title\'>\n";
    print "     <div class=\'text\'>BACK</div>\n";
    print "    </div>\n";
    print "    <div class=\'number\'>BACK</div>\n";
    print "   </div>\n";
    print "  </div>\n";
    print " </div>\n";
    print " </a>\n";
    print "</li>\n";
  }
}

sub print_group {
  local ($uid) = ($_[0]);

  if ($listmode == 1) {
    $odd++;
    print "<tr";
    print " class=odd" if (($odd % 2) == 0);
    print "><td class=icon><a id=\"link-$uid\" class=\"rowlink\" href=\"#$uid\"><i class=\"fa fa-cubes fa-2x\"></i></a></td>";
    print "<td class=type>GROUP</td>";
    print "<td class=name>$groups{$uid}{'name'}</td>";
    print "<td class=uid>$uid</td>";
    print "<td class=value></td>";
    print "</tr>\n";
  } else {
    print "<li>\n";
    print " <a id=\"link-$uid\" href=\"#$uid\">\n";
    print " <div class=\'container\'>\n";
    print "  <div class=\'card group\'>\n";
    print "   <div class=\'inner\'>\n";
    print "    <div class=\'icon\'>\n";
    print "     <i class=\"fa fa-cubes fa-3x\"></i>\n";
    print "    </div>\n";
    print "    <div class=\'title\'>\n";
    print "     <div class=\'text\'>GROUP</div>\n";
    print "    </div>\n";
    print "    <div class=\'name\'>$groups{$uid}{'name'}</div>\n";
    print "    <div class=\'devid\'>$uid</div>\n";
    print "   </div>\n";
    print "  </div>\n";
    print " </div>\n";
    print " </a>\n";
    print "</li>\n";
  }
}

sub nm_to_number {
  my ($name, @array) = @_;
  local $i;

  for ($i = 0;  $i < $#array;  $i++) {
    if ($array[$i] eq $name) {
      return $i;
    }
  }
  return -1;
}

sub build_topbar {
  print "<div id=\'cssmenu\'>\n";
  print "<img src=\"/gnhast-icon-48.png\" style=\"float:left;\">";
  print " <ul>\n";
  print "  <li class=\'active\'><a href=\'#toplevel\'><span>Top Level</span>";
  print "</a></li>\n";
  print "  <li><a href=\"#\" onclick=\"goFullscreen(); return false;\">";
  print "<span>FullScreen</span></a></li>\n";
  print "  <li><a href=\"$actionurl\"><span>Reload</span></a></li>\n";
  foreach $ln (keys(%{$confarray[0]{'links'}})) {
    print "  <li><a href=\"";
    print $confarray[0]{'links'}{$ln};
    print "\"><span>$ln</span></a></li>\n";
  }
  print "  <li class='last'><a href=\'#settings_popup\'><span>Settings</span></a></li>\n";
  print " </ul>\n";
  print "</div>\n";
}

# ugh, what a mess.
sub print_device {
  local ($uid, $anchor) = ($_[0], $_[1]);

  local $subtnm = $subtype[$devices{$uid}{'subt'}];
  local $subt = $devices{$uid}{'subt'};

  if ($listmode == 1) {
    $odd++;
    print "<tr";
    print " class=odd" if (($odd % 2) == 0);
    print "><td class=icon><i class=\"$subtypedata{$subtnm}[4] fa-2x\"></i></td>";
    print "<td class=type>$subtypedata{$subtnm}[1]</td>";
    print "<td class=name>$devices{$uid}{'name'}</td>";
    print "<td class=uid>$uid</td>";
    print "<td class=value name=\"$uid"."-val\">";
    &print_devdata($uid, $subt);
    print "</td>";
    print "</tr>\n";
  } else {
  print "<li>\n";
  print " <div class=\'container\'>\n";
  print "  <div class=\'card $subtypedata{$subtnm}[0]\'>\n";
  print "   <div class=\'inner\'>\n";
  if (($subtnm eq "switch" || $subtnm eq "outlet")
	   && $devices{$uid}{'devt'} == 1) {
    # we have a switch
    print "    <div class=\'icon\'>\n";
    print "     <form autocomplete=off action=\"$actionurl\" method=\"POST\">\n";
    print "	 <input type=\"hidden\" name=\"mode\" value=\"switch\">\n";
    print "	 <input type=\"hidden\" name=\"uid\" value=\"$uid\">\n";
    print "	 <input type=\"hidden\" name=\"anchor\" value=\"$anchor\">\n";
    print "      <input type=\"checkbox\" class=switchbutton name=\"$uid\" id=\"$uid-$anchor\"";
    print " checked=ON" if ($devices{$uid}{$ststorage[$subt]} != 0);
    print ">\n";
    print "     <label class=\"toggle\" for=\"$uid-$anchor\"></label>\n";
    print "     </form>\n";
    print "    </div>\n";
  } elsif ($subtnm eq "switch" && $devices{$uid}{'devt'} == 2) {
    # a dimmer
    print "    <div class=\'icon\'>\n";
    print "     <form autocomplete=off action=\"$actionurl\" method=\"POST\" class=\"dimmerform\">\n";
    print "	 <input type=\"hidden\" name=\"mode\" value=\"dimmer\">\n";
    print "	 <input type=\"hidden\" name=\"uid\" value=\"$uid\">\n";
    print "	 <input type=\"hidden\" name=\"anchor\" value=\"$anchor\">\n";
    print "      <input type=\"range\" name=\"$uid\" step=\"1\" value=\"";
    print &float_to_num($devices{$uid}{'dimmer'});
    print "\" max=\"100\">\n";
    print "      <input type=\"submit\" class=\"button red dimmerbutton\" value=\"Change\">\n";
    print "     </form>\n";
    print "    </div>\n";
  } elsif ($subtnm eq "blind") {
    print "    <div class=\'icon\'>\n";
    print "     <a href=\"doathingDOWN\"><i class=\"$subtypedata{$subtnm}[4] fa-3x\"></i>\n";
    print "     <a href=\"doathingUP\"><i class=\"$subtypedata{$subtnm}[5] fa-3x\"></i>\n";
    print "    </div>\n";
  } else {
    print "    <div class=\'icon\'>\n";
    if ($subtypedata{$subtnm}[4] ne "") {
      print "     <i class=\"$subtypedata{$subtnm}[4] fa-3x\"></i>\n";
    }
    if ($subtypedata{$subtnm}[5] ne "") {
      print "     <i class=\"$subtypedata{$subtnm}[5] fa-3x\"></i>\n";
    }
    print "    </div>\n";
    if ($graphaction ne undef) {
      print "    <div class=\'glink\'>\n";
      print "      <a data-name=\"$devices{$uid}{'name'}\" data-uid=\"$uid\" class=\"graphbutton\" href=\"" . $graphaction . "?res=3600&start=-24h&end=now-1h&uid=$uid\"><i class=\"fa fa-area-chart\"></i></a>";
      print "    </div>\n";
    }
  }
  print "    <div class=\'elink'\>\n";
  print "      <a class=\"devedit-link\" data-port=\"$gnhastdport\" data-host=\"$gnhastdhost\" data-uid=\"$uid\" data-url=\"$baseaction" . "askfjson.cgi\" href=\'#editdev_popup\'><i class=\"fa fa-cogs\"></i></a>\n";
  print "    </div>\n";
  print "    <div class=\'title\'>\n";
  print "     <div class=\'text\'>$subtypedata{$subtnm}[1]</div>\n";
  print "    </div>\n";

  # Print the data
  if ($subtypedata{$subtnm}[2] == 1) {
    print "    <div class=\'number\' name=\"$uid"."-val\">";
  } elsif ($subtypedata{$subtnm}[2] == 2) {
    print "    <div class=\'bignumber\' name=\"$uid"."-val\">";
  }

  &print_devdata($uid, $subt);

  if ($subtypedata{$subtnm}[2] != 0) {
    print "</div>\n";
  }

  print "    <div class=\'measure\'>$subtypedata{$subtnm}[3]</div>\n";
  print "    <div class=\'name\'><span>$devices{$uid}{'name'}</span></div>\n";
  print "    <div class=\'devid\'>$uid</div>\n";
  print "   </div>\n";
  print "  </div>\n";
  print " </div>\n";
  print "</li>\n";
  }
}

sub print_devdata {
  local ($uid, $subt) = ($_[0], $_[1]);

  if ($subtnm eq "switch" && $devices{$uid}{'devt'} == 2) { # dimmer
        print &float_to_num($devices{$uid}{'dimmer'});
  } elsif ($subtnm eq "switch" || $subtnm eq "outlet") {
    print "OFF" if ($devices{$uid}{$ststorage[$subt]} == 0);
    print "ON" if ($devices{$uid}{$ststorage[$subt]} >= 1);
  } elsif ($subtnm eq "weather") {
    print "$weathertypes[$devices{$uid}{$ststorage[$subt]}]";
  } elsif ($subtnm eq "alarmstatus") {
    print "$alarmtypes[$devices{$uid}{$ststorage[$subt]}]";
  } elsif ($subtnm eq "thmode") {
    print "$thermomodes[$devices{$uid}{$ststorage[$subt]}]";
  } elsif ($subtnm eq "thstate") {
    print "$thermostate[$devices{$uid}{$ststorage[$subt]}]";
  } else {
    if (&isfloat($devices{$uid}{$ststorage[$subt]})) {
      printf "%0.2f", $devices{$uid}{$ststorage[$subt]};
    } else {
      print "$devices{$uid}{$ststorage[$subt]}";
    }
  }
  if ($subtnm eq "humid" || $subtnm eq "wetness" ||
      $devices{$uid}{'devt'} == 2 || $subtnm eq "percentage") {
    print "%";
  }
}

sub isfloat {
  my $val = shift;
  return $val =~ m/^\d+\.\d+$/;
}

sub float_to_num {
  local ($fl) = ($_[0]);

  return int($fl * 100);
}

sub parse_groupdata {
  my @dd = @_;

  foreach $line (@dd) {
    chomp($line);
    @data = quotewords(" ", 0, $line);
    shift @data if ($data[0] eq "regg"); # discard regg
    ($junk, $uid) = split(/:/, $data[0]);
    foreach $kv (@data) {
      local @devs;
      local @grps;
      ($key, $value) = split(/:/, $kv);
      if ($key eq "name") {
	$groups{$uid}{$key} = $value;
      } elsif ($key eq "dlist") {
	@devs = split(/,/, $value);
	$groups{$uid}{$key} = \@devs;
      } elsif ($key eq "glist") {
	@grps = split(/,/, $value);
	$groups{$uid}{$key} = \@grps;
      }
    }
  }
}

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

sub confparse {

  local($raw) = ($_[0]);

  # http://www.perlmonks.org/?node_id=1112435

  my $FROM_CONFIG = qr{
		       (?<OBJECT_OPEN>
		       ^
		       \s*
		       (?<NAME>
		       \w+
		      )
		       \s*
		       \{
		       \s*
		       [\r\n]+
		      )
		       |
		       (?<OBJECT_CLOSE> ^ \s* \} \s* [\r\n]+ )
		       |
		       (?<KEYVAL>
		       ^
		       \s*
		       (?<KEY>
		       [\w\-]+
		      )
		       \s+=\s+
		       (?<VAL>
		       [^\r\n\{]+
		      )
		       \s*
		       [\r\n]+
		      )
		       |
		       (?<UHOH> . )
		     }xms;


my @stack = {};

while( $raw =~ m{$FROM_CONFIG}g ){
##    dd( \%+ );
##    push @stack, { %+ };

    my $freeze = { %+ };
    if( $freeze->{OBJECT_OPEN} ){
        my $new = {};
        $stack[-1]->{ $freeze->{NAME} } = $new;
        push @stack, $new;
    }elsif( $freeze->{OBJECT_CLOSE} ){
        pop @stack;
    }elsif( $freeze->{KEYVAL} ){
        $stack[-1]->{ $freeze->{KEY} } = $freeze->{VAL};
    }
}
return @stack;
}

sub Cookie{
   my ($cookiename) = @_;
   my @cookies = split(/\s*;\s*/, $ENV{'HTTP_COOKIE'});
   foreach (@cookies){
     my @tokens = split(/=/, $_);
     return $tokens[1] if($tokens[0] == $cookiename);
   }
   return '';
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
