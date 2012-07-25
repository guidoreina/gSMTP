#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/nameser.h>
#include "handle_connection.h"
#include "server.h"
#include "stream_copy.h"
#include "dnscache.h"
#include "parser.h"
#include "relay.h"
#include "reply_codes.h"
#include "log.h"
#include "version.h"

extern server_t server;
extern dnscache_t dnscache;

static char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int state_machine (connection_t *connection);
static int discard_command_line (connection_t *connection);
static int handle_write (connection_t *connection);
static int prepare_for_writing (connection_t *connection);
static void reset_mail_transaction (connection_t *connection);
static int handle_command (connection_t *connection);
static int handle_data_command (connection_t *connection);
static int discard_data (connection_t *connection);
static int handle_bdat_command (connection_t *connection);
static int discard_bdat (connection_t *connection);
static int prepare_message_file (connection_t *connection);
static int domain_is_reachable (const char *domain);

int handle_connection (connection_t *connection, struct epoll_event *event)
{
	if ((event->events & EPOLLERR) || (event->events & EPOLLHUP)) {
		return -1;
	}

	if (event->events & EPOLLOUT) {
		if (connection->state == INITIAL_STATE) {
			buffer_reset (&connection->output);
			if (buffer_format (&connection->output, REPLY_CODE_220, domainlist_get_first_domain (&server.domainlist), SMTPSERVER_NAME) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			connection->input_stream.fd = connection->sd;

			return handle_write (connection);
		} else if (connection->state == WRITING_RESPONSE_STATE) {
			return handle_write (connection);
		}
	} else if (event->events & EPOLLIN) {
		if (connection->state == READING_COMMAND_STATE) {
			return state_machine (connection);
		} else if (connection->state == DATA_STATE) {
			return handle_data_command (connection);
		} else if (connection->state == BDAT_STATE) {
			return handle_bdat_command (connection);
		} else if (connection->state == DISCARDING_COMMAND_LINE) {
			return discard_command_line (connection);
		} else if (connection->state == DISCARDING_DATA) {
			return discard_data (connection);
		} else if (connection->state == DISCARDING_BDAT) {
			return discard_bdat (connection);
		}
	}

	return -1;
}

int state_machine (connection_t *connection)
{
	input_stream_t *input_stream;
	size_t len;

	input_stream = &connection->input_stream;

	/* Read line. */
	if (!input_stream_fgets (input_stream, connection->input + connection->offset, sizeof (connection->input) - connection->offset, &len)) {
		if (input_stream->error == EAGAIN) {
			connection->offset += len;

			input_stream->error = 0;

			return 0;
		} else if (input_stream->error == EINTR) {
			connection->offset += len;

			server.interrupted_connections[server.number_interrupted_connections++] = connection;

			input_stream->error = 0;

			return 0;
		}

		return -1;
	}

	/* Has the peer performed an orderly shutdown? */
	if (input_stream->end_of_file) {
		return -1;
	}

	connection->offset += len;

	connection->last_read_write = server.current_time;

	/* If the line doesn't terminate in '\n'... */
	if (connection->input[connection->offset - 1] != '\n') {
		/* The line is too long... */
		connection->state = DISCARDING_COMMAND_LINE;
		connection->offset = 0;

		return discard_command_line (connection);
	}

	/* If the line is too short... */
	if (connection->offset < 6) {
		/* 500 5.5.1 Command unrecognized. */
		buffer_reset (&connection->output);
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_500, sizeof (REPLY_CODE_500) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		return prepare_for_writing (connection);
	}

	/* If the line doesn't terminate in "\r\n"? */
	if (connection->input[connection->offset - 2] != '\r') {
		/* 500 5.5.1 Command unrecognized. */
		buffer_reset (&connection->output);
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_500, sizeof (REPLY_CODE_500) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		return prepare_for_writing (connection);
	}

	return handle_command (connection);
}

int discard_command_line (connection_t *connection)
{
	input_stream_t *input_stream;
	size_t len;

	input_stream = &connection->input_stream;

	do {
		/* Read line. */
		if (!input_stream_fgets (input_stream, connection->input + connection->offset, sizeof (connection->input) - connection->offset, &len)) {
			if (input_stream->error == EAGAIN) {
				connection->offset += len;

				input_stream->error = 0;

				return 0;
			} else if (input_stream->error == EINTR) {
				connection->offset += len;

				server.interrupted_connections[server.number_interrupted_connections++] = connection;

				input_stream->error = 0;

				return 0;
			}

			return -1;
		}

		/* Has the peer performed an orderly shutdown? */
		if (input_stream->end_of_file) {
			return -1;
		}

		connection->offset += len;

		connection->last_read_write = server.current_time;

		/* If the line doesn't terminate in '\n'... */
		if (connection->input[connection->offset - 1] != '\n') {
			/* The line is too long... */
			connection->offset = 0;
		} else {
			/* We have reached the end of line. */
			/* 500 5.5.1 Command unrecognized. */
			buffer_reset (&connection->output);
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_500, sizeof (REPLY_CODE_500) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		}
	} while (1);
}

int handle_write (connection_t *connection)
{
	buffer_t *buffer;
	ssize_t bytes;
	size_t zero_bytes_sent;

#if !HAVE_EPOLLRDHUP
	struct epoll_event ev;
#endif

	zero_bytes_sent = 0;

	buffer = &connection->output;

	do {
		bytes = send (connection->sd, buffer->data + connection->offset, buffer->used - connection->offset, 0);
		if (bytes < 0) {
			if (errno == EAGAIN) {
				return 0;
			} else if (errno == EINTR) {
				/* A signal occurred before any data was transmitted. */
				/* Add ourself to the list of interrupted connections. */
				server.interrupted_connections[server.number_interrupted_connections++] = connection;

				return 0;
			}

			return -1;
		} else if (bytes == 0) {
			if (++zero_bytes_sent >= 5) {
				return -1;
			}
		} else {
			connection->last_read_write = server.current_time;

			zero_bytes_sent = 0;

			connection->offset += bytes;
			if (connection->offset == buffer->used) {
				if (connection->quit) {
					return -1;
				}

#if !HAVE_EPOLLRDHUP
				ev.events = EPOLLIN;
				ev.data.u64 = 0;
				ev.data.fd = connection->sd;

				if (epoll_ctl (server.epoll_fd, EPOLL_CTL_MOD, connection->sd, &ev) < 0) {
					return -1;
				}
#endif /* !HAVE_EPOLLRDHUP */

				if ((connection->next_state == DATA_STATE) || (connection->next_state == DISCARDING_DATA)) {
					connection->state = connection->next_state;
				} else {
					connection->state = READING_COMMAND_STATE;
				}

				connection->offset = 0;

				return 0;
			}
		}
	} while (1);
}

int prepare_for_writing (connection_t *connection)
{
#if !HAVE_EPOLLRDHUP
	struct epoll_event ev;

	/* Notify me of writeable events. */
	ev.events = EPOLLOUT;
	ev.data.u64 = 0;
	ev.data.fd = connection->sd;

	if (epoll_ctl (server.epoll_fd, EPOLL_CTL_MOD, connection->sd, &ev) < 0) {
		return -1;
	}
#endif /* !HAVE_EPOLLRDHUP */

	connection->state = WRITING_RESPONSE_STATE;
	connection->offset = 0;

	return 0;
}

void reset_mail_transaction (connection_t *connection)
{
	char filename[PATH_MAX + 1];

	mail_transaction_free (&connection->mail_transaction);

	if (connection->fd != -1) {
		close (connection->fd);
		connection->fd = -1;

		snprintf (filename, sizeof (filename), "%s/%lu-%u.eml", server.incoming_directory, connection->file_timestamp, connection->nfile);
		unlink (filename);
	}

	connection->next_state = INITIAL_STATE;
}

int handle_command (connection_t *connection)
{
	mail_transaction_t *mail_transaction;
	unsigned char *argument;
	unsigned char *local_part;
	size_t local_part_len;
	unsigned char *domain;
	size_t domainlen;
	int size_parameter;
	size_t size_value;
	size_t chunk_size;
	int last;
	int error;
	int ret;

	/* Reset output buffer. */
	buffer_reset (&connection->output);

	/* Parse command. */
	error = 0;
	if (((ret = parse_smtp_command ((unsigned char *) connection->input, &argument, &error)) < 0) && (error == 500)) {
		/* 500 5.5.1 Command unrecognized. */
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_500, sizeof (REPLY_CODE_500) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		return prepare_for_writing (connection);
	}

	mail_transaction = &connection->mail_transaction;

	switch ((eSmtpCommand) ret) {
		case BDAT:
			/* If the client hasn't issued the "RCPT TO:" command... */
			if (mail_transaction->forward_paths.used == 0) {
				/* 503 5.0.0 Need RCPT (recipient). */
				if (buffer_append_size_bounded_string (&connection->output, NEED_RCPT_COMMAND, sizeof (NEED_RCPT_COMMAND) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Wrong arguments? */
			if ((error == 501) || (parse_bdat (argument, &chunk_size, &last) < 0)) {
				/* 501 Syntax: "BDAT" SP chunk-size[SP "LAST"]. */
				if (buffer_append_size_bounded_string (&connection->output, BDAT_SYNTAX, sizeof (BDAT_SYNTAX) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			connection->chunk_size = chunk_size;
			connection->last = last;

			if (connection->next_state == BDAT_STATE) {
				connection->state = BDAT_STATE;
				return handle_bdat_command (connection);
			}

			if (connection->next_state == DISCARDING_BDAT) {
				connection->state = DISCARDING_BDAT;
				return discard_bdat (connection);
			}

			if (prepare_message_file (connection) < 0) {
				connection->state = DISCARDING_BDAT;
				connection->next_state = DISCARDING_BDAT;

				return discard_bdat (connection);
			}

			connection->filesize = 0;

			connection->state = BDAT_STATE;
			connection->next_state = BDAT_STATE;

			return handle_bdat_command (connection);
		case DATA:
			/* If we are handling BDAT... */
			if ((connection->next_state == BDAT_STATE) || (connection->next_state == DISCARDING_BDAT)) {
				/* 503 5.5.1 Error: MAIL transaction in progress. */
				if (buffer_append_size_bounded_string (&connection->output, MAIL_TRANSACTION_IN_PROGRESS, sizeof (MAIL_TRANSACTION_IN_PROGRESS) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* If the client hasn't issued the "RCPT TO:" command... */
			if (mail_transaction->forward_paths.used == 0) {
				/* 503 5.0.0 Need RCPT (recipient). */
				if (buffer_append_size_bounded_string (&connection->output, NEED_RCPT_COMMAND, sizeof (NEED_RCPT_COMMAND) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			if (error == 501) {
				/* 501 5.5.4 Syntax: "DATA". */
				if (buffer_append_size_bounded_string (&connection->output, DATA_SYNTAX, sizeof (DATA_SYNTAX) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			if (prepare_message_file (connection) < 0) {
				connection->next_state = DISCARDING_DATA;
			} else {
				connection->filesize = 0;

				connection->next_state = DATA_STATE;
			}

			/* 354 Enter mail, end with "." on a line by itself. */
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_354, sizeof (REPLY_CODE_354) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case EHLO:
			if (error == 501) {
				/* 501 5.0.0 ehlo requires domain address. */
				if (buffer_append_size_bounded_string (&connection->output, EHLO_REQUIRES_DOMAIN_ADDRESS, sizeof (EHLO_REQUIRES_DOMAIN_ADDRESS) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Valid domain? */
			if (parse_domain (argument, &domainlen) < 0) {
				/* 501 5.0.0 Invalid domain name. */
				if (buffer_append_size_bounded_string (&connection->output, INVALID_DOMAIN_NAME, sizeof (INVALID_DOMAIN_NAME) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Save domain. */
			buffer_reset (&connection->domain);
			if (buffer_allocate (&connection->domain, domainlen + 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			memcpy (connection->domain.data, argument, domainlen);
			connection->domain.data[domainlen] = 0;
			connection->domain.used = domainlen + 1;

			reset_mail_transaction (connection);

			if (buffer_format (&connection->output, EHLO_RESPONSE, domainlist_get_first_domain (&server.domainlist), server.max_message_size) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case EXPN:
			/* 502 5.5.1 Command not implemented. */
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_502, sizeof (REPLY_CODE_502) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case HELO:
			if (error == 501) {
				/* 501 5.0.0 helo requires domain address. */
				if (buffer_append_size_bounded_string (&connection->output, HELO_REQUIRES_DOMAIN_ADDRESS, sizeof (HELO_REQUIRES_DOMAIN_ADDRESS) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Valid domain? */
			if (parse_domain (argument, &domainlen) < 0) {
				/* 501 5.0.0 Invalid domain name. */
				if (buffer_append_size_bounded_string (&connection->output, INVALID_DOMAIN_NAME, sizeof (INVALID_DOMAIN_NAME) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Save domain. */
			buffer_reset (&connection->domain);
			if (buffer_allocate (&connection->domain, domainlen + 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			memcpy (connection->domain.data, argument, domainlen);
			connection->domain.data[domainlen] = 0;
			connection->domain.used = domainlen + 1;

			reset_mail_transaction (connection);

			if (buffer_format (&connection->output, HELO_RESPONSE, domainlist_get_first_domain (&server.domainlist)) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case HELP:
			/* 502 5.5.1 Command not implemented. */
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_502, sizeof (REPLY_CODE_502) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case MAIL:
			/* If the client has already issued the "MAIL FROM:" command... */
			if (mail_transaction->reverse_path[0]) {
				/* 503 5.5.0 Sender already specified. */
				if (buffer_append_size_bounded_string (&connection->output, SENDER_ALREADY_SPECIFIED, sizeof (SENDER_ALREADY_SPECIFIED) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			} else {
				/* If the client hasn't issued neither the "EHLO" nor the "HELO" command... */
				if (connection->domain.used == 0) {
					/* 503 5.0.0 Polite people say HELO first. */
					if (buffer_append_size_bounded_string (&connection->output, NEED_HELO_COMMAND, sizeof (NEED_HELO_COMMAND) - 1) < 0) {
						/* Couldn't allocate memory. */
						return -1;
					}

					return prepare_for_writing (connection);
				}
			}

			if (error == 501) {
				/* 501 5.5.2 Syntax error in parameters scanning "from". */
				if (buffer_append_size_bounded_string (&connection->output, SYNTAX_ERROR_MAIL_FROM, sizeof (SYNTAX_ERROR_MAIL_FROM) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Valid reverse path? */
			if (parse_reverse_path (argument, &local_part, &local_part_len, &domain, &domainlen, &size_parameter, &size_value) < 0) {
				/* 501 5.1.7 Bad sender address syntax. */
				if (buffer_append_size_bounded_string (&connection->output, BAD_SENDER, sizeof (BAD_SENDER) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Too many mails? */
			if (connection->ntransactions >= server.max_transactions) {
				/* 450 4.7.1 Error: too much mail from <domain>. */
				if (buffer_format (&connection->output, TOO_MANY_TRANSACTIONS, connection->domain.data) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			if (size_parameter) {
				if (size_value > server.max_message_size) {
					/* 552 5.2.3 Message size exceeds maximum value. */
					if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_552, sizeof (REPLY_CODE_552) - 1) < 0) {
						/* Couldn't allocate memory. */
						return -1;
					}

					return prepare_for_writing (connection);
				}
			}

			/* Null reverse path? */
			if (!local_part) {
				memcpy (mail_transaction->reverse_path, "<>", 3);
			} else {
				mail_transaction_set_reverse_path (mail_transaction, (const char *) local_part, local_part_len, (const char *) domain, domainlen);
			}

			/* 250 2.1.0 Sender ok. */
			if (buffer_append_size_bounded_string (&connection->output, SENDER_OK, sizeof (SENDER_OK) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case NOOP:
			/* 250 2.0.0 OK. */
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_250_2_0_0, sizeof (REPLY_CODE_250_2_0_0) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case QUIT:
			connection->quit = 1;

			/* 221 2.0.0 <domain> closing connection. */
			if (buffer_format (&connection->output, REPLY_CODE_221, domainlist_get_first_domain (&server.domainlist)) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case RCPT:
			/* If we are handling BDAT... */
			if ((connection->next_state == BDAT_STATE) || (connection->next_state == DISCARDING_BDAT)) {
				/* 503 5.5.1 Error: MAIL transaction in progress. */
				if (buffer_append_size_bounded_string (&connection->output, MAIL_TRANSACTION_IN_PROGRESS, sizeof (MAIL_TRANSACTION_IN_PROGRESS) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* If the client hasn't issued the "MAIL FROM:" command... */
			if (!mail_transaction->reverse_path[0]) {
				/* 503 5.0.0 Need MAIL before RCPT. */
				if (buffer_append_size_bounded_string (&connection->output, NEED_MAIL_COMMAND, sizeof (NEED_MAIL_COMMAND) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			if (error == 501) {
				/* 501 5.5.2 Syntax error in parameters scanning "to". */
				if (buffer_append_size_bounded_string (&connection->output, SYNTAX_ERROR_RCPT_TO, sizeof (SYNTAX_ERROR_RCPT_TO) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Valid forward path? */
			if (parse_forward_path (argument, &local_part, &local_part_len, &domain, &domainlen) < 0) {
				/* 501 5.1.3 Syntax error in mailbox address. */
				if (buffer_append_size_bounded_string (&connection->output, BAD_RECIPIENT, sizeof (BAD_RECIPIENT) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Too many recipients? */
			if (mail_transaction->forward_paths.used >= server.max_recipients) {
				/* 452 4.5.3 Too many recipients. */
				if (buffer_append_size_bounded_string (&connection->output, TOO_MANY_RECIPIENTS, sizeof (TOO_MANY_RECIPIENTS) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			/* Postmaster? */
			if (!domain) {
				local_part = server.postmaster.local_part;
				local_part_len = server.postmaster.local_part_len;
				domain = server.postmaster.domain;
				domainlen = server.postmaster.domainlen;
			} else {
				local_part[local_part_len] = 0;
				domain[domainlen] = 0;

				/* If I know nothing about: <local_part>@<domain>... */
				if ((ret = domainlist_search (&server.domainlist, (const char *) local_part, (const char *) domain)) < 0) {
					/* If I handle the recipient's domain... */
					if (ret == -1) {
						/* 550 5.1.1 Addressee unknown. */
						if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_550, sizeof (REPLY_CODE_550) - 1) < 0) {
							/* Couldn't allocate memory. */
							return -1;
						}

						return prepare_for_writing (connection);
					}

					/* If relay is not allowed for this IP... */
					if (ip_list_search (&server.ip_list, ntohl (connection->sin.sin_addr.s_addr)) < 0) {
						/* 550 5.1.1 Addressee unknown. */
						if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_550, sizeof (REPLY_CODE_550) - 1) < 0) {
							/* Couldn't allocate memory. */
							return -1;
						}

						return prepare_for_writing (connection);
					}

					/* If I don't know how to deliver/relay a message to this forward-path... */
					if (!domain_is_reachable ((const char *) domain)) {
						/* 550 5.1.1 Addressee unknown. */
						if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_550, sizeof (REPLY_CODE_550) - 1) < 0) {
							/* Couldn't allocate memory. */
							return -1;
						}

						return prepare_for_writing (connection);
					}
				}
			}

			if (mail_transaction_add_forward_path (mail_transaction, (const char *) local_part, local_part_len, (const char *) domain, domainlen) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			/* 250 2.1.5 Recipient ok. */
			if (buffer_append_size_bounded_string (&connection->output, RECIPIENT_OK, sizeof (RECIPIENT_OK) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case RSET:
			if (error == 501) {
				/* 501 5.5.4 Syntax: "RSET". */
				if (buffer_append_size_bounded_string (&connection->output, RSET_SYNTAX, sizeof (RSET_SYNTAX) - 1) < 0) {
					/* Couldn't allocate memory. */
					return -1;
				}

				return prepare_for_writing (connection);
			}

			reset_mail_transaction (connection);

			/* 250 2.0.0 Reset state. */
			if (buffer_append_size_bounded_string (&connection->output, RESET_STATE, sizeof (RESET_STATE) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
		case VRFY:
			/* 502 5.5.1 Command not implemented. */
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_502, sizeof (REPLY_CODE_502) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			return prepare_for_writing (connection);
	}

	return -1;
}

int handle_data_command (connection_t *connection)
{
	input_stream_t *input_stream;
	char oldpath[PATH_MAX + 1];
	char newpath[PATH_MAX + 1];
	size_t len;

	input_stream = &connection->input_stream;

	do {
		/* Read line. */
		if (!input_stream_fgets (input_stream, connection->input + connection->offset, sizeof (connection->input) - connection->offset, &len)) {
			if (input_stream->error == EAGAIN) {
				connection->offset += len;

				input_stream->error = 0;

				return 0;
			} else if (input_stream->error == EINTR) {
				connection->offset += len;

				server.interrupted_connections[server.number_interrupted_connections++] = connection;

				input_stream->error = 0;

				return 0;
			}

			return -1;
		}

		/* Has the peer performed an orderly shutdown? */
		if (input_stream->end_of_file) {
			return -1;
		}

		connection->offset += len;

		connection->last_read_write = server.current_time;

		/* If the line terminates in '\n'... */
		if (connection->input[connection->offset - 1] == '\n') {
			/* End of message? */
			if (((connection->offset == 2) || ((connection->offset == 3) && (connection->input[1] == '\r'))) && (connection->input[0] == '.')) {
				break;
			}
		}

		/* Write line to disk. */
		if (write (connection->fd, connection->input, connection->offset) != connection->offset) {
			/* Couldn't write. */
			/* 452 4.4.5 Insufficient disk space; try again later. */
			buffer_reset (&connection->output);
			if (buffer_append_size_bounded_string (&connection->output, INSUFFICIENT_DISK_SPACE, sizeof (INSUFFICIENT_DISK_SPACE) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			connection->state = DISCARDING_DATA;
			connection->offset = 0;

			return discard_data (connection);
		}

		connection->filesize += connection->offset;

		/* If the message is too large... */
		if (connection->filesize > server.max_message_size) {
			/* 552 5.2.3 Message size exceeds maximum value. */
			buffer_reset (&connection->output);
			if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_552, sizeof (REPLY_CODE_552) - 1) < 0) {
				/* Couldn't allocate memory. */
				return -1;
			}

			connection->state = DISCARDING_DATA;
			connection->offset = 0;

			return discard_data (connection);
		}

		connection->offset = 0;
	} while (1);

	close (connection->fd);
	connection->fd = -1;

	snprintf (oldpath, sizeof (oldpath), "%s/%lu-%u.eml", server.incoming_directory, connection->file_timestamp, connection->nfile);

	/* 250 2.0.0 Message accepted for delivery. */
	buffer_reset (&connection->output);
	if (buffer_append_size_bounded_string (&connection->output, MESSAGE_ACCEPTED_FOR_DELIVERY, sizeof (MESSAGE_ACCEPTED_FOR_DELIVERY) - 1) < 0) {
		/* Couldn't allocate memory. */
		unlink (oldpath);
		return -1;
	}

	/* Move file from incoming to received directory. */
	snprintf (newpath, sizeof (newpath), "%s/%lu-%u.eml", server.received_directory, connection->file_timestamp, connection->nfile);
	rename (oldpath, newpath);

	/* Notify delivery process. */
	kill (server.delivery_pid, SIGUSR1);

	if (server.log_mails) {
		log_mail (&server.local_time, connection);
	}

	connection->ntransactions++;

	mail_transaction_free (&connection->mail_transaction);
	connection->next_state = INITIAL_STATE;

	return prepare_for_writing (connection);
}

int discard_data (connection_t *connection)
{
	input_stream_t *input_stream;
	size_t len;

	input_stream = &connection->input_stream;

	do {
		/* Read line. */
		if (!input_stream_fgets (input_stream, connection->input + connection->offset, sizeof (connection->input) - connection->offset, &len)) {
			if (input_stream->error == EAGAIN) {
				connection->offset += len;

				input_stream->error = 0;

				return 0;
			} else if (input_stream->error == EINTR) {
				connection->offset += len;

				server.interrupted_connections[server.number_interrupted_connections++] = connection;

				input_stream->error = 0;

				return 0;
			}

			return -1;
		}

		/* Has the peer performed an orderly shutdown? */
		if (input_stream->end_of_file) {
			return -1;
		}

		connection->offset += len;

		connection->last_read_write = server.current_time;

		/* If the line terminates in '\n'... */
		if (connection->input[connection->offset - 1] == '\n') {
			/* End of message? */
			if (((connection->offset == 2) || ((connection->offset == 3) && (connection->input[1] == '\r'))) && (connection->input[0] == '.')) {
				break;
			}
		}

		connection->offset = 0;
	} while (1);

	reset_mail_transaction (connection);

	return prepare_for_writing (connection);
}

int handle_bdat_command (connection_t *connection)
{
	input_stream_t *input_stream;
	stream_copy_t stream_copy;
	char oldpath[PATH_MAX + 1];
	char newpath[PATH_MAX + 1];
	size_t chunk_size;

	input_stream = &connection->input_stream;

	stream_copy_init (&stream_copy, input_stream, connection->fd);

	/* Save chunk size. */
	chunk_size = connection->chunk_size;

	if (stream_copy_chunk (&stream_copy, &connection->chunk_size) < 0) {
		if (input_stream->error == EAGAIN) {
			input_stream->error = 0;
			return 0;
		} else if (input_stream->error == EINTR) {
			server.interrupted_connections[server.number_interrupted_connections++] = connection;

			input_stream->error = 0;

			return 0;
		} else if (!input_stream->error) {
			/* Couldn't write. */
			/* 452 4.4.5 Insufficient disk space; try again later. */
			connection->state = DISCARDING_BDAT;
			connection->next_state = DISCARDING_BDAT;

			return discard_bdat (connection);
		}

		return -1;
	}

	/* Has the peer performed an orderly shutdown? */
	if (input_stream->end_of_file) {
		return -1;
	}

	connection->filesize += chunk_size;

	buffer_reset (&connection->output);

	/* If the message is too large... */
	if (connection->filesize > server.max_message_size) {
		/* 552 5.2.3 Message size exceeds maximum value. */
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_552, sizeof (REPLY_CODE_552) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		reset_mail_transaction (connection);

		return prepare_for_writing (connection);
	}

	/* If it's not the last chunk... */
	if (!connection->last) {
		/* 250 2.0.0 OK. */
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_250_2_0_0, sizeof (REPLY_CODE_250_2_0_0) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		return prepare_for_writing (connection);
	}

	close (connection->fd);
	connection->fd = -1;

	snprintf (oldpath, sizeof (oldpath), "%s/%lu-%u.eml", server.incoming_directory, connection->file_timestamp, connection->nfile);

	/* 250 2.0.0 Message accepted for delivery. */
	if (buffer_append_size_bounded_string (&connection->output, MESSAGE_ACCEPTED_FOR_DELIVERY, sizeof (MESSAGE_ACCEPTED_FOR_DELIVERY) - 1) < 0) {
		/* Couldn't allocate memory. */
		unlink (oldpath);
		return -1;
	}

	/* Move file from incoming to received directory. */
	snprintf (newpath, sizeof (newpath), "%s/%lu-%u.eml", server.received_directory, connection->file_timestamp, connection->nfile);
	rename (oldpath, newpath);

	/* Notify delivery process. */
	kill (server.delivery_pid, SIGUSR1);

	if (server.log_mails) {
		log_mail (&server.local_time, connection);
	}

	connection->ntransactions++;

	mail_transaction_free (&connection->mail_transaction);
	connection->next_state = INITIAL_STATE;

	return prepare_for_writing (connection);
}

int discard_bdat (connection_t *connection)
{
	input_stream_t *input_stream;

	input_stream = &connection->input_stream;

	input_stream_skip (input_stream, connection->chunk_size);
	if (input_stream->error == EAGAIN) {
		input_stream->error = 0;
		return 0;
	} else if (input_stream->error == EINTR) {
		server.interrupted_connections[server.number_interrupted_connections++] = connection;

		input_stream->error = 0;

		return 0;
	} else if (input_stream->error) {
		return -1;
	}

	/* Has the peer performed an orderly shutdown? */
	if (input_stream->end_of_file) {
		return -1;
	}

	buffer_reset (&connection->output);

	/* If it's not the last chunk... */
	if (!connection->last) {
		/* 250 2.0.0 OK. */
		if (buffer_append_size_bounded_string (&connection->output, REPLY_CODE_250_2_0_0, sizeof (REPLY_CODE_250_2_0_0) - 1) < 0) {
			/* Couldn't allocate memory. */
			return -1;
		}

		return prepare_for_writing (connection);
	}

	/* 452 4.4.5 Insufficient disk space; try again later. */
	if (buffer_append_size_bounded_string (&connection->output, INSUFFICIENT_DISK_SPACE, sizeof (INSUFFICIENT_DISK_SPACE) - 1) < 0) {
		/* Couldn't allocate memory. */
		return -1;
	}

	reset_mail_transaction (connection);

	return prepare_for_writing (connection);
}

int prepare_message_file (connection_t *connection)
{
	buffer_t buffer;
	mail_transaction_t *mail_transaction;
	domainlist_t *forward_paths;
	domain_t *domain;
	struct tm *stm;
	char *data;
	char filename[PATH_MAX + 1];
	char peer[20];
	struct stat buf;
	size_t i, j;

	/* Get peer IP. */
	if (!inet_ntop (AF_INET, &(connection->sin.sin_addr), peer, sizeof (peer))) {
		return -1;
	}

	/* Seek an available filename. */
	do {
		snprintf (filename, sizeof (filename), "%s/%lu-%u.eml", server.incoming_directory, server.current_time, server.nfile);
		if (stat (filename, &buf) < 0) {
			connection->file_timestamp = server.current_time;
			connection->nfile = server.nfile++;

			break;
		}

		server.nfile++;
	} while (1);

	/* Open file where we will store the message. */
	connection->fd = open (filename, O_CREAT | O_WRONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (connection->fd < 0) {
		return -1;
	}

	/* Build a pre-header with reverse-path and forward-paths for the delivery program. */
	buffer_init (&buffer, 1024);
	mail_transaction = &connection->mail_transaction;

	/* Add reverse path to the pre-header. */
	if (buffer_format (&buffer, "MAIL FROM: %s\r\n", mail_transaction->reverse_path) < 0) {
		/* Couldn't allocate memory. */
		buffer_free (&buffer);
		return -1;
	}

	forward_paths = &mail_transaction->forward_paths;
	data = forward_paths->data.data;

	/* Add forward paths to the pre-header. */
	for (i = 0; i < forward_paths->used; i++) {
		domain = &(forward_paths->records[i]);
		for (j = 0; j < domain->used; j++) {
			if (buffer_format (&buffer, "RCPT TO: %s@%s\r\n", data + domain->local_parts[j], data + domain->domain_name) < 0) {
				/* Couldn't allocate memory. */
				buffer_free (&buffer);
				return -1;
			}
		}
	}

	/* Add an empty line to the pre-header. */
	if (buffer_append_size_bounded_string (&buffer, "\r\n", 2) < 0) {
		/* Couldn't allocate memory. */
		buffer_free (&buffer);
		return -1;
	}

	stm = &server.stm;

	/* Message starts here. */
	/* Add Received field. */
	if (buffer_format (&buffer, "Received: FROM %s\r\n\tBY %s;\r\n\t%s, %d %s %d %02d:%02d:%02d GMT\r\n", peer, domainlist_get_first_domain (&server.domainlist), days[stm->tm_wday], stm->tm_mday, months[stm->tm_mon], 1900 + stm->tm_year, stm->tm_hour, stm->tm_min, stm->tm_sec) < 0) {
		/* Couldn't allocate memory. */
		buffer_free (&buffer);
		return -1;
	}

	/* Write buffer to disk. */
	if (write (connection->fd, buffer.data, buffer.used) != buffer.used) {
		/* Couldn't write. */
		buffer_free (&buffer);
		return -1;
	}

	buffer_free (&buffer);

	return 0;
}

int domain_is_reachable (const char *domain)
{
	eDnsStatus status;
	off_t index;
	int sd;

	status = dnscache_lookup (&dnscache, domain, T_MX, MAX_HOSTS, &index, server.current_time);
	if (status == DNS_SUCCESS) {
		return 1;
	}

	if ((status == DNS_HOST_NOT_FOUND) || (status == DNS_NO_DATA)) {
		status = dnscache_lookup (&dnscache, domain, T_A, MAX_HOSTS, &index, server.current_time);
		if (status != DNS_SUCCESS) {
			return 0;
		}

		if ((sd = connect_to_smtp_server (&(dnscache.records[index]))) < 0) {
			return 0;
		}

		close (sd);

		return 1;
	}

	return 0;
}
