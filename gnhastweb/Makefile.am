CGIBIN = $(srcdir)/cgi-bin

noinst_X = \
	$(CGIBIN)/gnhastweb.cgi \
	$(CGIBIN)/rrdjson.cgi \
	$(CGIBIN)/askfjson.cgi \
	$(CGIBIN)/askgjson.cgi
CLEANFILES = $(noinst_X)
EXTRA_DIST = \
	README \
	gnhastweb.conf \
	favicon.png \
	header.html \
	gnhast-icon-48.png \
	cgi-bin/ \
	css/font-awesome.min.css \
	css/gnhastweb.css \
	css/tr_bg.png \
	css/weather-icons-wind.css \
	css/weather-icons-wind.min.css \
	css/weather-icons.css \
	css/weather-icons.min.css \
	scripts/gnhastweb.js \
	scripts/jquery.jscrollpane.min.js \
	scripts/jquery.mousewheel.js \
	scripts/jquery.simpleWeather.min.js \
	fonts/FontAwesome.otf \
	fonts/fontawesome-webfont.eot \
	fonts/fontawesome-webfont.svg \
	fonts/fontawesome-webfont.ttf \
	fonts/fontawesome-webfont.woff \
	fonts/fontawesome-webfont.woff2 \
	fonts/weathericons-regular-webfont.eot \
	fonts/weathericons-regular-webfont.svg \
	fonts/weathericons-regular-webfont.ttf \
	fonts/weathericons-regular-webfont.woff \
	fonts/weathericons-regular-webfont.woff2 \
	src/gnhastweb.pl \
	src/rrdjson.pl \
	src/askfjson.pl

do_subst = sed -e 's,[@]PERL[@],$(PERL),g'

all: $(noinst_X)

$(CGIBIN)/gnhastweb.cgi: src/gnhastweb.pl
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@

$(CGIBIN)/rrdjson.cgi: src/rrdjson.pl
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@

$(CGIBIN)/askfjson.cgi: src/askfjson.pl
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@

$(CGIBIN)/askgjson.cgi: src/askgjson.pl
	$(do_subst) < $(srcdir)/$? > $@
	chmod +x $@
