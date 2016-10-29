/*
 * Copyright (c) 2016
 *      Tim Rightnour.  All rights reserved.
 * Copyright (C) 2003 Tim Rightnour
 * Copyright (C) 2003 Genecys Developer Team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Tim Rightnour may not be used to endorse or promote 
 *    products derived from this software without specific prior written 
 *    permission.
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
   \file xmlparse.c
   \brief Contains generic routines to parse and write XML
   \author Tim Rightnour
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/param.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "common.h"
#include "gnhast.h"
#include "xmlparse.h"

/**
   \brief Replacement xmlGenericErrorFunc
*/

void xml_error_handler(void *ctx, const char *msg, ...)
{
    va_list args;
    char buf[4096];

    va_start(args, msg);
    vsnprintf(buf, 4096, msg, args);
    va_end(args);
    LOG(LOG_ERROR, buf);
}

/** \brief parse a string from the XML stuff */

void parsestring(xmlDocPtr doc, xmlNodePtr cur, char **string)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *string = add_string(tmp);
    free(tmp);
}


/** \brief parse a float from the XML stuff */

void parsefloat(xmlDocPtr doc, xmlNodePtr cur, float *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atof(tmp);
    free(tmp);
}

/** \brief parse a double from the XML stuff */

void parsedouble(xmlDocPtr doc, xmlNodePtr cur, double *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atof(tmp);
    free(tmp);
}

/** \brief parse a long from the XML stuff */

void parselong(xmlDocPtr doc, xmlNodePtr cur, long *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atol(tmp);
    free(tmp);
}

/** \brief parse an int from the XML stuff */

void parseint(xmlDocPtr doc, xmlNodePtr cur, int *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atoi(tmp);
    free(tmp);
}

/** \brief parse a short from the XML stuff */

void parseshort(xmlDocPtr doc, xmlNodePtr cur, int16_t *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atoi(tmp);
    free(tmp);
}

/** \brief parse a char from the XML stuff */

void parsechar(xmlDocPtr doc, xmlNodePtr cur, int8_t *data)
{
    char *tmp;

    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
    *data = atoi(tmp);
    free(tmp);
}



/* XML WRITERS */

/** \brief write a string */
xmlNodePtr write_string(xmlNodePtr node, char *elm, char *string)
{
    return(xmlNewChild(node, NULL, elm, string));
}

/** \brief write a float */
xmlNodePtr write_float(xmlNodePtr node, char *elm, float f)
{
    char buf[256];

    sprintf(buf, "%f", f);
    return(xmlNewChild(node, NULL, elm, buf));
}

/** \brief write a double */
xmlNodePtr write_double(xmlNodePtr node, char *elm, double d)
{
    char buf[256];

    sprintf(buf, "%f", d);
    return(xmlNewChild(node, NULL, elm, buf));
}

/** \brief write a long */
xmlNodePtr write_long(xmlNodePtr node, char *elm, long l)
{
    char buf[256];

    sprintf(buf, "%ld", l);
    return(xmlNewChild(node, NULL, elm, buf));
}

/** \brief write an int */
xmlNodePtr write_int(xmlNodePtr node, char *elm, int i)
{
    char buf[256];

    sprintf(buf, "%d", i);
    return(xmlNewChild(node, NULL, elm, buf));
}

/** \brief write a short */
xmlNodePtr write_short(xmlNodePtr node, char *elm, int16_t i)
{
    char buf[256];

    sprintf(buf, "%d", i);
    return(xmlNewChild(node, NULL, elm, buf));
}

/** \brief write a char */
xmlNodePtr write_char(xmlNodePtr node, char *elm, int8_t i)
{
    char buf[256];

    sprintf(buf, "%d", i);
    return(xmlNewChild(node, NULL, elm, buf));
}
