#include <stdlib.h>
#include <string.h>
#include "stringlist.h"

#define DATA_ALLOC  (4 * 1024)
#define INDEX_ALLOC (100)

static int search (stringlist_t *stringlist, const char *string, size_t len, unsigned int *position);
static int allocate (stringlist_t *stringlist, size_t len);

void stringlist_init (stringlist_t *stringlist)
{
	stringlist->data = NULL;
	stringlist->data_size = 0;
	stringlist->data_used = 0;

	stringlist->strings = NULL;
	stringlist->size = 0;
	stringlist->used = 0;
}

void stringlist_free (stringlist_t *stringlist)
{
	if (stringlist->data) {
		free (stringlist->data);
		stringlist->data = NULL;
	}

	stringlist->data_size = 0;
	stringlist->data_used = 0;

	if (stringlist->strings) {
		free (stringlist->strings);
		stringlist->strings = NULL;
	}

	stringlist->size = 0;
	stringlist->used = 0;
}

int search (stringlist_t *stringlist, const char *string, size_t len, unsigned int *position)
{
	int i, j, pivot;
	int ret;

	i = 0;
	j = stringlist->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;
		ret = strncmp (string, stringlist->data + stringlist->strings[pivot].string, len);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			if (len < stringlist->strings[pivot].len) {
				j = pivot - 1;
			} else if (len == stringlist->strings[pivot].len) {
				*position = (unsigned int) pivot;
				return 0;
			} else {
				i = pivot + 1;
			}
		} else {
			i = pivot + 1;
		}
	}

	*position = (unsigned int) i;

	return -1;
}

int allocate (stringlist_t *stringlist, size_t len)
{
	string_t *strings;
	char *data;
	size_t size;
	size_t quotient, remainder;

	if (stringlist->data_used + len > stringlist->data_size) {
		size = stringlist->data_size + len;

		quotient = size / DATA_ALLOC;
		remainder = size % DATA_ALLOC;
		if (remainder != 0) {
			quotient++;
		}

		size = quotient * DATA_ALLOC;
		data = (char *) realloc (stringlist->data, size);
		if (!data) {
			return -1;
		}

		stringlist->data = data;
		stringlist->data_size = size;
	}

	if (stringlist->used == stringlist->size) {
		size = stringlist->size + INDEX_ALLOC;
		strings = (string_t *) realloc (stringlist->strings, size * sizeof (string_t));
		if (!strings) {
			return -1;
		}

		stringlist->strings = strings;
		stringlist->size = size;
	}

	return 0;
}

int stringlist_insert_bounded_string (stringlist_t *stringlist, const char *string, size_t len, int data)
{
	unsigned int position;

	if (search (stringlist, string, len, &position) == 0) {
		/* Found. Update data. */
		stringlist->strings[position].data = data;
		return 0;
	}

	if (allocate (stringlist, len) < 0) {
		return -1;
	}

	if (position < stringlist->used) {
		memmove (&(stringlist->strings[position + 1]), &(stringlist->strings[position]), (stringlist->used - position) * sizeof (string_t));
	}

	stringlist->strings[position].string = stringlist->data_used;
	memcpy (stringlist->data + stringlist->data_used, string, len);
	stringlist->data_used += len;

	stringlist->strings[position].len = len;
	stringlist->strings[position].data = data;

	stringlist->used++;

	return 0;
}

int stringlist_insert_string (stringlist_t *stringlist, const char *string, int data)
{
	return stringlist_insert_bounded_string (stringlist, string, strlen (string), data);
}

int stringlist_search_bounded_string (stringlist_t *stringlist, const char *string, size_t len, int *data)
{
	unsigned int position;

	if (search (stringlist, string, len, &position) < 0) {
		/* Not found. */
		return -1;
	}

	if (data) {
		*data = stringlist->strings[position].data;
	}

	return position;
}

int stringlist_search_string (stringlist_t *stringlist, const char *string, int *data)
{
	return stringlist_search_bounded_string (stringlist, string, strlen (string), data);
}
