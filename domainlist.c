#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include "domainlist.h"
#include "parser.h"

#define DOMAIN_ALLOC     10
#define LOCAL_PART_ALLOC 100

static int allocate_domains (domainlist_t *domainlist);
static int allocate_local_parts (domain_t *domain);
static int search_domain (domainlist_t *domainlist, const char *domain, unsigned int *position);
static int search_local_part (domainlist_t *domainlist, domain_t *domain, const char *local_part, unsigned int *position);
static domain_t *insert_domain (domainlist_t *domainlist, const char *domain, size_t len);
static int insert_local_part (domainlist_t *domainlist, domain_t *domain, const char *local_part, size_t len);

void domainlist_init (domainlist_t *domainlist)
{
	domainlist->records = NULL;
	domainlist->index = NULL;

	domainlist->size = 0;
	domainlist->used = 0;

	buffer_init (&domainlist->data, 4 * 1024);
}

void domainlist_free (domainlist_t *domainlist)
{
	domain_t *record;
	size_t i;

	if (domainlist->records) {
		for (i = 0; i < domainlist->used; i++) {
			record = &(domainlist->records[i]);
			if (record->local_parts) {
				free (record->local_parts);
			}
		}

		free (domainlist->records);
		domainlist->records = NULL;
	}

	if (domainlist->index) {
		free (domainlist->index);
		domainlist->index = NULL;
	}

	domainlist->size = 0;
	domainlist->used = 0;

	buffer_free (&domainlist->data);
}

void domainlist_swap (domainlist_t *domainlist1, domainlist_t *domainlist2)
{
	domainlist_t tmp;

	memcpy (&tmp, domainlist1, sizeof (domainlist_t));
	memcpy (domainlist1, domainlist2, sizeof (domainlist_t));
	memcpy (domainlist2, &tmp, sizeof (domainlist_t));
}

int allocate_domains (domainlist_t *domainlist)
{
	domain_t *records;
	off_t *index;
	size_t size;

	if (domainlist->used == domainlist->size) {
		size = domainlist->size + DOMAIN_ALLOC;
		index = (off_t *) malloc (size * sizeof (off_t));
		if (!index) {
			return -1;
		}

		records = (domain_t *) realloc (domainlist->records, size * sizeof (domain_t));
		if (!records) {
			free (index);
			return -1;
		}

		if (domainlist->index) {
			memcpy (index, domainlist->index, domainlist->used * sizeof (off_t));
			free (domainlist->index);
		}

		domainlist->records = records;
		domainlist->index = index;
		domainlist->size = size;
	}

	return 0;
}

int allocate_local_parts (domain_t *domain)
{
	off_t *local_parts;
	size_t size;

	if (domain->used == domain->size) {
		size = domain->size + LOCAL_PART_ALLOC;
		local_parts = (off_t *) realloc (domain->local_parts, size * sizeof (off_t));
		if (!local_parts) {
			return -1;
		}

		domain->local_parts = local_parts;
		domain->size = size;
	}

	return 0;
}

int search_domain (domainlist_t *domainlist, const char *domain, unsigned int *position)
{
	domain_t *record;
	int i, j, pivot;
	int ret;

	i = 0;
	j = domainlist->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;
		record = &(domainlist->records[domainlist->index[pivot]]);

		ret = strcasecmp (domain, domainlist->data.data + record->domain_name);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			*position = (unsigned int) pivot;
			return 0;
		} else {
			i = pivot + 1;
		}
	}

	*position = (unsigned int) i;

	return -1;
}

int search_local_part (domainlist_t *domainlist, domain_t *domain, const char *local_part, unsigned int *position)
{
	int i, j, pivot;
	int ret;

	i = 0;
	j = domain->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;

		ret = strcasecmp (local_part, domainlist->data.data + domain->local_parts[pivot]);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			*position = (unsigned int) pivot;
			return 0;
		} else {
			i = pivot + 1;
		}
	}

	*position = (unsigned int) i;

	return -1;
}

domain_t *insert_domain (domainlist_t *domainlist, const char *domain, size_t len)
{
	buffer_t *data;
	domain_t *record;
	unsigned int position;
	off_t offset;

	if (search_domain (domainlist, domain, &position) == 0) {
		return &(domainlist->records[domainlist->index[position]]);
	}

	if (allocate_domains (domainlist) < 0) {
		return NULL;
	}

	record = &(domainlist->records[domainlist->used]);

	data = &domainlist->data;

	offset = data->used;

	if (buffer_allocate (data, len + 1) < 0) {
		return NULL;
	}

	memcpy (data->data + offset, domain, len);
	data->data[offset + len] = 0;
	data->used += (len + 1);

	record->domain_name = offset;

	record->local_parts = NULL;
	record->size = 0;
	record->used = 0;

	if (position < domainlist->used) {
		memmove (&(domainlist->index[position + 1]), &(domainlist->index[position]), (domainlist->used - position) * sizeof (off_t));
	}

	domainlist->index[position] = domainlist->used++;

	return record;
}

int insert_local_part (domainlist_t *domainlist, domain_t *domain, const char *local_part, size_t len)
{
	buffer_t *data;
	unsigned int position;
	off_t offset;

	if (search_local_part (domainlist, domain, local_part, &position) == 0) {
		return 0;
	}

	if (allocate_local_parts (domain) < 0) {
		return -1;
	}

	data = &domainlist->data;

	offset = data->used;

	if (buffer_allocate (data, len + 1) < 0) {
		return -1;
	}

	memcpy (data->data + offset, local_part, len);
	data->data[offset + len] = 0;
	data->used += (len + 1);

	if (position < domain->used) {
		memmove (&(domain->local_parts[position + 1]), &(domain->local_parts[position]), (domain->used - position) * sizeof (off_t));
	}

	domain->local_parts[position] = offset;

	domain->used++;

	return 0;
}

int domainlist_load (domainlist_t *domainlist, const char *directory)
{
	DIR *domains;
	DIR *local_parts;
	struct dirent *domain;
	struct dirent *local_part;
	domain_t *record;
	struct stat buf;
	char path[PATH_MAX + 1];
	size_t nmailboxes;
	size_t dirlen;
	size_t domainlen;
	size_t len;

	domainlist_free (domainlist);

	domains = opendir (directory);
	if (!domains) {
		fprintf (stderr, "Couldn't open domains directory %s.\n", directory);
		return -1;
	}

	nmailboxes = 0;

	dirlen = strlen (directory);

	/* For each domain... */
	while ((domain = readdir (domains)) != NULL) {
		if (domain->d_name[0] == '.') {
			continue;
		}

		/* If it is not a directory... */
		domainlen = snprintf (path, sizeof (path), "%s/%s", directory, domain->d_name);
		if ((stat (path, &buf) < 0) || (!S_ISDIR (buf.st_mode))) {
			continue;
		}

		domainlen -= (dirlen + 1);

		/* Valid domain name? */
		if (valid_domain ((const unsigned char *) domain->d_name) < 0) {
			fprintf (stderr, "%s is not a valid domain name.\n", domain->d_name);
			continue;
		}

		/* Insert domain. */
		if ((record = insert_domain (domainlist, domain->d_name, domainlen)) == NULL) {
			fprintf (stderr, "Couldn't allocate memory for inserting domain %s.\n", domain->d_name);

			closedir (domains);
			return -1;
		}

		local_parts = opendir (path);
		if (!local_parts) {
			fprintf (stderr, "Couldn't open domain directory %s.\n", domain->d_name);
			continue;
		}

		/* For each local part... */
		while ((local_part = readdir (local_parts)) != NULL) {
			if (local_part->d_name[0] == '.') {
				if (local_part->d_name[1] == 0) {
					/* Current directory. */
					continue;
				}

				if (local_part->d_name[1] == '.') {
					if (local_part->d_name[2] == 0) {
						/* Parent directory. */
						continue;
					}
				}

				/* Local part cannot start with '.'. */
				continue;
			}

			/* If it is not a directory... */
			len = snprintf (path, sizeof (path), "%s/%s/%s", directory, domain->d_name, local_part->d_name);
			if ((stat (path, &buf) < 0) || (!S_ISDIR (buf.st_mode))) {
				continue;
			}

			len -= (dirlen + domainlen + 2);

			/* Valid local part? */
			if (valid_local_part ((const unsigned char *) local_part->d_name) < 0) {
				fprintf (stderr, "%s is not a valid local part.\n", local_part->d_name);
				continue;
			}

			/* Insert local part. */
			if (insert_local_part (domainlist, record, local_part->d_name, len) < 0) {
				fprintf (stderr, "Couldn't allocate memory for inserting local part %s of domain %s.\n", local_part->d_name, domain->d_name);

				closedir (local_parts);
				closedir (domains);
				return -1;
			}

#if DEBUG
			printf ("Inserted %s@%s.\n", local_part->d_name, domain->d_name);
#endif /* DEBUG */

			nmailboxes++;
		}

		closedir (local_parts);
	}

	closedir (domains);

	if (nmailboxes == 0) {
		fprintf (stderr, "No mailboxes.\n");
		return -1;
	}

	return 0;
}

int domainlist_search_domain (domainlist_t *domainlist, const char *domain)
{
	unsigned int position;

	if (search_domain (domainlist, domain, &position) < 0) {
		return -1;
	}

	return 0;
}

int domainlist_search (domainlist_t *domainlist, const char *local_part, const char *domain)
{
	domain_t *record;
	unsigned int position;

	if (search_domain (domainlist, domain, &position) < 0) {
		return -2;
	}

	record = &(domainlist->records[domainlist->index[position]]);

	return search_local_part (domainlist, record, local_part, &position);
}

const char *domainlist_get_first_domain (domainlist_t *domainlist)
{
	if (domainlist->used == 0) {
		return NULL;
	}

	return (domainlist->data.data + domainlist->records[domainlist->index[0]].domain_name);
}

int domainlist_insert_path (domainlist_t *domainlist, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen)
{
	domain_t *record;

	/* Insert domain. */
	if ((record = insert_domain (domainlist, domain, domainlen)) == NULL) {
		fprintf (stderr, "Couldn't allocate memory for inserting domain %s.\n", domain);
		return -1;
	}

	/* Insert local part. */
	if (insert_local_part (domainlist, record, local_part, local_part_len) < 0) {
		fprintf (stderr, "Couldn't allocate memory for inserting local part %s of domain %s.\n", local_part, domain);
		return -1;
	}

	return 0;
}
