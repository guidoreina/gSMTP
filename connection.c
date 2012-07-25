#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "connection.h"
#include "server.h"

extern server_t server;

int connection_init (connection_t *connection, size_t buffer_size)
{
	input_stream_t *input_stream;

	connection->index = -1;

	connection->sd = -1;
	connection->fd = -1;

	input_stream = &connection->input_stream;

	input_stream->fd = -1;

	input_stream->buf_base = (unsigned char *) malloc (buffer_size);
	if (!input_stream->buf_base) {
		return -1;
	}

	input_stream->buf_end = input_stream->buf_base + buffer_size;
	input_stream->size = buffer_size;

	input_stream->read_ptr = input_stream->buf_base;
	input_stream->read_end = input_stream->buf_base;

	input_stream->end_of_file = 0;
	input_stream->error = 0;

	buffer_init (&connection->output, 1024);

	connection->state = INITIAL_STATE;
	connection->next_state = INITIAL_STATE;

	connection->offset = 0;

	buffer_init (&connection->domain, 64);
	mail_transaction_init (&connection->mail_transaction);
	connection->ntransactions = 0;

	connection->last_read_write = 0;

	connection->quit = 0;

	return 0;
}

void connection_free (connection_t *connection)
{
	/* Close socket descriptor. */
	if (connection->sd != -1) {
		close (connection->sd);
	}

	/* Close file descriptor. */
	if (connection->fd != -1) {
		close (connection->fd);
	}

	if (connection->input_stream.buf_base) {
		free (connection->input_stream.buf_base);
	}

	buffer_free (&connection->output);

	buffer_free (&connection->domain);
	mail_transaction_free (&connection->mail_transaction);
}

void connection_reset (connection_t *connection)
{
	input_stream_t *input_stream;
	char filename[PATH_MAX + 1];

	connection->index = -1;

	/* Close socket descriptor. */
	if (connection->sd != -1) {
		close (connection->sd);
		connection->sd = -1;
	}

	/* Close file descriptor. */
	if (connection->fd != -1) {
		close (connection->fd);
		connection->fd = -1;

		snprintf (filename, sizeof (filename), "%s/%lu-%u.eml", server.incoming_directory, connection->file_timestamp, connection->nfile);
		unlink (filename);
	}

	input_stream = &connection->input_stream;

	input_stream->fd = -1;

	input_stream->read_ptr = input_stream->buf_base;
	input_stream->read_end = input_stream->buf_base;

	input_stream->end_of_file = 0;
	input_stream->error = 0;

	if (connection->output.size > connection->output.buffer_increment) {
		buffer_free (&connection->output);
	}

	connection->state = INITIAL_STATE;
	connection->next_state = INITIAL_STATE;

	connection->offset = 0;

	if (connection->domain.size > connection->domain.buffer_increment) {
		buffer_free (&connection->domain);
	} else {
		buffer_reset (&connection->domain);
	}

	mail_transaction_free (&connection->mail_transaction);
	connection->ntransactions = 0;

	connection->last_read_write = 0;

	connection->quit = 0;
}
