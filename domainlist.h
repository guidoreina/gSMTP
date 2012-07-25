#ifndef DOMAINLIST_H
#define DOMAINLIST_H

#include "buffer.h"

typedef struct {
	off_t domain_name;

	off_t *local_parts;
	size_t size;
	size_t used;
} domain_t;

typedef struct {
	domain_t *records;
	off_t *index;

	size_t size;
	size_t used;

	buffer_t data;
} domainlist_t;

void domainlist_init (domainlist_t *domainlist);
void domainlist_free (domainlist_t *domainlist);

void domainlist_swap (domainlist_t *domainlist1, domainlist_t *domainlist2);

int domainlist_load (domainlist_t *domainlist, const char *directory);

int domainlist_search_domain (domainlist_t *domainlist, const char *domain);
int domainlist_search (domainlist_t *domainlist, const char *local_part, const char *domain);

const char *domainlist_get_first_domain (domainlist_t *domainlist);

int domainlist_insert_path (domainlist_t *domainlist, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen);

#endif /* DOMAINLIST_H */
