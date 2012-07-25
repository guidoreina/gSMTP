#ifndef DNSCACHE_H
#define DNSCACHE_H

#include "dns.h"

#define MAX_DNS_ENTRIES 10000

typedef struct {
	buffer_t name;
	int type;

	time_t timestamp;

	rr_t rr_list[MAX_HOSTS];
	size_t nhosts;

	eDnsStatus status;
} dnscache_entry_t;

typedef struct {
	dnscache_entry_t *records;
	off_t *index;

	size_t size;
	size_t used;
} dnscache_t;

void dnscache_init (dnscache_t *dnscache);
void dnscache_free (dnscache_t *dnscache);
void dnscache_reset (dnscache_t *dnscache);

eDnsStatus dnscache_lookup (dnscache_t *dnscache, const char *dname, int type, size_t max_hosts, off_t *index, time_t current_time);

#endif /* DNSCACHE_H */
