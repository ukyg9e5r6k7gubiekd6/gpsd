/* json.c - parse JSON into fixed-extent data structures */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpsd_config.h"	/* for strlcpy() prototype */
#include "json.h"

int json_read_array(const char *, const struct json_array_t *, const char **);

int json_read_object(const char *cp, const struct json_attr_t *attrs, int offset, const char **end)
{
    enum {init, await_attr, in_attr, await_value, 
	  in_val_string, in_val_token, post_val} state = 0;
    char attrbuf[JSON_ATTR_MAX+1], *pattr = NULL;
    char valbuf[JSON_VAL_MAX+1], *pval = NULL;
    const struct json_attr_t *cursor;
    int substatus;

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
	    cursor->addr.boolean[offset] = cursor->dflt.boolean;
	    break;
	case array:	/* silences a compiler warning */
	    break;
	}

    /* parse input JSON */
    for (; *cp; cp++) {
#ifdef JSONDEBUG
	(void) printf("State %d, looking at '%c'\n", state, *cp);
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
		--cp;
		if (cursor->type != array)
		    return JSON_ERR_NOARRAY;	/* saw [ when not expecting array */
		substatus = json_read_array(cp, &cursor->addr.array, &cp);
		if (substatus < 0)
		    return substatus;
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
	    if (*cp == '"') {
		*pval = '\0';
#ifdef JSONDEBUG
		(void) printf("Collected string value %s\n", valbuf);
#endif /* JSONDEBUG */
		state = post_val;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1 || pval > valbuf + cursor->addr.string.len - 1)
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
	    case array:	/* silences a compiler warning */
		break;
	    }
	    if (isspace(*cp))
		continue;
	    else if (*cp == ',')
		state = await_attr;
	    else if (*cp == '}')
		return 0;
	    else
		return JSON_ERR_BADTRAIL;	/* garbage while expecting comma or } */
	    break;
	}
    }

    if (end != NULL)
	*end = cp;
    return 0;
}

int json_read_array(const char *cp, const struct json_array_t *array, const char **end)
{
    int substatus;

    while (isspace(*cp))
	cp++;
    if (*cp != '[')
	return JSON_ERR_ARRAYSTART;	/* didn't find expected array start */

    for (;;) {
	while (isspace(*cp))
	    cp++;
	if (*cp == ']')
	    break;
    }

    if (end != NULL)
	*end = cp;
    return 0;
}
