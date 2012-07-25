#include <stdlib.h>
#include <unistd.h>
#include "session.h"

#define TRANSACTION_ALLOC 2

void session_init (session_t *session)
{
	session->index = -1;

	session->sd = -1;
	session->dnscache = -1;
	buffer_init (&session->domain, 32);

	session->transaction_list.transactions = NULL;
	session->transaction_list.size = 0;
	session->transaction_list.used = 0;

	session->state = 0;
	session->len = 0;
	session->offset = 0;
	session->nlines = 0;
	session->status = 0;
	session->ntransaction = 0;
	session->nforward_path = 0;
	session->naccepted = 0;

	session->last_read_write = 0;
}

void session_free (session_t *session)
{
	/* Close socket descriptor. */
	if (session->sd != -1) {
		close (session->sd);
	}

	buffer_free (&session->domain);
	session_free_transactions (session);
}

void session_reset (session_t *session)
{
	session->index = -1;

	/* Close socket descriptor. */
	if (session->sd != -1) {
		close (session->sd);
		session->sd = -1;
	}

	session->dnscache = -1;

	if (session->domain.size > session->domain.buffer_increment) {
		buffer_free (&session->domain);
	} else {
		buffer_reset (&session->domain);
	}

	session_free_transactions (session);

	session->state = 0;
	session->len = 0;
	session->offset = 0;
	session->nlines = 0;
	session->status = 0;
	session->ntransaction = 0;
	session->nforward_path = 0;
	session->naccepted = 0;

	session->last_read_write = 0;
}

int session_allocate_transactions (session_t *session)
{
	transaction_list_t *transaction_list;
	transaction_t *transactions;
	size_t size;

	transaction_list = &session->transaction_list;
	if (transaction_list->used == transaction_list->size) {
		size = transaction_list->size + TRANSACTION_ALLOC;
		transactions = (transaction_t *) realloc (transaction_list->transactions, size * sizeof (transaction_t));
		if (!transactions) {
			return -1;
		}

		transaction_list->transactions = transactions;
		transaction_list->size = size;
	}

	return 0;
}

void session_free_transactions (session_t *session)
{
	transaction_list_t *transaction_list;
	size_t i;

	transaction_list = &session->transaction_list;
	if (transaction_list->transactions) {
		for (i = 0; i < transaction_list->used; i++) {
			buffer_free (&(transaction_list->transactions[i].reverse_path));
			stringlist_free (&(transaction_list->transactions[i].forward_paths));
		}

		free (transaction_list->transactions);
		transaction_list->transactions = NULL;
	}

	transaction_list->size = 0;
	transaction_list->used = 0;
}
