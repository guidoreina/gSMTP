#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dnscache.h"

#define DNS_CACHE_ALLOC    1000
#define QUERY_MIN_INTERVAL 60

static int search (dnscache_t *dnscache, const char *dname, int type, unsigned int *position);
static int allocate (dnscache_t *dnscache);

void dnscache_init (dnscache_t *dnscache)
{
	dnscache->records = NULL;
	dnscache->index = NULL;

	dnscache->size = 0;
	dnscache->used = 0;
}

void dnscache_free (dnscache_t *dnscache)
{
	size_t i;

	if (dnscache->records) {
		for (i = 0; i < dnscache->used; i++) {
			buffer_free (&(dnscache->records[i].name));
			rr_list_free (dnscache->records[i].rr_list, MAX_HOSTS);
		}

		free (dnscache->records);
		dnscache->records = NULL;
	}

	if (dnscache->index) {
		free (dnscache->index);
		dnscache->index = NULL;
	}

	dnscache->size = 0;
	dnscache->used = 0;
}

void dnscache_reset (dnscache_t *dnscache)
{
	size_t i;

	if (dnscache->records) {
		for (i = 0; i < dnscache->used; i++) {
			buffer_free (&(dnscache->records[i].name));

			rr_list_free (dnscache->records[i].rr_list, MAX_HOSTS);
			dnscache->records[i].nhosts = 0;
		}
	}

	dnscache->used = 0;
}

int search (dnscache_t *dnscache, const char *dname, int type, unsigned int *position)
{
	dnscache_entry_t *record;
	int i, j, pivot;
	int ret;

	i = 0;
	j = dnscache->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;
		record = &(dnscache->records[dnscache->index[pivot]]);

		ret = strcasecmp (dname, record->name.data);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			if (type < record->type) {
				j = pivot - 1;
			} else if (type == record->type) {
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

int allocate (dnscache_t *dnscache)
{
	dnscache_entry_t *records;
	off_t *index;
	size_t size;

	/* If the cache is already too large... */
	if (dnscache->used >= MAX_DNS_ENTRIES) {
		/* Empty cache. */
		dnscache_reset (dnscache);
	}

	if (dnscache->used == dnscache->size) {
		size = dnscache->size + DNS_CACHE_ALLOC;
		index = (off_t *) malloc (size * sizeof (off_t));
		if (!index) {
			return -1;
		}

		records = (dnscache_entry_t *) realloc (dnscache->records, size * sizeof (dnscache_entry_t));
		if (!records) {
			free (index);
			return -1;
		}

		if (dnscache->index) {
			memcpy (index, dnscache->index, dnscache->used * sizeof (off_t));
			free (dnscache->index);
		}

		dnscache->records = records;
		dnscache->index = index;
		dnscache->size = size;
	}

	return 0;
}

eDnsStatus dnscache_lookup (dnscache_t *dnscache, const char *dname, int type, size_t max_hosts, off_t *index, time_t current_time)
{
	dnscache_entry_t *record;
	rr_t *rr_list;
	buffer_t name;
	unsigned int position;
	int min_ttl;
	int lookup;
	size_t len;
	size_t i;

	if (max_hosts > MAX_HOSTS) {
		return DNS_ERROR;
	}

	if (search (dnscache, dname, type, &position) < 0) {
		/* DNS cache miss. */
		buffer_init (&name, 64);
		len = strlen (dname);
		if (buffer_allocate (&name, len + 1) < 0) {
			buffer_free (&name);
			return DNS_ERROR;
		}

		memcpy (name.data, dname, len + 1);
		name.used = len + 1;

		if (allocate (dnscache) < 0) {
			buffer_free (&name);
			return DNS_ERROR;
		}

		/* We might have emptied the cache. */
		if (dnscache->used == 0) {
			position = 0;
		}

		record = &(dnscache->records[dnscache->used]);

		memcpy (&record->name, &name, sizeof (buffer_t));
		record->type = type;
		record->timestamp = current_time;
		rr_list_init (record->rr_list, MAX_HOSTS);
		record->nhosts = 0;

		record->status = dns_lookup (dname, type, max_hosts, record->rr_list, &record->nhosts);

		*index = dnscache->used;

		if (position < dnscache->used) {
			memmove (&(dnscache->index[position + 1]), &(dnscache->index[position]), (dnscache->used - position) * sizeof (off_t));
		}

		dnscache->index[position] = dnscache->used++;

		return record->status;
	}

	*index = dnscache->index[position];
	record = &(dnscache->records[*index]);
	rr_list = record->rr_list;

	if ((record->status == DNS_HOST_NOT_FOUND) || (record->status == DNS_NO_DATA)) {
		if (record->timestamp + QUERY_MIN_INTERVAL >= current_time) {
			return record->status;
		}

		lookup = 1;
	} else if (record->status != DNS_SUCCESS) {
		lookup = 1;
	} else {
		/* Compute the minimum TTL. */
		min_ttl = rr_list[0].ttl;
		for (i = 1; i < record->nhosts; i++) {
			if (rr_list[i].ttl < min_ttl) {
				min_ttl = rr_list[i].ttl;
			}
		}

		/* If the TTL has already expired... */
		if (record->timestamp + min_ttl < current_time) {
			lookup = 1;
		} else {
			lookup = 0;
		}
	}

	if (lookup) {
		rr_list_free (rr_list, MAX_HOSTS);

		record->status = dns_lookup (dname, type, max_hosts, rr_list, &record->nhosts);
		record->timestamp = current_time;
	}

	return record->status;
}
