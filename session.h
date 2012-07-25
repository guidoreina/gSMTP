#ifndef SESSION_H
#define SESSION_H

#include "buffer.h"
#include "stringlist.h"
#include "constants.h"

typedef struct {
	int fd;

	buffer_t reverse_path;
	stringlist_t forward_paths;

	off_t message_offset;
	off_t offset;
	off_t filesize;
} transaction_t;

typedef struct {
	transaction_t *transactions;
	size_t size;
	size_t used;
} transaction_list_t;

typedef struct {
	int index; /* Position of session in index array. */

	int sd;
	off_t dnscache;
	buffer_t domain;

	transaction_list_t transaction_list;

	int state;
	char input[TEXT_LINE_MAXLEN + 1];
	size_t len;
	off_t offset;
	size_t nlines;
	int status;
	size_t ntransaction;
	size_t nforward_path;
	size_t naccepted;

	time_t last_read_write;
} session_t;

void session_init (session_t *session);
void session_free (session_t *session);

void session_reset (session_t *session);

int session_allocate_transactions (session_t *session);
void session_free_transactions (session_t *session);

#endif /* SESSION_H */
