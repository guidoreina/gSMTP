#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ip_list.h"

#define IP_ALLOC 10

static int allocate (ip_list_t *ip_list);
static uint32_t compute_mask (uint8_t n);
static int search (ip_list_t *ip_list, uint32_t ip, uint32_t mask, unsigned int *position);
static int insert (ip_list_t *ip_list, uint32_t ip, uint32_t mask);

void ip_list_init (ip_list_t *ip_list)
{
	ip_list->ips = NULL;
	ip_list->size = 0;
	ip_list->used = 0;
}

void ip_list_free (ip_list_t *ip_list)
{
	if (ip_list->ips) {
		free (ip_list->ips);
		ip_list->ips = NULL;
	}

	ip_list->size = 0;
	ip_list->used = 0;
}

int allocate (ip_list_t *ip_list)
{
	ip_t *ips;
	size_t size;

	if (ip_list->used == ip_list->size) {
		size = ip_list->size + IP_ALLOC;
		ips = (ip_t *) realloc (ip_list->ips, size * sizeof (ip_t));
		if (!ips) {
			return -1;
		}

		ip_list->ips = ips;
		ip_list->size = size;
	}

	return 0;
}

uint32_t compute_mask (uint8_t n)
{
	uint32_t mask;
	uint8_t i;

	mask = 0;
	for (i = 0; i < n; i++) {
		mask |= (0x80000000 >> i);
	}

	return mask;
}

int search (ip_list_t *ip_list, uint32_t ip, uint32_t mask, unsigned int *position)
{
	int i, j, pivot;

	i = 0;
	j = ip_list->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;

		if ((ip & mask) < (ip_list->ips[pivot].ip & ip_list->ips[pivot].mask)) {
			j = pivot - 1;
		} else if ((ip & mask) == (ip_list->ips[pivot].ip & ip_list->ips[pivot].mask)) {
			*position = (unsigned int) pivot;
			return 0;
		} else {
			i = pivot + 1;
		}
	}

	*position = (unsigned int) i;

	return -1;
}

int insert (ip_list_t *ip_list, uint32_t ip, uint32_t mask)
{
	unsigned int position;

	if (search (ip_list, ip, mask, &position) == 0) {
		return 0;
	}

	if (allocate (ip_list) < 0) {
		return -1;
	}

	if (position < ip_list->used) {
		memmove (&(ip_list->ips[position + 1]), &(ip_list->ips[position]), (ip_list->used - position) * sizeof (ip_t));
	}

	ip_list->ips[position].ip = ip;
	ip_list->ips[position].mask = mask;

	ip_list->used++;

	return 0;
}

int ip_list_load (ip_list_t *ip_list, configuration_t *conf)
{
	struct in_addr addr;
	const char *string;
	const char *ptr;
	char ip[16];
	int n;
	size_t len;
	size_t i;

	ip_list_free (ip_list);

	for (i = 0; ((string = configuration_get_child (conf, i, "General", "IPsForRelay", NULL)) != NULL); i++) {
		ptr = strchr (string, '/');
		if (ptr) {
			len = ptr - string;

			n = atoi (ptr + 1);
			if ((n < 0) || (n > 32)) {
				fprintf (stderr, "Ignoring IP: [%s].\n", string);
				continue;
			}
		} else {
			len = strlen (string);

			n = 32;
		}

		if ((len < 7) || (len > 15)) {
			fprintf (stderr, "Ignoring IP: [%s].\n", string);
			continue;
		}

		memcpy (ip, string, len);
		ip[len] = 0;
		if (inet_aton (ip, &addr) < 0) {
			fprintf (stderr, "Ignoring IP: [%s].\n", string);
			continue;
		}

		if (insert (ip_list, ntohl (addr.s_addr), compute_mask (n)) < 0) {
			/* Couldn't allocate memory. */
			fprintf (stderr, "[ip_list_load] Couldn't allocate memory.\n");
			return -1;
		}
	}

	return 0;
}

int ip_list_search (ip_list_t *ip_list, uint32_t ip)
{
	int i, j, pivot;

	i = 0;
	j = ip_list->used - 1;

	while (i <= j) {
		pivot = (i + j) / 2;

		if ((ip & ip_list->ips[pivot].mask) < (ip_list->ips[pivot].ip & ip_list->ips[pivot].mask)) {
			j = pivot - 1;
		} else if ((ip & ip_list->ips[pivot].mask) == (ip_list->ips[pivot].ip & ip_list->ips[pivot].mask)) {
			return 0;
		} else {
			i = pivot + 1;
		}
	}

	return -1;
}
