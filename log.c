#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "log.h"
#include "server.h"

extern server_t server;

static char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void log_mail (struct tm *local_time, connection_t *connection)
{
	buffer_t *buffer;
	mail_transaction_t *mail_transaction;
	domainlist_t *forward_paths;
	domain_t *domain;
	char *data;
	char peer[20];
	size_t peerlen;
	size_t i, j;
	size_t idx;

	if (!inet_ntop (AF_INET, &(connection->sin.sin_addr), peer, sizeof (peer))) {
		return;
	}

	peerlen = strlen (peer);

	buffer = &server.logbuffer;
	mail_transaction = &connection->mail_transaction;

	buffer_reset (buffer);
	if (buffer_format (buffer, "[%.*s] [%s, %d %s %d %02d:%02d:%02d] [%s] [", peerlen, peer, days[local_time->tm_wday], local_time->tm_mday, months[local_time->tm_mon], 1900 + local_time->tm_year, local_time->tm_hour, local_time->tm_min, local_time->tm_sec, mail_transaction->reverse_path) < 0) {
		buffer_free (buffer);
		return;
	}

	forward_paths = &mail_transaction->forward_paths;
	data = forward_paths->data.data;

	idx = 0;

	for (i = 0; i < forward_paths->used; i++) {
		domain = &(forward_paths->records[i]);
		for (j = 0; j < domain->used; j++) {
			if (idx == 0) {
				if (buffer_format (buffer, "%s@%s", data + domain->local_parts[j], data + domain->domain_name) < 0) {
					buffer_free (buffer);
					return;
				}
			} else {
				if (buffer_format (buffer, ", %s@%s", data + domain->local_parts[j], data + domain->domain_name) < 0) {
					buffer_free (buffer);
					return;
				}
			}

			idx++;
		}
	}

	if (buffer_format (buffer, "] [%u]\n", connection->filesize) < 0) {
		buffer_free (buffer);
		return;
	}

	write (server.log_fd, buffer->data, buffer->used);
}
