#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "configuration.h"

#define COMMENT '#'
#define SAFE    ".-_/@"

#define IS_ALPHA(x) (((x >= 'A') && (x <= 'Z')) || ((x >= 'a') && (x <= 'z')))
#define IS_DIGIT(x) ((x >= '0') && (x <= '9'))

static void remove_key_list (conf_key_list_t *key_list);
static conf_key_list_t *create_key_list (void);
static conf_key_t *add_key (configuration_t *conf, conf_key_list_t *parent_list, conf_key_t *parent_key, conf_key_list_t **key_list, char *key_name, size_t len);
static int set_value (configuration_t *conf, conf_key_t *key, char *value, size_t len);
static int search (configuration_t *conf, conf_key_list_t *key_list, char *key_name, unsigned int *position);
static int add_string (configuration_t *conf, char *string, size_t len);
static void print_key_list (configuration_t *conf, conf_key_list_t *key_list, size_t depth);
static conf_key_t *get_key (configuration_t *conf, va_list ap);

void configuration_init (configuration_t *conf, int ordered)
{
	conf->root.keys = NULL;
	conf->root.size = 0;
	conf->root.used = 0;

	conf->root.parent = NULL;

	conf->data = NULL;
	conf->size = 0;
	conf->used = 0;

	conf->ordered = ordered;
}

void remove_key_list (conf_key_list_t *key_list)
{
	size_t i;

	if (key_list->keys) {
		for (i = 0; i < key_list->used; i++) {
			if ((key_list->keys[i].type == KEY_MIGHT_HAVE_CHILDREN) && (key_list->keys[i].u.children)) {
				remove_key_list (key_list->keys[i].u.children);

				free (key_list->keys[i].u.children);
			}
		}

		free (key_list->keys);
		key_list->keys = NULL;
	}

	key_list->size = 0;
	key_list->used = 0;

	key_list->parent = NULL;
}

void configuration_free (configuration_t *conf)
{
	remove_key_list (&conf->root);

	if (conf->data) {
		free (conf->data);
		conf->data = NULL;
	}

	conf->size = 0;
	conf->used = 0;
}

conf_key_list_t *create_key_list (void)
{
	conf_key_list_t *key_list;

	key_list = (conf_key_list_t *) calloc (1, sizeof (conf_key_list_t));
	if (!key_list) {
		return NULL;
	}

	return key_list;
}

conf_key_t *add_key (configuration_t *conf, conf_key_list_t *parent_list, conf_key_t *parent_key, conf_key_list_t **key_list, char *key_name, size_t len)
{
	conf_key_t *keys;
	unsigned int position;
	size_t size;

	/* If we must start a new key list... */
	if (parent_key) {
		if (!parent_key->u.children) {
			*key_list = create_key_list ();
			if (!*key_list) {
				/* Couldn't allocate memory. */
				return NULL;
			}

			parent_key->u.children = *key_list;
			(*key_list)->parent = parent_list;
		} else {
			*key_list = parent_key->u.children;
		}
	}

	if ((*key_list)->used == (*key_list)->size) {
		size = (*key_list)->size + 20;
		keys = (conf_key_t *) calloc (size, sizeof (conf_key_t));
		if (!keys) {
			/* Couldn't allocate memory. */
			return NULL;
		}

		if ((*key_list)->used > 0) {
			memcpy (keys, (*key_list)->keys, (*key_list)->used * sizeof (conf_key_t));
			free ((*key_list)->keys);
		}

		(*key_list)->keys = keys;
		(*key_list)->size = size;
	}

	if (search (conf, *key_list, key_name, &position) < 0) {
		if (position < (*key_list)->used) {
			memmove (&((*key_list)->keys[position + 1]), &((*key_list)->keys[position]), ((*key_list)->used - position) * sizeof (conf_key_t));

			(*key_list)->keys[position].u.children = NULL;
		}

		(*key_list)->keys[position].name = conf->used;

		if (add_string (conf, key_name, len) < 0) {
			/* Couldn't allocate memory. */
			return NULL;
		}

		(*key_list)->used++;
	}

	return &((*key_list)->keys[position]);
}

int set_value (configuration_t *conf, conf_key_t *key, char *value, size_t len)
{
	key->type = KEY_HAS_VALUE;

	key->u.value = conf->used;

	return add_string (conf, value, len);
}

int search (configuration_t *conf, conf_key_list_t *key_list, char *key_name, unsigned int *position)
{
	int i, j, pivot;
	int ret;

	if (!conf->ordered) {
		for (i = 0; i < key_list->used; i++) {
			if (strcmp (key_name, (char *) (conf->data + key_list->keys[i].name))) {
				*position = (unsigned int) i;
				return 0;
			}
		}
	} else {
		i = 0;
		j = key_list->used - 1;

		while (i <= j) {
			pivot = (i + j) / 2;

			ret = strcmp (key_name, (char *) (conf->data + key_list->keys[pivot].name));
			if (ret < 0) {
				j = pivot - 1;
			} else if (ret == 0) {
				*position = (unsigned int) pivot;
				return 0;
			} else {
				i = pivot + 1;
			}
		}
	}

	*position = (unsigned int) i;

	return -1;
}

int add_string (configuration_t *conf, char *string, size_t len)
{
	char *data;
	size_t size;

	if (conf->used + len >= conf->size) {
		size = conf->size + (len + (50 * 1024));
		data = (char *) realloc (conf->data, size);
		if (!data) {
			return -1;
		}

		conf->data = data;
		conf->size = size;
	}

	if ((string) && (*string)) {
		memcpy (conf->data + conf->used, string, len);
		conf->used += len;
	}

	conf->data[conf->used++] = 0;

	return 0;
}

int configuration_load (configuration_t *conf, const char *filename)
{
	FILE *file;
	char buffer[20 * 1024];
	char *ptr, *ptrEnd;
	off_t state = 0;
	size_t bytes;
	char key_name[CONF_KEY_NAME_MAXLEN + 1];
	char value[CONF_VALUE_MAXLEN + 1];
	off_t nline = 1;
	size_t len;
	char c;
	conf_key_t *key = NULL;
	conf_key_list_t *current_list = NULL, *parent_list = NULL;

	configuration_free (conf);

	/* Open file for reading. */
	file = fopen (filename, "rb");
	if (!file) {
		fprintf (stderr, "Couldn't open [%s] for reading.\n", filename);
		return -1;
	}

	current_list = &conf->root;
	len = 0;

	do {
		bytes = fread (buffer, 1, sizeof (buffer), file);
		if (bytes <= 0) {
			*buffer = '\n';
			ptrEnd = buffer + 1;
		} else {
			ptrEnd = buffer + bytes;
		}

		ptr = buffer;

		while (ptr < ptrEnd) {
			switch (state) {
				case 0:
					/* No keys/values found yet. */
					if (*ptr == '{') {
						if ((!key) || (!current_list)) {
							/* No parent key present. */
							fprintf (stderr, "[%s:%lu] No parent key present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						parent_list = current_list;
						current_list = NULL;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						current_list = parent_list;
						parent_list = current_list->parent;
						key = NULL;
					} else if (*ptr == COMMENT) {
						state = 8;
					} else if (*ptr == '\n') {
						nline++;
					} else if ((IS_ALPHA (*ptr)) || (IS_DIGIT (*ptr)) || (strchr (SAFE, *ptr))) {
						if (current_list) {
							key = NULL;
						}

						*key_name = *ptr;
						len = 1;

						state = 1;
					} else if ((*ptr != '\r') && (*ptr != ' ') && (*ptr != '\t')) {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 1:
					/* Reading key. */
					if ((IS_ALPHA (*ptr)) || (IS_DIGIT (*ptr)) || (strchr (SAFE, *ptr))) {
						if (len >= CONF_KEY_NAME_MAXLEN) {
							/* Key name too long. */
							fprintf (stderr, "[%s:%lu] Key name too long (> %d).\n", filename, nline, CONF_KEY_NAME_MAXLEN);
							fclose (file);
							return -1;
						}

						key_name[len++] = *ptr;
					} else if ((*ptr == '\r') || (*ptr == ' ') || (*ptr == '\t')) {
						key_name[len] = 0;
						if ((key = add_key (conf, parent_list, key, &current_list, key_name, len)) == NULL) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						state = 2;
					} else if (*ptr == '\n') {
						key_name[len] = 0;
						if ((key = add_key (conf, parent_list, key, &current_list, key_name, len)) == NULL) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key->type = KEY_MIGHT_HAVE_CHILDREN;

						nline++;

						state = 0;
					} else if (*ptr == '=') {
						key_name[len] = 0;
						if ((key = add_key (conf, parent_list, key, &current_list, key_name, len)) == NULL) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						state = 3;
					} else if (*ptr == '{') {
						key_name[len] = 0;
						if ((key = add_key (conf, parent_list, key, &current_list, key_name, len)) == NULL) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key->type = KEY_MIGHT_HAVE_CHILDREN;

						parent_list = current_list;
						current_list = NULL;

						state = 0;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key_name[len] = 0;
						if ((key = add_key (conf, parent_list, key, &current_list, key_name, len)) == NULL) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key->type = KEY_MIGHT_HAVE_CHILDREN;

						current_list = parent_list;
						parent_list = current_list->parent;
						key = NULL;

						state = 0;
					} else {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 2:
					/* Space after key. */
					if (*ptr == '\n') {
						key->type = KEY_MIGHT_HAVE_CHILDREN;

						nline++;

						state = 0;
					} else if (*ptr == COMMENT) {
						key->type = KEY_MIGHT_HAVE_CHILDREN;

						state = 8;
					} else if (*ptr == '=') {
						state = 3;
					} else if (*ptr == '{') {
						key->type = KEY_MIGHT_HAVE_CHILDREN;

						parent_list = current_list;
						current_list = NULL;

						state = 0;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key->type = KEY_MIGHT_HAVE_CHILDREN;

						current_list = parent_list;
						parent_list = current_list->parent;
						key = NULL;

						state = 0;
					} else if ((*ptr != '\r') && (*ptr != ' ') && (*ptr != '\t')) {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 3:
					/* After '='. */
					if ((IS_ALPHA (*ptr)) || (IS_DIGIT (*ptr)) || (strchr (SAFE, *ptr))) {
						*value = *ptr;
						len = 1;

						state = 4;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						/* The key has no value. */
						if (set_value (conf, key, "", 0) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						current_list = parent_list;
						parent_list = current_list->parent;
						key = NULL;

						state = 0;
					} else if (*ptr == '"') {
						len = 0;

						state = 6;
					} else if (*ptr == '\n') {
						/* The key has no value. */
						if (set_value (conf, key, "", 0) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key = NULL;

						nline++;

						state = 0;
					} else if ((*ptr != '\r') && (*ptr != ' ') && (*ptr != '\t')) {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 4:
					/* Reading value. */
					if ((IS_ALPHA (*ptr)) || (IS_DIGIT (*ptr)) || (strchr (SAFE, *ptr))) {
						if (len >= CONF_VALUE_MAXLEN) {
							/* Value too long. */
							fprintf (stderr, "[%s:%lu] Value too long (> %d).\n", filename, nline, CONF_VALUE_MAXLEN);
							fclose (file);
							return -1;
						}

						value[len++] = *ptr;
					} else if ((*ptr == '\r') || (*ptr == ' ') || (*ptr == '\t')) {
						if (set_value (conf, key, value, len) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key = NULL;

						state = 5;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						if (set_value (conf, key, value, len) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						current_list = parent_list;
						parent_list = current_list->parent;
						key = NULL;

						state = 0;
					} else if (*ptr == '\n') {
						if (set_value (conf, key, value, len) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key = NULL;

						nline++;

						state = 0;
					} else {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 5:
					/* After having read value. */
					if (*ptr == '\n') {
						nline++;

						state = 0;
					} else if (*ptr == '}') {
						if (!parent_list) {
							/* No parent list present. */
							fprintf (stderr, "[%s:%lu] No parent list present.\n", filename, nline);
							fclose (file);
							return -1;
						}

						current_list = parent_list;
						parent_list = current_list->parent;

						state = 0;
					} else if (*ptr == COMMENT) {
						state = 8;
					} else if ((*ptr != '\r') && (*ptr != ' ') && (*ptr != '\t')) {
						/* Wrong character. */
						fprintf (stderr, "[%s:%lu] Wrong character found: [%c].\n", filename, nline, *ptr);
						fclose (file);
						return -1;
					}

					break;
				case 6:
					/* The value is between quotation marks. */
					if (*ptr == '"') {
						if (set_value (conf, key, value, len) < 0) {
							/* Couldn't allocate memory. */
							fprintf (stderr, "[%s:%lu] Couldn't allocate memory.\n", filename, nline);
							fclose (file);
							return -1;
						}

						key = NULL;

						state = 5;
					} else if (*ptr == '\\') {
						state = 7;
					} else if (*ptr == '\n') {
						if (len >= CONF_VALUE_MAXLEN) {
							/* Value too long. */
							fprintf (stderr, "[%s:%lu] Value too long (> %d).\n", filename, nline, CONF_VALUE_MAXLEN);
							fclose (file);
							return -1;
						}

						value[len++] = *ptr;

						nline++;
					} else {
						if (len >= CONF_VALUE_MAXLEN) {
							/* Value too long. */
							fprintf (stderr, "[%s:%lu] Value too long (> %d).\n", filename, nline, CONF_VALUE_MAXLEN);
							fclose (file);
							return -1;
						}

						value[len++] = *ptr;
					}

					break;
				case 7:
					/* Escape character. */
					if (*ptr == 'r') {
						c = '\r';
					} else if (*ptr == 'n') {
						c = '\n';
					} else if (*ptr == 't') {
						c = '\t';
					} else {
						c = *ptr;
					}

					if (len >= CONF_VALUE_MAXLEN) {
						/* Value too long. */
						fprintf (stderr, "[%s:%lu] Value too long (> %d).\n", filename, nline, CONF_VALUE_MAXLEN);
						fclose (file);
						return -1;
					}

					value[len++] = c;

					state = 6;

					break;
				case 8:
					/* Comment. */
					if (*ptr == '\n') {
						nline++;

						state = 0;
					}

					break;
			}

			ptr++;
		}
	} while (bytes > 0);

	fclose (file);

	return (((state == 0) && (!parent_list))?0:-1);
}

void configuration_print (configuration_t *conf)
{
	print_key_list (conf, &conf->root, 0);
}

void print_key_list (configuration_t *conf, conf_key_list_t *key_list, size_t depth)
{
	size_t i, j;

	for (i = 0; i < key_list->used; i++) {
		/* Print tabs. */
		for (j = 0; j < depth; j++) {
			printf ("\t");
		}

		if (key_list->keys[i].type == KEY_MIGHT_HAVE_CHILDREN) {
			printf ("%s\n", (char *) conf->data + key_list->keys[i].name);

			if ((key_list->keys[i].u.children) && (key_list->keys[i].u.children->used > 0)) {
				for (j = 0; j < depth; j++) {
					printf ("\t");
				}

				printf ("{\n");

				print_key_list (conf, key_list->keys[i].u.children, depth + 1);

				for (j = 0; j < depth; j++) {
					printf ("\t");
				}

				printf ("}\n");
			}
		} else {
			printf ("%s = %s\n", (char *) conf->data + key_list->keys[i].name, (char *) conf->data + key_list->keys[i].u.value);
		}
	}
}

conf_key_t *get_key (configuration_t *conf, va_list ap)
{
	conf_key_list_t *key_list;
	conf_key_t *key;
	char *s;
	unsigned int position;

	key_list = &conf->root;
	key = NULL;

	do {
		s = va_arg (ap, char *);
		if ((!s) || (!*s)) {
			break;
		}

		if (search (conf, key_list, s, &position) < 0) {
			return NULL;
		}

		key = &key_list->keys[position];
		if ((key->type == KEY_HAS_VALUE) || (!key->u.children)) {
			s = va_arg (ap, char *);
			if ((s) && (*s)) {
				return NULL;
			}

			break;
		}

		key_list = key->u.children;
	} while (1);

	return key;
}

const char *configuration_get_value (configuration_t *conf, ...)
{
	va_list ap;
	conf_key_t *key;

	va_start (ap, conf);

	key = get_key (conf, ap);

	va_end (ap);

	if ((!key) || (key->type == KEY_MIGHT_HAVE_CHILDREN)) {
		return NULL;
	}

	return (const char *) (conf->data + key->u.value);
}

const char *configuration_get_child (configuration_t *conf, size_t nchild, ...)
{
	va_list ap;
	conf_key_t *key;

	va_start (ap, nchild);

	key = get_key (conf, ap);

	va_end (ap);

	if ((!key) || (key->type == KEY_HAS_VALUE) || (!key->u.children) || (nchild >= key->u.children->used)) {
		return NULL;
	}

	return (const char *) (conf->data + key->u.children->keys[nchild].name);
}

ssize_t configuration_get_count (configuration_t *conf, ...)
{
	va_list ap;
	conf_key_t *key;

	va_start (ap, conf);

	key = get_key (conf, ap);

	va_end (ap);

	if ((!key) || (key->type == KEY_HAS_VALUE)) {
		return -1;
	}

	if (!key->u.children) {
		return 0;
	}

	return key->u.children->used;
}
