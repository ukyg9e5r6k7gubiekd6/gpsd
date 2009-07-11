/* Structures for JSON paarsing using only fixed-extent memory */

#include <stdbool.h>
#include <ctype.h>

struct json_attr_t {
    char *attribute;
    enum {integer, real, string, boolean} type;
    union {
	int *integer;
	double *real;
	char *string;
	bool *boolean;
    } addr;
    union {
	int integer;
	double real;
	char string;
	bool boolean;
    } dflt;
};

#define JSON_ATTR_MAX	31	/* max charss in JSON attribute name */
#define JSON_VAL_MAX	63	/* max charss in JSON value part */

int parse_json(const char *, const struct json_attr_t *);

/* json.h ends here */
