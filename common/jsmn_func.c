/*
 * Copyright (c) 2014
 *      Tim Rightnour.  All rights reserved.
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
   \file http_func.c
   \author Tim Rightnour
   \brief Generic jsmn JSOM helper routines
*/

#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "jsmn.h"
#include "jsmn_func.h"
#include "common.h"

/**
   \brief Allocate a string for a token and return just that
   \param token token to parse
   \param buf buffer containing the parsed string
   \return nul terminated string containing data
   Must be a string.
   Must free return value
*/

char *jtok_string(jsmntok_t *token, char *buf)
{
	char *data;
	size_t len;

	if (token->type != JSMN_STRING)
		return NULL;
	len = token->end - token->start;
	if (len < 1)
		return NULL;
	data = safer_malloc(len+1);

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	return data;
}

/**
   \brief Return int value of token
   \param token token to parse
   \param buf buffer containing the parsed string
   \return zero if error, or value
   Cannot be array type
*/

int jtok_int(jsmntok_t *token, char *buf)
{
	char data[256];
	size_t len;

	if (token->type == JSMN_ARRAY)
		return 0;
	len = token->end - token->start;
	if (len < 1 || len > 255)
		return 0;

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	return atoi(data);
}

/**
   \brief Return double value of token
   \param token token to parse
   \param buf buffer containing the parsed string
   \return value or 0.0 if error
   Cannot be array type
*/

double jtok_double(jsmntok_t *token, char *buf)
{
	char data[256];
	size_t len;

	if (token->type == JSMN_ARRAY)
		return 0.0;
	len = token->end - token->start;
	if (len < 1 || len > 255)
		return 0.0;

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	return atof(data);
}

/**
   \brief Return bool value of token
   \param token token to parse
   \param buf buffer containing the parsed string
   \return zero if error, or value
   Cannot be array type
   if anything other than "true" returns false
*/

int jtok_bool(jsmntok_t *token, char *buf)
{
	char data[256];
	size_t len;

	if (token->type == JSMN_ARRAY)
		return 0;
	len = token->end - token->start;
	if (len < 1 || len > 255)
		return 0;

	strncpy(data, buf+token->start, len);
	data[len] = '\0';
	if (strcasecmp(data, "true") == 0)
		return 1;
	return 0;
}

/**
   \brief Find a specific token by string name
   \param tokens array of tokens
   \param buf parsed buffer
   \param match string to match
   \param maxtoken last token
*/

int jtok_find_token(jsmntok_t *tokens, char *buf, char *match, int maxtoken)
{
	int i;
	size_t len, mlen;

	if (match == NULL || buf == NULL)
		return -1;
	mlen = strlen(match);
	for (i=0; i<maxtoken; i++) {
		if (tokens[i].type != JSMN_STRING)
			continue;
		len = tokens[i].end - tokens[i].start;
		if (len != mlen)
			continue;
		if (strncmp(buf+tokens[i].start, match, len) == 0)
			return i;
	}
	return -1;
}

#ifdef JSMN_TOKEN_LINKS

/**
   \brief Find a specific token's value by string name of parent
   \param tokens array of tokens
   \param buf parsed buffer
   \param match string to match
   \param maxtoken last token
   We don't care what kind of value it is, might be an array?  who knows.
*/

int jtok_find_token_val(jsmntok_t *tokens, char *buf, char *match,
			int maxtoken)
{
	int tok, i;

	tok = jtok_find_token(tokens, buf, match, maxtoken);
	if (tok == -1)
		return -1;
	for (i = tok+1; i < maxtoken; i++)
		if (tokens[i].valueof == tok)
			return i;
	return -1;
}

/**
   \brief Find a specific array member inside array named match
   \param tokens array of tokens
   \param buf parsed buffer
   \param nth array member to find, count from 0
   \param match string to match
   \param maxtoken last token
*/

int jtok_find_nth_array_member(jsmntok_t *tokens, char *buf, int nth,
			       char *match, int maxtoken)
{
	int i, j, array_size, tok;

	tok = jtok_find_token_val(tokens, buf, match, maxtoken);
	if (tok < 0 || tokens[tok].type != JSMN_ARRAY)
		return -1;

	array_size = tokens[tok].size;
	if (array_size < nth)
		return -1;

	if (nth == 0)
		return tok+1;

	for (i=1,j=0; i < nth+1; i++)
		/* +1 to skip primitives */
		j += tokens[tok+1+j].size + 1;

	if (j+tok < maxtoken)
		return j + tok + 1; /* skip one more primitive */
	return -1;
}

/**
   \brief Find the value of a field inside nth member of array named match
   \param tokens array of tokens
   \param buf parsed buffer
   \param nth array member to find, count from 0
   \param arraymatch array name
   \param match string to match
   \param maxtoken last token
*/
int jtok_find_token_val_nth_array(jsmntok_t *tokens, char *buf, int nth,
				  char *arraymatch, char *match, int maxtoken)
{
	int i, tok, aend;
	size_t mlen, len;

	if (match == NULL)
		return -1;

	tok = jtok_find_nth_array_member(tokens, buf, nth, arraymatch,
					 maxtoken);
	if (tok == -1)
		return -1;

	aend = tok + tokens[tok].size;
	mlen = strlen(match);
	for (i=tok+1; i < aend; i++) {
		if (tokens[i].type != JSMN_STRING)
			continue;
		len = tokens[i].end - tokens[i].start;
		if (len != mlen)
			continue;
		if (strncmp(buf+tokens[i].start, match, len) == 0 &&
			tokens[i+1].valueof == i)
			return i+1;
	}
	return -1;
}

#endif /*JSMN_TOKEN_LINKS*/

