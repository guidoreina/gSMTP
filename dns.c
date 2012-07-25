#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include "dns.h"
#include "parser.h"

#ifndef MAXPACKET
#define MAXPACKET 8192
#endif /* MAXPACKET */

typedef union {
	HEADER header;
	u_char buf[MAXPACKET];
} response_t;

static int insert_rr (unsigned char *msg, unsigned char *end, unsigned char *cp, rr_t *rr_list, size_t nhosts);

void rr_list_init (rr_t *rr_list, size_t nhosts)
{
	size_t i;

	for (i = 0; i < nhosts; i++) {
		rr_list[i].type = T_A;
	}
}

void rr_list_free (rr_t *rr_list, size_t nhosts)
{
	size_t i;

	for (i = 0; i < nhosts; i++) {
		if (rr_list[i].type != T_A) {
			buffer_free (&(rr_list[i].u.name));
		}
	}
}

eDnsStatus dns_lookup (const char *dname, int type, size_t max_hosts, rr_t *rr_list, size_t *nhosts)
{
	response_t response;
	HEADER *header;
	unsigned char *end;
	unsigned char *cp;
	const char *src;
	char *dest;
	char addr[20];
	ssize_t len;
	int qdcount, ancount;
	int i;
	int bytes;

	/* If the domain name is an IP address... */
	if (*dname == '[') {
		if (valid_domain ((const unsigned char *) dname) < 0) {
			return DNS_ERROR;
		}

		for (src = dname + 1, dest = addr; *src != ']';) {
			*dest++ = *src++;
		}

		*dest = 0;

		rr_list[0].type = T_A;
		rr_list[0].class = C_IN;
		rr_list[0].ttl = 0;
		rr_list[0].preference = 0;
		rr_list[0].u.addr = inet_addr (addr);

		*nhosts = 1;

		return DNS_SUCCESS;
	}

	/* If res_init hasn't been called... */
	if ((_res.options & RES_INIT) == 0) {
		if (res_init () < 0) {
			return DNS_ERROR;
		}
	}

	if ((len = res_search (dname, C_IN, type, (unsigned char *) &response, sizeof (response))) < 0) {
		switch (h_errno) {
			case HOST_NOT_FOUND:
				return DNS_HOST_NOT_FOUND;
			case TRY_AGAIN:
				return DNS_TRY_AGAIN;
			case NO_RECOVERY:
				return DNS_NO_RECOVERY;
			case NO_DATA:
				return DNS_NO_DATA;
			default:
				return DNS_ERROR;
		}
	}

	if (len < HFIXEDSZ) {
		return DNS_ERROR;
	}

	if (len > sizeof (response)) {
		len = sizeof (response);
	}

	/* Compute the end of message. */
	end = (unsigned char *) &response + len;

	/* Get a pointer to the header. */
	header = (HEADER *) &response;

	/* Skip header. */
	cp = (unsigned char *) &response + HFIXEDSZ;

	/* First comes the question section, skip it. */
	qdcount = ntohs (header->qdcount);
	for (i = 0; (cp < end) && (i < qdcount); i++) {
		if ((bytes = dn_skipname (cp, end)) < 0) {
			return DNS_ERROR;
		}

		cp += (bytes + QFIXEDSZ);
	}

	*nhosts = 0;

	/* Then comes the answer section. */
	ancount = ntohs (header->ancount);
	for (i = 0; (cp < end) && (i < ancount) && (*nhosts < max_hosts); i++) {
		if ((bytes = insert_rr ((unsigned char *) &response, end, cp, rr_list, *nhosts)) < 0) {
			break;
		}

		*nhosts = *nhosts + 1;

		cp += bytes;
	}

	if (!*nhosts) {
		return DNS_ERROR;
	}

	return DNS_SUCCESS;
}

int insert_rr (unsigned char *msg, unsigned char *end, unsigned char *cp, rr_t *rr_list, size_t nhosts)
{
	buffer_t name;
	uint16_t type;
	uint32_t ttl;
	uint16_t rdlength;
	uint16_t preference;
	in_addr_t addr;
	char host[MAXDNAME + 1];
	size_t len;
	int bytes;
	size_t size;
	size_t i;

	if ((bytes = dn_skipname (cp, end)) < 0) {
		return -1;
	}

	cp += bytes;

	/* Just in case the response is corrupt... */
	if (cp + RRFIXEDSZ > end) {
		return -1;
	}

	/* Get type. */
	GETSHORT (type, cp);

	/* Skip class. */
	cp += INT16SZ;

	/* Get TTL. */
	GETLONG (ttl, cp);

	/* Get rdlength. */
	GETSHORT (rdlength, cp);

	if (cp + rdlength > end) {
		return -1;
	}

	size = bytes + RRFIXEDSZ;

	switch (type) {
		case T_A:
			if (rdlength != sizeof (in_addr_t)) {
				return -1;
			}

			memcpy (&addr, cp, sizeof (in_addr_t));
			if (addr == INADDR_NONE) {
				return -1;
			}

			preference = 0;

			break;
		case T_CNAME:
		case T_MB:
		case T_MG:
		case T_MR:
		case T_NS:
		case T_PTR:
			if ((bytes = dn_expand (msg, end, cp, host, sizeof (host))) < 0) {
				return -1;
			}

			if (valid_domain ((const unsigned char *) host) < 0) {
				return -1;
			}

			buffer_init (&name, 64);
			len = strlen (host);
			if (buffer_allocate (&name, len + 1) < 0) {
				buffer_free (&name);
				return -1;
			}

			memcpy (name.data, host, len + 1);
			name.used = len + 1;

			preference = 0;

			break;
		case T_MX:
			/* Get preference. */
			GETSHORT (preference, cp);

			if ((bytes = dn_expand (msg, end, cp, host, sizeof (host))) < 0) {
				return -1;
			}

			if (valid_domain ((const unsigned char *) host) < 0) {
				return -1;
			}

			buffer_init (&name, 64);
			len = strlen (host);
			if (buffer_allocate (&name, len + 1) < 0) {
				buffer_free (&name);
				return -1;
			}

			memcpy (name.data, host, len + 1);
			name.used = len + 1;

			break;
		default:
			return -1;
	}

	/* Insert rr in rr list. */
	for (i = 0; i < nhosts; i++) {
		if (preference < rr_list[i].preference) {
			break;
		}
	}

	if (i != nhosts) {
		memmove (&(rr_list[i + 1]), &(rr_list[i]), (nhosts - i) * sizeof (rr_t));
	}

	rr_list[i].type = type;
	rr_list[i].class = C_IN;
	rr_list[i].ttl = ttl;
	rr_list[i].preference = preference;

	if (type == T_A) {
		rr_list[i].u.addr = addr;
	} else {
		memcpy (&(rr_list[i].u.name), &name, sizeof (buffer_t));
	}

	return size + rdlength;
}
