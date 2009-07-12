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
	int offset;
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

#define JSON_ERR_OBSTART	-1	/* non-WZ when expecting object start */
#define JSON_ERR_ATTRSTART	-2	/* non-WZ when expecting attrib start */
#define JSON_ERR_BADATTR	-3	/* unknown attribute name */
#define JSON_ERR_ATTRLEN	-4	/* attribute name too long */
#define JSON_ERR_NOARRAY	-5	/* saw [ when not expecting array */
#define JSON_ERR_NOBRAK 	-6	/* array element specified, but no [ */
#define JSON_ERR_STRLONG	-7	/* string value too long */
#define JSON_ERR_TOKLONG	-8	/* token value too long */
#define JSON_ERR_SUBSTRING	-9	/* no strings in array subobjects */
#define JSON_ERR_BADTRAIL	-10	/* garbage while expecting , or } */
#define JSON_ERR_ARRAYSTART	-11	/* didn't find expected array start */

/* json.h ends here */
