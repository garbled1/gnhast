
AC_INIT([gnhast], 0.5)
AM_INIT_AUTOMAKE
LT_INIT
#LT_INIT([disable-shared])
AC_PROG_CC

AC_CONFIG_HEADERS(common/config.h)
AC_CONFIG_SRCDIR(libconfuse/confuse.c)

# Checks for programs.
# disable unnecessary compiler checks
AC_DEFUN([AC_PROG_F77], [:])
AC_DEFUN([AC_PROG_FC], [:])
AC_DEFUN([AC_PROG_CXX], [:])
AC_DEFUN([AC_PROG_CXXCPP], [:])
AC_DEFUN([AC_PROG_OBJC], [:])
AC_DEFUN([AC_PROG_OBJCCPP], [:])
AC_DEFUN([AC_LIBTOOL_CXX], [:])
AC_DEFUN([AC_LIBTOOL_F77], [:])
AC_PROG_CC
AM_PROG_LEX
#AC_DISABLE_SHARED

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h \
	 stdint.h stdlib.h string.h strings.h sys/param.h \
	 sys/socket.h sys/time.h unistd.h event2/event.h \
	 sys/queue.h sys/rbtree.h ])

# Checks for libraries.
AC_SEARCH_LIBS([event_base_new], [event_core event], , AC_MSG_ERROR(["Unable to find libevent 2 core"]))
AC_SEARCH_LIBS([evdns_base_new], [event_extra event], , AC_MSG_ERROR(["Unable to find libevent2 extra"]))
AC_SEARCH_LIBS([bufferevent_get_openssl_error], [event_openssl event], , AC_MSG_ERROR(["Unable to find libevent2 openssl"]), [-lssl -lcrypto])

AC_CHECK_LIB(ssl, SSL_CTX_new,,AC_MSG_ERROR(["Unable to find openSSL library"]), [-lcrypto])
AC_CHECK_LIB(crypto, RAND_poll,,AC_MSG_ERROR(["Unable to find crypto library"]))
AC_CHECK_LIB(m, lrint,,AC_MSG_ERROR(["Unable to find math library"]))
#AC_CHECK_LIB(event_pthreads, evthread_use_pthreads,, AC_MSG_ERROR(["Unable to find libevent2 pthreads"]))
#AC_CHECK_LIB(pthread, pthread_mutex_lock,,AC_MSG_ERROR(["Unable to find pthread library"]))

# Checks for programs
AC_PATH_PROG([NETCAT], [nc], [/usr/pkg/sbin/nc],
	[$PATH$PATH_SEPARATOR/usr/sbin$PATH_SEPARATOR/usr/pkg/sbin$PATH_SEPARATOR/usr/local/sbin])
AC_PATH_PROG([CURL], [curl], [/usr/pkg/bin/curl],
	[$PATH$PATH_SEPARATOR/usr/bin$PATH_SEPARATOR/usr/pkg/bin$PATH_SEPARATOR/usr/local/bin])
AC_PATH_PROG([RRDTOOL], [rrdtool], [/usr/pkg/bin/rrdtool],
	[$PATH$PATH_SEPARATOR/usr/bin$PATH_SEPARATOR/usr/pkg/bin$PATH_SEPARATOR/usr/local/bin])
AC_PATH_PROG([PERL], [perl], [/usr/pkg/bin/perl],
	[$PATH$PATH_SEPARATOR/usr/bin$PATH_SEPARATOR/usr/pkg/bin$PATH_SEPARATOR/usr/local/bin])

PKG_CHECK_MODULES([RRD], [librrd])
PKG_CHECK_MODULES([GTK2], [gtk+-2.0 >= 2.0.0], [HAVE_GTK2=1], [NO_GTK2=1])
PKG_CHECK_MODULES([LIBXML2], [libxml-2.0 >= 2.9.0])
AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)
AC_SUBST(GTK2_CFLAGS)
AC_SUBST(GTK2_LIBS)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME

# Checks for library functions.
AC_CHECK_FUNCS([gethostbyaddr gettimeofday inet_ntoa \
	memset select socket strcspn strrchr strcasecmp \
	strdup strndup])

# Check for BSD rbtree implementation
AC_CHECK_FUNC(rb_tree_init, [HAVE_RBTREE=1],)
AC_CHECK_FUNC(getprogname, , AC_CHECK_LIB(bsd, getprogname,, AC_MSG_ERROR(["No libbsd compat lib"])))
AM_CONDITIONAL([NEED_RBTREE], [test "x$HAVE_RBTREE" = x])
AM_CONDITIONAL([HAVE_GTK2], [test "$HAVE_GTK2" = 1])
AC_CHECK_FUNC(clock_gettime, , AC_CHECK_LIB(rt, clock_gettime,, AC_MSG_ERROR(["No librt"])))

# Enable debugging via mudflap.  This option will cause most libraries
# to be built as archives which are statically linked into the applications.
# All code, as far as possible, is compiled instrumented to catch all
# the bugs valgrind is able to catch.
use_mudflap=no
AC_ARG_ENABLE([mudflap],
AS_HELP_STRING([--enable-mudflap],
[build binaries with mudflap instrumentation]), [dnl
if test "x$enable_mudflap" = xyes; then
  # Check whether the compiler support -fmudflap.
  old_CFLAGS="$CFLAGS"
  AC_MSG_NOTICE([Turning On Mudflap])
  CFLAGS="$CFLAGS -fmudflap"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[use_mudflap=yes],[use_mudflap=fail])
  CFLAGS="$old_CFLAGS"
fi])
if test "$use_mudflap" = fail; then
  AC_MSG_FAILURE([--enable-mudflap requires a compiler which understands this option])
fi
AM_CONDITIONAL(MUDFLAP, test "$use_mudflap" = yes)

AC_CONFIG_FILES([Makefile \
		 gnhastd/Makefile \
		 fakecoll/Makefile \
		 owsrvcoll/Makefile \
		 rrdcoll/Makefile \
		 brulcoll/Makefile \
		 insteoncoll/Makefile \
		 wmr918coll/Makefile \
		 wupwscoll/Makefile \
		 ad2usbcoll/Makefile \
		 icaddycoll/Makefile \
		 venstarcoll/Makefile \
		 moncoll/Makefile \
		 jsoncgicoll/Makefile \
		 urtsicoll/Makefile \
		 astrocoll/Makefile \
		 handlers/Makefile \
		 tools/Makefile \
		 gtk-gnhast/Makefile \
		 gtk-insteonedit/Makefile \
		 data/Makefile \
		 gnhastweb/Makefile \
		 libconfuse/Makefile \
		 common/Makefile])

AC_OUTPUT
