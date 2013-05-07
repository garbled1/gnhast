/* $Id$ */

/*
 * Copyright (c) 2013
 *	  Tim Rightnour.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The name of Tim Rightnour may not be used to endorse or promote 
 *	products derived from this software without specific prior written 
 *	permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TIM RIGHTNOUR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL TIM RIGHTNOUR BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
   \file common.c
   \author Tim Rightnour
   \brief General utility functions
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/queue.h>

#include "common.h"

char *stored_logname = NULL;
extern FILE *logfile;
extern cfg_t *cfg;

int debugmode = 0;

const char *loglevels[LOG_FATAL+1] = {
	"",
	"DEBUG",
	"NOTICE",
	"WARNING",
	"ERROR",
	"FATAL"
};

/**
   \brief open the logfile for writing
*/

FILE *openlog(char *logname)
{
	FILE *l;
	char *p, *buf;
	int madebuf=0;

	if (logname == NULL)
		return NULL;

	if (stored_logname == NULL)
		stored_logname = strdup(logname);

	p = logname;
	buf = NULL;
	if (strlen(p) >= 2) {
		if (p[0] == '.' && p[1] == '/')
			buf = logname;
		if (p[0] == '/')
			buf = logname;
	}
	/* ok, not an absolute/relative path, set it up */

	if (buf == NULL) {
		buf = safer_malloc(64 + strlen(logname));
		sprintf(buf, "%s/log/%s", LOCALSTATEDIR, logname);
		madebuf++;
	}

	l = fopen(buf, "a");
	if (l == NULL)
		LOG(LOG_WARNING, "Cannot open logfile: %s, %s\n", 
		    buf, strerror(errno));
	if (madebuf)
		free(buf);
	return l;
}

/**
   \brief close the logfile
*/
void closelog(void)
{
	if (logfile != NULL)
		fclose(logfile);
}

/**
   \brief Log something
   \param severity Severity of log, see LOG_WARNING etc
   \param s format string
   \param ... arguments
*/
void LOG(int severity, const char *s, ...)
{
	char *t, *timestamp;
	time_t now;
	va_list args;

	if (!debugmode && severity == LOG_DEBUG)
		return;
	now = time(NULL);
	t = safer_malloc(sizeof(char)*26);
	timestamp = safer_malloc(sizeof(char)*26);
	t = ctime_r(&now, t); /* cut the day off */
	(void)strncpy(timestamp, t+4, 15); /* cut the year off */
	free(t);

	if (logfile != NULL) {
		va_start(args, s);
		fprintf(logfile, "%s [%s]:", timestamp, loglevels[severity]);
		free(timestamp);
		vfprintf(logfile, s, args);
		fprintf(logfile, "\n");
		if (severity == LOG_FATAL) {
			vfprintf(stderr, s, args);
			fprintf(stderr, "\n");
		}
		fflush(logfile);
		va_end(args);
	} else {
		va_start(args, s);
		fprintf(stderr, "%s [%s]:", timestamp, loglevels[severity]);
		free(timestamp);
		vfprintf(stderr, s, args);
		fprintf(stderr, "\n");
		fflush(stderr);
		va_end(args);
	}
	if (severity == LOG_FATAL)
		exit(EXIT_FAILURE);
}

/**
   \brief exit on a failure
   \param file file name we bailed in
   \param line line number we bailed on
*/

void _bailout(char *file, int line)
{
	char *pname;
	extern int errno;

#if defined(__NetBSD__)
	pname = (char *)getprogname();
#elif defined(__linux__)
	pname = program_invocation_name;
#endif

	/* the fprintf catches cases where malloc failed, as it will likely fail
	   again in LOG() */
	(void)fprintf(stderr, "%s: Failed in %s:%d: %s %d\n",
	    pname, file, line, strerror(errno), errno);
	(void)LOG(LOG_FATAL, "%s: Failed in %s:%d: %s %d\n",
	    pname, file, line, strerror(errno), errno);
}

/**
   \brief return a zero'd malloc with error checking
   \param size size to malloc
   \param file filename we were called from
   \param line line number we were called from
   \return malloc'd data
*/

void *_safer_malloc(size_t size, char *file, int line)
{
	void *stuff;

	stuff = malloc(size);
	if (stuff == NULL)
		_bailout(file, line);
	memset(stuff, 0, size);
	return(stuff);
}

/**
	\brief Take a string, and turn it into a valid rrdname
	\param orig The original string
	\return A newly malloc'd string that needs to be freed
	\note rrdname is 19 chars max, [A-Za-z0-9_]
	\note Can be used to validate an rrdname when transferring it to data
*/

char *mk_rrdname(char *orig)
{
	char *newbuf, *p;
	int i, l;

	newbuf = safer_malloc(20); /* only need 20 */
	l = strlen(orig);
	for (i=0, p=orig; *p != '\0' && i < l && i < 19;) {
		if (isalpha(*p) || *p == '_' || isdigit(*p)) {
			newbuf[i] = *p;
			i++;
			p++;
		}
	}
	newbuf[i] = '\0';
	return(newbuf);
}

/**
   \brief find the least common multiplier
   \param a first int
   \param b second int
   \return lcm
*/

int lcm(int a,int b)
{
 	int temp = a;

	while(1){
		if(temp % b == 0 && temp % a == 0)
			break;
		temp++;
	}

	return temp;
}

/**
   \brief find the greatest common divisor
   \param a first int
   \param b second int
   \return gcd
*/

int gcd(int a, int b)
{
  if (a==0)
	  return b;
  return gcd ( b%a, a );
}

/**
   \brief write out a pidfile
   \param filename filename to create
*/

void writepidfile(char *filename)
{
	FILE *l;
	pid_t mypid;
	char *buf;
        char *p;
	int madebuf=0;

	p = filename;
	buf = NULL;
	if (strlen(filename) >= 2) {
		if (p[0] == '.' && p[1] == '/')
			buf = filename;
		if (p[0] == '/')
			buf = filename;
	}
	/* ok, not an absolute/relative path, set it up */

	if (buf == NULL) {
		buf = safer_malloc(64 + strlen(filename));
		sprintf(buf, "%s/run/%s", LOCALSTATEDIR, filename);
		madebuf++;
	}

	l = fopen(buf, "w");
	if (l == NULL) {
		LOG(LOG_WARNING, "Cannot open pidfile: %s, %s\n", 
		    buf, strerror(errno));
		return;
	}
	mypid = getpid();
	fprintf(l, "%d", mypid);
	fclose(l);
	if (madebuf)
		free(buf);
}

/**
   \brief A sighup handler
   \param fd unused
   \param what what happened?
   \param arg pointer to conffile name
*/

void cb_sighup(int fd, short what, void *arg)
{
	char *conffile = (char *)arg;

	if (!(what & EV_SIGNAL))
		return;

	LOG(LOG_NOTICE, "Got sighup, re-reading conf file and re-opening log");

	cfg = parse_conf(conffile);
	closelog();
	openlog(stored_logname);
	LOG(LOG_NOTICE, "Logfile re-opened");
}
