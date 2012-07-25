#ifndef MAIL_TRANSACTION_H
#define MAIL_TRANSACTION_H

#include "domainlist.h"
#include "constants.h"

typedef struct {
	char reverse_path[PATH_MAXLEN + 1];
	domainlist_t forward_paths;
} mail_transaction_t;

void mail_transaction_init (mail_transaction_t *mail_transaction);
void mail_transaction_free (mail_transaction_t *mail_transaction);

void mail_transaction_set_reverse_path (mail_transaction_t *mail_transaction, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen);
int mail_transaction_add_forward_path (mail_transaction_t *mail_transaction, const char *local_part, size_t local_part_len, const char *domain, size_t domainlen);

#endif /* MAIL_TRANSACTION_H */
