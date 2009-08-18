/* $Id$ */
/****************************************************************************

NAME
   json.c - parse JSON into fixed-extent data structures

DESCRIPTION
   This module parses a large subset of JSON (JavaScript Object
Notation).  Unlike more general JSON parsers, it doesn't use malloc(3)
and doesn't support polymorphism; you need to give it a set of
template structures describing the expected shape of the incoming
JSON, and it will error out if that shape is not matched.  When the
parse succeds, attribute values will be extracted into static
locations specified in the template structures.

   The "shape" of a JSON object in the type signature of its
attributes (and attribute values, and so on recursively down through
all nestings of objects and arrays).  This parser is indifferent to
the order of attributes at any level, but you have to tell it in
advance what the type of each attribute value will be and where the
parsed value will be stored. The tamplate structures may supply
default values to be used when an expected attribute is omitted.

   The dialect this parses has some limitations.  First, it cannot
recognize the JSON "null" value.  Secondly, arrays may only have
objects or strings - not reals or integers or floats - as elements
(this limitation could be easily removed if required). Third, all
elements of an array must be of the same type.

   There are separata entry points for beginning a parse of either
JSON object or a JSON array. JSON "float" quantities are actually
stored as doubles.

***************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpsd_config.h"	/* for strlcpy() prototype */
#include "json.h"

int json_read_object(const char *cp, const struct json_attr_t *attrs, int offset, const char **end)
{
    enum {init, await_attr, in_attr, await_value, 
	  in_val_string, in_val_token, post_val} state = 0;
    char attrbuf[JSON_ATTR_MAX+1], *pattr = NULL;
    char valbuf[JSON_VAL_MAX+1], *pval = NULL;
    const struct json_attr_t *cursor;
    int substatus, maxlen;

    /* stuff fields with defaults in case they're omitted in the JSON input */
    for (cursor = attrs; cursor->attribute != NULL; cursor++)
	switch(cursor->type)
	{
	case integer:
	    cursor->addr.integer[offset] = cursor->dflt.integer;
	    break;
	case real:
	    cursor->addr.real[offset] = cursor->dflt.real;
	    break;
	case string:
	    cursor->addr.string.ptr[offset] = '\0';
	    break;
	case boolean:
	    /* nullbool default says not to set the value at all */
	    if (cursor->dflt.boolean != nullbool)
		cursor->addr.boolean[offset] = cursor->dflt.boolean;
	    break;
	case object:	/* silences a compiler warning */
	case array:
	case check:
	    break;
	}

    /* parse input JSON */
    for (; *cp; cp++) {
#ifdef JSONDEBUG
	(void) printf("State %d, looking at '%c' (%p)\n", state, *cp, cp);
#endif /* JSONDEBUG */
	switch (state)
	{
	case init:
	    if (isspace(*cp))
		continue;
	    else if (*cp == '{')
		state = await_attr;
	    else
		return JSON_ERR_OBSTART;	/* non-WS when expecting object start */
	    break;
	case await_attr:
	    if (isspace(*cp))
		continue;
	    else if (*cp == '"') {
		state = in_attr;
		pattr = attrbuf;
	    } else if (*cp == '}')
		break;
	    else
		return JSON_ERR_ATTRSTART;	/* non-WS while expecting attribute */
	    break;
	case in_attr:
	    if (*cp == '"') {
		*pattr++ = '\0';
#ifdef JSONDEBUG
		(void) printf("Collected attribute name %s\n", attrbuf);
#endif /* JSONDEBUG */
		for (cursor = attrs; cursor->attribute != NULL; cursor++)
		    if (strcmp(cursor->attribute, attrbuf)==0)
			break;
		if (cursor->attribute == NULL)
		    return JSON_ERR_BADATTR;	/* unknown attribute name */
		state = await_value;
		pval = valbuf;
	    } else if (pattr >= attrbuf + JSON_ATTR_MAX - 1)
		return JSON_ERR_ATTRLEN;	/* attribute name too long */
	    else
		*pattr++ = *cp;
	    break;
	case await_value:
	    if (isspace(*cp) || *cp == ':')
		continue;
	    else if (*cp == '[') {
		if (cursor->type != array)
		    return JSON_ERR_NOARRAY;	/* saw [ when not expecting array */
		substatus = json_read_array(cp, &cursor->addr.array, &cp);
		if (substatus != 0)
		    return substatus;
		state = post_val;
	    } else if (cursor->type == array)
		return JSON_ERR_NOBRAK;	/* array element was specified, but no [ */
	    else if (*cp == '"') {
		state = in_val_string;
		pval = valbuf;
	    } else {
		state = in_val_token;
		pval = valbuf;
		*pval++ = *cp;
	    }
	    break;
	case in_val_string:
	    if (cursor->type == string)
		maxlen = cursor->addr.string.len - 1;
	    else if (cursor->type == check)
		maxlen = strlen(cursor->dflt.check);
	    if (*cp == '"') {
		*pval = '\0';
#ifdef JSONDEBUG
		(void) printf("Collected string value %s\n", valbuf);
#endif /* JSONDEBUG */
		state = post_val;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1 || pval > valbuf + maxlen)
		return JSON_ERR_STRLONG;	/* value too long */
	    else
		*pval++ = *cp;
	    break;
	case in_val_token:
	    if (isspace(*cp) || *cp == ',' || *cp == '}') {
		*pval = '\0';
#ifdef JSONDEBUG
		(void) printf("Collected token value %s\n", valbuf);
#endif /* JSONDEBUG */
		state = post_val;
		if (*cp == '}' || *cp == ',')
		    --cp;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1)
		return JSON_ERR_TOKLONG;	/* value too long */
	    else
		*pval++ = *cp;
	    break;
	case post_val:
	    switch(cursor->type)
	    {
	    case integer:
		cursor->addr.integer[offset] = atoi(valbuf);
		break;
	    case real:
		cursor->addr.real[offset] = atof(valbuf);
		break;
	    case string:
		(void)strncpy(cursor->addr.string.ptr+offset, valbuf, cursor->addr.string.len);
		break;
	    case boolean:
		cursor->addr.boolean[offset] = (bool)!strcmp(valbuf, "true");
		break;
	    case check:
		if (strcmp(cursor->dflt.check, valbuf)!=0)
		    return JSON_ERR_CHECKFAIL;
	    case object:	/* silences a compiler warning */
	    case array:
		break;
	    }
	    if (isspace(*cp))
		continue;
	    else if (*cp == ',')
		state = await_attr;
	    else if (*cp == '}')
		goto good_parse;
	    else
		return JSON_ERR_BADTRAIL;	/* garbage while expecting comma or } */
	    break;
	}
    }

good_parse:
    if (end != NULL)
	*end = ++cp;
    return 0;
}

int json_read_array(const char *cp, const struct json_array_t *arr, const char **end)
{
    int substatus, offset;
    char *tp;

#ifdef JSONDEBUG
    (void) printf("Entered json_read_array()\n");
#endif /* JSONDEBUG */

    while (isspace(*cp))
	cp++;
    if (*cp != '[')
	return JSON_ERR_ARRAYSTART;	/* didn't find expected array start */
    else
	cp++;

    tp = arr->arr.strings.store;
    if (arr->count != NULL)
	*(arr->count) = 0;
    for (offset = 0; offset < arr->maxlen; offset++) {
#ifdef JSONDEBUG
	(void) printf("Looking at %s\n", cp);
#endif /* JSONDEBUG */
	switch (arr->element_type)
	{
	case string:
	    if (isspace(*cp))
		cp++;
	    if (*cp != '"')
		return JSON_ERR_BADSTRING;
	    else
		++cp;
	    arr->arr.strings.ptrs[offset] = tp;
	    for (; tp - arr->arr.strings.store < arr->arr.strings.storelen; tp++)
		if (*cp == '"') {
		    ++cp;
		    *tp++ = '\0';
		    goto stringend;
		} else if (*cp == '\0')
		    return JSON_ERR_BADSTRING;
	        else {
		    *tp = *cp++;
		}
	    return JSON_ERR_BADSTRING;
	stringend:
	    break;
	case object:
#ifdef JSONDEBUG
	    (void) printf("Subarray parse begins.\n");
#endif /* JSONDEBUG */
	    substatus = json_read_object(cp, arr->arr.subtype, offset, &cp);
#ifdef JSONDEBUG
	    (void) printf("Subarray parse ends.\n");
#endif /* JSONDEBUG */
	    if (substatus != 0)
		return substatus;
	    break;
	case integer:
	case real:
	case boolean:
	case array:
	case check:
	    return JSON_ERR_SUBTYPE;
	}
	if (arr->count != NULL)
	    (*arr->count)++;
	if (isspace(*cp))
	    cp++;
	if (*cp == ']') {
#ifdef JSONDEBUG
	    (void) printf("End of array found.\n");
#endif /* JSONDEBUG */
	    goto breakout;
	} else if (*cp == ',')
	    cp++;
	else
	    return JSON_ERR_BADSUBTRAIL;
    }
    return JSON_ERR_SUBTOOLONG;
breakout:
    if (end != NULL)
	*end = cp;
    return 0;
}

const char *json_error_string(int err)
{
    const char *errors[] = {
	"unknown error while parsing JSON",
	"non-whitespace when expecting object start",
	"non-whitespace when expecting attribute start",
	"unknown attribute name",
	"attribute name too long",
	"saw [ when not expecting array",
	"array element specified, but no [",
	"string value too long",
	"token value too long",
	"garbage while expecting , or }",
	"didn't find expected array start",
	"error while parsing object array",
	"too many array elements",
	"garbage while expecting array comma",
	"unsupported array element type",
	"error while string parsing",
	"check attribute not matched",
    };

    if (err <= 0 || err >= (int)(sizeof(errors)/sizeof(errors[0])))
	return errors[0];
    else
	return errors[err];
}
