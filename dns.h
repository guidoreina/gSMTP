#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <netinet/in.h>
#include "buffer.h"

#define MAX_HOSTS 5

typedef enum {DNS_ERROR = -1, DNS_SUCCESS, DNS_HOST_NOT_FOUND, DNS_TRY_AGAIN, DNS_NO_RECOVERY, DNS_NO_DATA} eDnsStatus;

typedef struct {
	uint16_t type;
	uint16_t class;
	uint32_t ttl;
	uint16_t preference;

	union {
		in_addr_t addr;
		buffer_t name;
	} u;
} rr_t;

void rr_list_init (rr_t *rr_list, size_t nhosts);
void rr_list_free (rr_t *rr_list, size_t nhosts);

eDnsStatus dns_lookup (const char *dname, int type, size_t max_hosts, rr_t *rr_list, size_t *nhosts);

#endif /* DNS_H */
