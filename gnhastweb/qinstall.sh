#!/bin/sh

CGIDIR=/usr/pkg/libexec/cgi-bin
DOCROOT=/usr/pkg/share/httpd/htdocs
BINDIR=/usr/local/bin

cp cgi-bin/* ${CGIDIR}/
cp -r css scripts fonts gnhast-icon-48.png favicon.png ${DOCROOT}/
rm -f ${CGIDIR}/jsoncoll.cgi
cp ${BINDIR}/jsoncoll.cgi ${CGIDIR}/
cp header.html ${CGIDIR}/

if [ ! -f ${CGIDIR}/gnhastweb.conf ]; then
    cp gnhastweb.conf ${CGIDIR}/
fi
