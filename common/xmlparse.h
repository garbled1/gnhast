#ifndef __xmlparse_h__
#define __xmlparse_h__

/**
   \file xmlparse.h
   \author Tim Rightnour
   \brief XML parsing definitions
*/

void xml_error_handler(void *ctx, const char *msg, ...);
void parsestring(xmlDocPtr doc, xmlNodePtr cur, char **string);
void parsefloat(xmlDocPtr doc, xmlNodePtr cur, float *data);
void parsedouble(xmlDocPtr doc, xmlNodePtr cur, double *data);
void parselong(xmlDocPtr doc, xmlNodePtr cur, long *data);
void parseint(xmlDocPtr doc, xmlNodePtr cur, int *data);
void parseshort(xmlDocPtr doc, xmlNodePtr cur, int16_t *data);
void parsechar(xmlDocPtr doc, xmlNodePtr cur, int8_t *data);

/*writers*/
xmlNodePtr write_string(xmlNodePtr node, char *elm, char *string);
xmlNodePtr write_float(xmlNodePtr node, char *elm, float f);
xmlNodePtr write_double(xmlNodePtr node, char *elm, double d);
xmlNodePtr write_long(xmlNodePtr node, char *elm, long l);
xmlNodePtr write_int(xmlNodePtr node, char *elm, int i);
xmlNodePtr write_short(xmlNodePtr node, char *elm, int16_t i);
xmlNodePtr write_char(xmlNodePtr node, char *elm, int8_t i);

#define write_string_if(node, elm, string) \
    if (string != NULL) \
	write_string(node, elm, string)
#define write_float_if(node, elm, f) \
    if (f != 0.0) \
	write_float(node, elm, f)
#define write_double_if(node, elm, d) \
    if (d != 0.0) \
	write_double(node, elm, d)
#define write_long_if(node, elm, l) \
    if (l != 0) \
	write_long(node, elm, l)
#define write_int_if(node, elm, i) \
    if (i != 0) \
	write_int(node, elm, i)
#define write_short_if(node, elm, i) \
    if (i != 0) \
	write_int(node, elm, i)
#define write_char_if(node, elm, i) \
    if (i != 0) \
	write_int(node, elm, i)

#endif /* _xmlparse_h_ */
