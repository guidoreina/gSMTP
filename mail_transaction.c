#include <sys/types.h>
#include <string.h>
#include "mail_transaction.h"

void mail_transaction_init (mail_transaction_t *mail_transaction)
{
	mail_transaction->reverse_path[0] = 0;
	domainlist_init (&mail_transaction->forward_paths);
}

void mail_transaction_free (mail_transaction_t *mail_transaction)
{
	mail_transaction->reverse_path[0] = 0;
	domainlist_free (&mail_transaction->forward_paths);
}

void mail_transaction_set_reverse_path (mail_transaction_t *mail_transaction, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen)
{
	memcpy (mail_transaction->reverse_path, local_part, local_part_len);
	mail_transaction->reverse_path[local_part_len] = '@';
	memcpy (mail_transaction->reverse_path + local_part_len + 1, domain, domainlen);
	mail_transaction->reverse_path[local_part_len + 1 + domainlen] = 0;
}

int mail_transaction_add_forward_path (mail_transaction_t *mail_transaction, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen)
{
	return domainlist_insert_path (&mail_transaction->forward_paths, local_part, local_part_len, domain, domainlen);
}
