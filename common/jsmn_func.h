#ifndef _JSMN_FUNC_H_
#define _JSMN_FUNC_H_

char *jtok_string(jsmntok_t *token, char *buf);
int jtok_int(jsmntok_t *token, char *buf);
double jtok_double(jsmntok_t *token, char *buf);
int jtok_bool(jsmntok_t *token, char *buf);
int jtok_find_token(jsmntok_t *tokens, char *buf, char *match, int maxtoken);

#ifdef JSMN_PARENT_LINKS
int jtok_find_token_val(jsmntok_t *tokens, char *buf, char *match,
			int maxtoken);
int jtok_find_nth_array_member(jsmntok_t *tokens, char *buf, int nth,
			       char *match, int maxtoken);
int jtok_find_token_val_nth_array(jsmntok_t *tokens, char *buf, int nth,
				  char *arraymatch, char *match, int maxtoken);
#endif
#endif /*_JSMN_FUNC_H*/
