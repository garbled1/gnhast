# How to install gnhastweb

1. 	Run the normal gnhast build.  This will substitute the @PERL@ with your real perl path.
2. 	Edit cgi-bin/rrdjson.cgi.  Change $path to the location of your generated rrd files.  These would be wherever you told rrdcoll to put them.
3. 	Copy all the `cgi-bin/*` scripts to the cgi-bin dir of your webserver.
4. 	Copy the "css", "scripts" and "fonts" directories to the docroot of your webserver. (ie, like /htdocs/css, etc)
5. 	Copy favicon.png (if you want) and gnhast-icon-48.png to the docroot.
6. 	Copy jsoncoll.cgi from your installed bin directory to your cgi-bin directory.
7.  Edit your jsoncgicoll.conf file in your etc directory.  An example:
```

jsoncgicoll {
        instance = 1
}
gnhastd {
  hostname = "electra"
  port = 2920
}
```
8. Copy the example gnhastweb.conf file to the cgi-bin directory of your webserver.  Edit it to your heart's content.  The example activates most of the features, many can be disabled.
9. Copy header.html to the cgi-bin directory of your webserver.
10. Connect to your webserver, it should work?
