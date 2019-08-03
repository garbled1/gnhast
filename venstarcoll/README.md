# Venstar collector #

To setup, use the provided example config. Set the name of your venstar
unit in the config file (the default is Thermostat, change it to
what you set on the display panel)

./venstarcoll -m /tmp/new.conf -c ./venstart.conf

This generates a new config file in /tmp/conf.  Install this to 
/usr/local/etc/venstarcoll.conf, and then fire the collector up for real.

You might want to edit the file to taste, if you don't like the auto-generated
device names.
