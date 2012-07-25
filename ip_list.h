#ifndef IP_LIST_H
#define IP_LIST_H

#include <stdint.h>
#include "configuration.h"

typedef struct {
	uint32_t ip;
	uint32_t mask;
} ip_t;

typedef struct {
	ip_t *ips;
	size_t size;
	size_t used;
} ip_list_t;

void ip_list_init (ip_list_t *ip_list);
void ip_list_free (ip_list_t *ip_list);

int ip_list_load (ip_list_t *ip_list, configuration_t *conf);

int ip_list_search (ip_list_t *ip_list, uint32_t ip);

#endif /* IP_LIST_H */
