if HAVE_GTK2
GTKGNHASTDIR = gtk-gnhast
GTKINSTDIR = gtk-insteonedit
endif

if MUDFLAP
CFLAGS+=-fmudflap -static
endif

SUBDIRS = libconfuse \
	common \
	gnhastd \
	fakecoll \
	owsrvcoll \
	rrdcoll \
	brulcoll \
	insteoncoll \
	wmr918coll \
	wupwscoll \
	ad2usbcoll \
	icaddycoll \
	venstarcoll \
	moncoll \
	jsoncgicoll \
	urtsicoll \
	astrocoll \
	tools \
	data \
	gnhastweb \
	handlers $(GTKGNHASTDIR) $(GTKINSTDIR)

