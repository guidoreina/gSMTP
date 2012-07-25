#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <sys/types.h>

#define CONF_KEY_NAME_MAXLEN  (255)
#define CONF_VALUE_MAXLEN     (1024)

typedef enum {KEY_HAS_VALUE, KEY_MIGHT_HAVE_CHILDREN} conf_key_type_t;

/* Forward declaration. */
struct _conf_key_list_t;

/* A key has either a value or a list of child keys. */
typedef struct {
	off_t name;

	conf_key_type_t type;
	union {
		off_t value;
		struct _conf_key_list_t *children;
	} u;
} conf_key_t;

/* List of keys ordered by key name. */
typedef struct _conf_key_list_t {
	conf_key_t *keys;
	size_t size;
	size_t used;

	struct _conf_key_list_t *parent;
} conf_key_list_t;

/* Configuration. */
typedef struct {
	conf_key_list_t root;

	char *data;
	size_t size;
	size_t used;

	int ordered;
} configuration_t;

void configuration_init (configuration_t *conf, int ordered);
void configuration_free (configuration_t *conf);

int configuration_load (configuration_t *conf, const char *filename);

void configuration_print (configuration_t *conf);

const char *configuration_get_value (configuration_t *conf, ...);

const char *configuration_get_child (configuration_t *conf, size_t nchild, ...);

ssize_t configuration_get_count (configuration_t *conf, ...);

#endif /* CONFIGURATION_H */
