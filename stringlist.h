#ifndef STRINGLIST_H
#define STRINGLIST_H

typedef struct {
	off_t string;
	size_t len;

	int data;
} string_t;

typedef struct {
	char *data;
	size_t data_size;
	size_t data_used;

	string_t *strings;
	size_t size;
	size_t used;
} stringlist_t;

void stringlist_init (stringlist_t *stringlist);
void stringlist_free (stringlist_t *stringlist);

int stringlist_insert_bounded_string (stringlist_t *stringlist, const char *string, size_t len, int data);
int stringlist_insert_string (stringlist_t *stringlist, const char *string, int data);

int stringlist_search_bounded_string (stringlist_t *stringlist, const char *string, size_t len, int *data);
int stringlist_search_string (stringlist_t *stringlist, const char *string, int *data);

#endif /* STRINGLIST_H */
