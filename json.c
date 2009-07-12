/* json.c - parse JSON into fixed-extent data structures */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "gpsd_config.h"	/* for strlcpy() prototype */
#include "json.h"

int json_read_array(const char *, const struct json_attr_t *, const char **);

int json_read_object(const char *cp, const struct json_attr_t *attrs, const char **end)
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
#ifdef JSONDEBUG
		(void) printf("Collected attribute name %s\n", attrbuf);
#endif /* JSONDEBUG */
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
		*pattr++ = *cp;
	    break;
	case await_value:
	    if (isspace(*cp) || *cp == ':')
		continue;
	    else if (*cp == '[') {
		--cp;
		if (cursor->type != array)
		    return -5;	/* saw [ when not expecting array */
		substatus = json_read_array(cp, cursor->addr.array, &cp);
		if (substatus < 0)
		    return substatus;
	    } else if (cursor->type == array)
		return -6;	/* array element was specified, but no [ */
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
	    } else if (pval > valbuf + JSON_VAL_MAX - 1)
		return -7;	/* value too long */
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
		return -8;	/* value too long */
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
		(void)strlcpy(cursor->addr.string, valbuf, sizeof(valbuf));
		break;
	    case boolean:
#ifdef JSONDEBUG
		(void) printf("Boolean value '%s' processed to %d\n", valbuf, !strcmp(valbuf, "true"));
#endif /* JSONDEBUG */
		*(cursor->addr.boolean) = (bool)!strcmp(valbuf, "true");
		break;
	    case array:
		// FIXME: must actually handle this case
		break;
	    }
	    if (isspace(*cp))
		continue;
	    else if (*cp == ',')
		state = await_attr;
	    else if (*cp == '}')
		return 0;
	    else
		return -9;	/* garbage while expecting comma or } */
	    break;
	}
    }

    if (end != NULL)
	*end = cp;
    return 0;
}

int json_read_array(const char *cp, const struct json_attr_t *attrs, const char **end)
{
    while (isspace(*cp))
	cp++;
    if (*cp != '[')
	return -10;	/* didn't find expected array start */

    // Array parsing logic goes here

    if (end != NULL)
	*end = cp;
    return 0;
}
