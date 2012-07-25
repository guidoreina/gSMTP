#ifndef CONNECTION_H
#define CONNECTION_H

#include <time.h>
#include <arpa/inet.h>
#include "input_stream.h"
#include "mail_transaction.h"
#include "buffer.h"

typedef enum {
	INITIAL_STATE,
	READING_COMMAND_STATE,
	WRITING_RESPONSE_STATE,
	DATA_STATE,
	BDAT_STATE,
	DISCARDING_COMMAND_LINE,
	DISCARDING_DATA,
	DISCARDING_BDAT
} eSmtpConnectionState;

typedef struct {
	int index; /* Position of connection in index array. */

	int sd; /* Socket descriptor. */
	int fd; /* File descriptor. */

	struct sockaddr_in sin;

	input_stream_t input_stream;
	char input[TEXT_LINE_MAXLEN + 1];

	buffer_t output;

	eSmtpConnectionState state;
	eSmtpConnectionState next_state;

	off_t offset;

	buffer_t domain;
	mail_transaction_t mail_transaction;
	size_t ntransactions;

	time_t last_read_write; /* Time at which the client got connected or last time we got data from him or sent data to him. */

	time_t file_timestamp;
	unsigned nfile;

	size_t filesize;

	size_t chunk_size;
	char last;

	char quit;
} connection_t;

int connection_init (connection_t *connection, size_t buffer_size);
void connection_free (connection_t *connection);

void connection_reset (connection_t *connection);

#endif /* CONNECTION_H */
