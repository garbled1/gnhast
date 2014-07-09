Summary: Garbled's NetBSD Home Automation Scripting Tools
Name: gnhast
Version: 0.2.3
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
/usr/local/share/gnhast/examples
/usr/local/share/gnhast/examples/pwsweather.conf
/usr/local/share/gnhast/examples/wunderground.conf
/usr/local/etc/insteon.db
/usr/local/bin/insteon_discover
/usr/local/bin/owsrvcoll
/usr/local/bin/wupwscoll
/usr/local/bin/insteoncoll
/usr/local/bin/insteon_aldb
/usr/local/bin/rrdcoll
/usr/local/bin/brulcoll
/usr/local/bin/gnhastd
/usr/local/bin/fakecoll
/usr/local/bin/wmr918coll

%changelog
* Wed Jul  9 2014 Tim Rightnour <root@garbled.net> - 
- Initial build for 0.2.3
