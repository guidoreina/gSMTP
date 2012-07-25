#ifndef RELAY_H
#define RELAY_H

#include "session.h"
#include "input_stream.h"
#include "dnscache.h"

typedef struct {
	int epoll_fd; /* epoll file descriptor. */

	int max_file_descriptors;
	unsigned nfds; /* # of file descriptors. */

	int *index;
	struct epoll_event *events;

	session_t *sessions;

	session_t **interrupted_sessions;
	size_t number_interrupted_sessions;

	input_stream_t input_stream;
} relay_t;

void relay_loop (void);

int connect_to_smtp_server (dnscache_entry_t *dnscache_entry);

#endif /* RELAY_H */
