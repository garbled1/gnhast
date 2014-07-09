Summary: Garbled's NetBSD Home Automation Scripting Tools
Name: gnhast
Version: 0.3
Release: 1
License: BSD
#Group: 
URL: http://sourceforge.net/p/gnhast/wiki/
#Source0: %{name}-%{version}.tar.gz
Source: http://sourceforge.net/projects/gnhast/files/%{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
A collection of daemons that work together to build an event-based home
automation system. The core set of daemons is written in C, however, any
event (such as a light being turned on) can be handled by an external script
or program. These programs can be written in any language, and the central
daemon handles all the intercommunication. It is designed to be easily
extensible for new device types and protocols.

%prep
%setup -q

%build
./configure
make

%install
prefix=%{buildroot}/usr/local make install -e

%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc README
%doc COPYING
%doc ChangeLog
/usr/local/libexec/gnhast/switchoff
/usr/local/libexec/gnhast/switchon
/usr/local/libexec/gnhast/nightlight
/usr/local/libexec/gnhast/functions
/usr/local/libexec/gnhast/timer
/usr/local/libexec/gnhast/timer2
/usr/local/share/gnhast/examples/icaddy.conf
/usr/local/share/gnhast/examples/venstar.conf
/usr/local/share/gnhast/examples/pwsweather.conf
/usr/local/share/gnhast/examples/wunderground.conf
/usr/local/share/gnhast/pixmaps/alarmstatus.png
/usr/local/share/gnhast/pixmaps/amps.png
/usr/local/share/gnhast/pixmaps/battery.png
/usr/local/share/gnhast/pixmaps/co-2.png
/usr/local/share/gnhast/pixmaps/counter.png
/usr/local/share/gnhast/pixmaps/dir.png
/usr/local/share/gnhast/pixmaps/distance.png
/usr/local/share/gnhast/pixmaps/flowrate.png
/usr/local/share/gnhast/pixmaps/gnhast-icon-16.png
/usr/local/share/gnhast/pixmaps/gnhast-icon-32.png
/usr/local/share/gnhast/pixmaps/gnhast-icon-48.png
/usr/local/share/gnhast/pixmaps/gnhast-logo.png
/usr/local/share/gnhast/pixmaps/gnhast.png
/usr/local/share/gnhast/pixmaps/graph.png
/usr/local/share/gnhast/pixmaps/group.png
/usr/local/share/gnhast/pixmaps/hub.png
/usr/local/share/gnhast/pixmaps/humid.png
/usr/local/share/gnhast/pixmaps/lux.png
/usr/local/share/gnhast/pixmaps/moisture.png
/usr/local/share/gnhast/pixmaps/number.png
/usr/local/share/gnhast/pixmaps/outlet.png
/usr/local/share/gnhast/pixmaps/percent.png
/usr/local/share/gnhast/pixmaps/pressure.png
/usr/local/share/gnhast/pixmaps/rainrate.png
/usr/local/share/gnhast/pixmaps/smnumber.png
/usr/local/share/gnhast/pixmaps/speed.png
/usr/local/share/gnhast/pixmaps/switch.png
/usr/local/share/gnhast/pixmaps/temp.png
/usr/local/share/gnhast/pixmaps/thmode.png
/usr/local/share/gnhast/pixmaps/thstate.png
/usr/local/share/gnhast/pixmaps/timer.png
/usr/local/share/gnhast/pixmaps/voltage.png
/usr/local/share/gnhast/pixmaps/volume.png
/usr/local/share/gnhast/pixmaps/watt.png
/usr/local/share/gnhast/pixmaps/wattsec.png
/usr/local/share/gnhast/pixmaps/weather.png
/usr/local/share/gnhast/pixmaps/wetness.png
/usr/local/etc/insteon.db
/usr/local/bin/ad2usbcoll
/usr/local/bin/icaddycoll
/usr/local/bin/venstarcoll
/usr/local/bin/owsrvcoll
/usr/local/bin/wupwscoll
/usr/local/bin/insteoncoll
/usr/local/bin/insteon_aldb
/usr/local/bin/insteon_discover
/usr/local/bin/rrdcoll
/usr/local/bin/brulcoll
/usr/local/bin/gnhastd
/usr/local/bin/fakecoll
/usr/local/bin/wmr918coll
/usr/local/bin/venstar_stats
/usr/local/bin/addhandler
/usr/local/bin/mk_rfx_ad2usb
/usr/local/bin/modhargs
/usr/local/bin/notify_listen
/usr/local/bin/ssdp_scan
/usr/local/bin/gtk-gnhast


%changelog
* Wed Jul  9 2014 Tim Rightnour <root@garbled.net> - 
- Initial build for 0.2.3

* Wed Jul  9 2014 Tim Rightnour <root@garbled.net> - 
- Update for 0.3 release
