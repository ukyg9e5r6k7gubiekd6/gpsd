/* json.c - parse JSON into fixed-extent data structures */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "json.h"

int parse_json(const char *cp, const struct json_attr_t *attrs)
{
    enum {init, await_attr, in_attr, await_value, 
	  in_val_string, in_val_token, post_val} state = 0;
    char attrbuf[JSON_ATTR_MAX+1], *pattr = NULL;
    char valbuf[JSON_VAL_MAX+1], *pval = NULL;
    struct json_attr_t *cursor;

    /* stuff fields with defaults in case they're omitted in the JSON input */
    for (cursor = attrs; cursor->attribute != NULL; cursor++)
	switch(cursor->type)
	{
	case integer:
	    *(cursor->addr.integer) = cursor->dflt.integer;
	    break;
	case real:
	    *(cursor->addr.real) = cursor->dflt.real;
	    break;
	case string:
	    cursor->addr.string[0] = '\0';
	    break;
	case boolean:
	    *(cursor->addr.boolean) = cursor->dflt.boolean;
	    break;
	}

    /* parse input JSON */
    for (; *cp; cp++) {
	switch (state)
	{
	case init:
	    if (isspace(*cp))
		continue;
	    else if (*cp == '{')
		state = await_attr;
	    else
		return -1;	/* non-whitespace when expecting object start */
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
		return -2;	/* non-whitespace while expecting attribute */
	    break;
	case in_attr:
	    if (*cp == '"') {
		*pattr++ = '\0';
		for (cursor = attrs; cursor->attribute != NULL; cursor++)
		    if (strcmp(cursor->attribute, attrbuf)==0)
			break;
		if (cursor->attribute == NULL)
		    return -3;	/* unknown attribute name */
		state = await_value;
		pval = valbuf;
	    } else if (pattr >= attrbuf + JSON_ATTR_MAX - 1)
		return -4;	/* attribute name too long */
	    else
		*pattr = *cp;
	    break;
	case await_value:
	    if (isspace(*cp) || *cp == ':')
		continue;
	    else if (*cp == '"') {
		state = in_val_string;
		pval = valbuf;
	    } else {
		state = in_val_token;
		pval = valbuf;
	    }
	    break;
	case in_val_string:
	    if (*cp == '"') {
		state = post_val;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1)
		return -5;	/* value too long */
	    else
		*pval++ = *cp;
	    break;
	case in_val_token:
	    if (isspace(*cp) || *cp == ',' || *cp == '}') {
		state = post_val;
		if (*cp == '}')
		    --cp;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1)
		return -5;	/* value too long */
	    else
		*pval++ = *cp;
	    break;
	case post_val:
	    switch(cursor->type)
	    {
	    case integer:
		*(cursor->addr.integer) = atoi(valbuf);
		break;
	    case real:
		*(cursor->addr.real) = atof(valbuf);
		break;
	    case string:
		(void)strcpy(cursor->addr.string, valbuf);
		break;
	    case boolean:
		*(cursor->addr.boolean) = (bool)!strcmp(valbuf, "true");
		break;
	    }
	    if (isspace(*cp))
		continue;
	    else if (*cp == ',')
		state = await_attr;
	    else if (*cp == '}')
		return 0;
	    else
		return -6;	/* garbage while expecting comma or } */
	    break;
	}
    }

    return 0;
}
