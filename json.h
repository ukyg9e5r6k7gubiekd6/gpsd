/* Structures for JSON paarsing using only fixed-extent memory */

#include <stdbool.h>
#include <ctype.h>

struct json_array_t { 
    struct json_attr_t *subtype;
    char *baseptr;
    int element_size;
};

struct json_attr_t {
    char *attribute;
    enum {integer, real, string, boolean, array} type;
    union {
	int *integer;
	double *real;
	struct {
	    char *ptr;
	    int len;
	} string;
	bool *boolean;
	struct json_array_t *array;
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

int json_read_object(const char *, char *, const struct json_attr_t *, const char **end);

/* json.h ends here */
