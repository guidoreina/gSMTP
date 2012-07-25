#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include "handle_session.h"
#include "server.h"
#include "relay.h"

#define TOO_MANY_LINES 20

extern server_t server;
extern relay_t relay;

static int handle_write (session_t *session);
static int handle_read (session_t *session);
static int read_response (session_t *session);
static int prepare_for_writing (session_t *session);
static int prepare_for_reading (session_t *session);
static int send_message (session_t *session);

int handle_session (session_t *session, struct epoll_event *event)
{
	input_stream_t *input_stream;
	transaction_t *transaction;
	buffer_t *buffer;
	stringlist_t *forward_paths;
	int optval;
	int error;
	socklen_t optlen;
	int ret;
	size_t i;

	if ((event->events & EPOLLERR) || (event->events & EPOLLHUP)) {
		return -1;
	}

	if (event->events & EPOLLOUT) {
		switch (session->state) {
			case 0:
				optlen = sizeof (optval);
				if ((getsockopt (session->sd, SOL_SOCKET, SO_ERROR, &error, &optlen) < 0) || (error)) {
					return -1;
				}

				return prepare_for_reading (session);
			case 1:
				/* Send "HELO" command. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				session->state = 2;
				return prepare_for_reading (session);
			case 3:
				/* Send "MAIL FROM:" command. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				session->state = 4;
				return prepare_for_reading (session);
			case 5:
				/* Send "RCPT TO:" command. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				session->state = 6;
				return prepare_for_reading (session);
			case 7:
				/* Send "DATA" command. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				session->state = 8;
				return prepare_for_reading (session);
			case 9:
				/* Send message. */
				if ((ret = send_message (session)) <= 0) {
					return ret;
				}

				/* Message has been sent, send end of message. */
				memcpy (session->input, ".\r\n", 3);
				session->len = 3;

				session->state = 10;
			case 10:
				/* Send end of message. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				optval = 0;
				setsockopt (session->sd, SOL_TCP, TCP_CORK, &optval, sizeof (int));

				session->state = 11;
				return prepare_for_reading (session);
			case 12:
				/* Send "QUIT" command. */
				if ((ret = handle_write (session)) <= 0) {
					return ret;
				}

				session->state = 13;
				return prepare_for_reading (session);
		}
	} else if (event->events & EPOLLIN) {
		input_stream = &relay.input_stream;

		input_stream->fd = session->sd;
		input_stream->read_ptr = input_stream->buf_base;
		input_stream->read_end = input_stream->buf_base;

		input_stream->end_of_file = 0;
		input_stream->error = 0;

		switch (session->state) {
			case 0: /* Initial state. */
				/* Receive welcoming message. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				if (ret != 220) {
					memcpy (session->input, "QUIT\r\n", 6);
					session->len = 6;

					session->state = 12;
					return prepare_for_writing (session);
				}

				/* Compose "HELO" command. */
				session->len = snprintf (session->input, sizeof (session->input), "HELO %s\r\n", domainlist_get_first_domain (&server.domainlist));

				session->state = 1;
				return prepare_for_writing (session);
			case 2:
				/* Receive response to "HELO" command. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				if (ret != 250) {
					memcpy (session->input, "QUIT\r\n", 6);
					session->len = 6;

					session->state = 12;
					return prepare_for_writing (session);
				}

				/* Compose "MAIL FROM:" command. */
				buffer = &(session->transaction_list.transactions[session->ntransaction].reverse_path);
				session->len = snprintf (session->input, sizeof (session->input), "MAIL FROM:<%.*s>\r\n", buffer->used, buffer->data);

				session->state = 3;
				return prepare_for_writing (session);
			case 4:
				/* Receive response to "MAIL FROM:" command. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				if (ret != 250) {
					memcpy (session->input, "QUIT\r\n", 6);
					session->len = 6;

					session->state = 12;
					return prepare_for_writing (session);
				}

				/* Compose "RCPT TO:" command. */
				forward_paths = &(session->transaction_list.transactions[session->ntransaction].forward_paths);
				session->len = snprintf (session->input, sizeof (session->input), "RCPT TO:<%.*s@%.*s>\r\n", forward_paths->strings[session->nforward_path].len, forward_paths->data + forward_paths->strings[session->nforward_path].string, session->domain.used, session->domain.data);

				session->state = 5;
				return prepare_for_writing (session);
			case 6:
				/* Receive response to "RCPT TO:" command. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				transaction = &(session->transaction_list.transactions[session->ntransaction]);
				forward_paths = &transaction->forward_paths;

				if (ret == 250) {
					session->naccepted++;
					forward_paths->strings[session->nforward_path].data = 1;
				}

				session->nforward_path++;

				/* If there are no more recipients... */
				if (session->nforward_path == forward_paths->used) {
					/* If none of the recipients have been accepted... */
					if (session->naccepted == 0) {
						session->ntransaction++;

						/* If there are no more transactions... */
						if (session->ntransaction == session->transaction_list.used) {
							memcpy (session->input, "QUIT\r\n", 6);
							session->len = 6;

							session->state = 12;
						} else {
							session->nforward_path = 0;

							/* Compose "HELO" command. */
							session->len = snprintf (session->input, sizeof (session->input), "HELO %s\r\n", domainlist_get_first_domain (&server.domainlist));

							session->state = 1;
						}

						return prepare_for_writing (session);
					}
				} else {
					/* Compose "RCPT TO:" command. */
					session->len = snprintf (session->input, sizeof (session->input), "RCPT TO:<%.*s@%.*s>\r\n", forward_paths->strings[session->nforward_path].len, forward_paths->data + forward_paths->strings[session->nforward_path].string, session->domain.used, session->domain.data);

					session->state = 5;
					return prepare_for_writing (session);
				}

				/* Compose "DATA" command. */
				memcpy (session->input, "DATA\r\n", 6);
				session->len = 6;

				session->state = 7;
				return prepare_for_writing (session);
			case 8:
				/* Receive response to "DATA" command. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				if (ret != 354) {
					session->ntransaction++;

					/* If there are no more transactions... */
					if (session->ntransaction == session->transaction_list.used) {
						memcpy (session->input, "QUIT\r\n", 6);
						session->len = 6;

						session->state = 12;
					} else {
						session->nforward_path = 0;
						session->naccepted = 0;

						/* Compose "HELO" command. */
						session->len = snprintf (session->input, sizeof (session->input), "HELO %s\r\n", domainlist_get_first_domain (&server.domainlist));

						session->state = 1;
					}

					return prepare_for_writing (session);
				}

				optval = 1;
				setsockopt (session->sd, SOL_TCP, TCP_CORK, &optval, sizeof (int));

				session->state = 9;
				return prepare_for_writing (session);
			case 11:
				/* Receive response to message. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				if (ret == 250) {
					/* Mark message as sent to these recipients. */
					forward_paths = &(session->transaction_list.transactions[session->ntransaction].forward_paths);
					for (i = 0; i < forward_paths->used; i++) {
						if (forward_paths->strings[i].data == 1) {
							forward_paths->strings[i].data = 2;
						}
					}
				}

				session->ntransaction++;

				/* If there are no more transactions... */
				if (session->ntransaction == session->transaction_list.used) {
					memcpy (session->input, "QUIT\r\n", 6);
					session->len = 6;

					session->state = 12;
				} else {
					session->nforward_path = 0;
					session->naccepted = 0;

					/* Compose "MAIL FROM:" command. */
					buffer = &(session->transaction_list.transactions[session->ntransaction].reverse_path);
					session->len = snprintf (session->input, sizeof (session->input), "MAIL FROM:<%.*s>\r\n", buffer->used, buffer->data);

					session->state = 3;
				}

				return prepare_for_writing (session);
			case 13:
				/* Receive response to "QUIT" command. */
				if ((ret = read_response (session)) <= 0) {
					return ret;
				}

				return -1;
		}
	}

	return -1;
}

int handle_write (session_t *session)
{
	ssize_t bytes;
	size_t zero_bytes_sent;

	zero_bytes_sent = 0;

	do {
		bytes = send (session->sd, session->input + session->offset, session->len - session->offset, 0);
		if (bytes < 0) {
			if (errno == EAGAIN) {
				return 0;
			} else if (errno == EINTR) {
				/* A signal occurred before any data was transmitted. */
				/* Add ourself to the list of interrupted sessions. */
				relay.interrupted_sessions[relay.number_interrupted_sessions++] = session;

				return 0;
			}

			return -1;
		} else if (bytes == 0) {
			if (++zero_bytes_sent >= 5) {
				return -1;
			}
		} else {
			session->last_read_write = server.current_time;

			zero_bytes_sent = 0;

			session->offset += bytes;
			if (session->offset == session->len) {
				return session->len;
			}
		}
	} while (1);
}

int handle_read (session_t *session)
{
	input_stream_t *input_stream;
	size_t len;

	input_stream = &relay.input_stream;

	/* Read line. */
	if (!input_stream_fgets (input_stream, session->input + session->offset, sizeof (session->input) - session->offset, &len)) {
		if (input_stream->error == EAGAIN) {
			session->offset += len;

			input_stream->error = 0;

			return 0;
		} else if (input_stream->error == EINTR) {
			session->offset += len;

			relay.interrupted_sessions[relay.number_interrupted_sessions++] = session;

			input_stream->error = 0;

			return 0;
		}

		return -1;
	}

	/* Has the peer performed an orderly shutdown? */
	if (input_stream->end_of_file) {
		return -1;
	}

	session->offset += len;

	session->last_read_write = server.current_time;

	/* If the line doesn't terminate in '\n'... */
	if (session->input[session->offset - 1] != '\n') {
		/* The line is too long... */
		return -1;
	}

	/* If the line is too short... */
	if (session->offset < 5) {
		return -1;
	}

	return len;
}

int read_response (session_t *session)
{
	int ret;
	int status;

	do {
		if ((ret = handle_read (session)) <= 0) {
			return ret;
		}

		/* First line? */
		if (session->nlines == 0) {
			status = atoi (session->input);
			if ((status < 100) || (status > 599)) {
				return -1;
			}

			session->status = status;
		}

		/* If not a multiline reply... */
		if (session->input[3] != '-') {
			session->nlines = 0;
			return session->status;
		}

		session->nlines++;

		/* Too many lines? */
		if (session->nlines == TOO_MANY_LINES) {
			return -1;
		}
	} while (1);
}

int prepare_for_writing (session_t *session)
{
#if !HAVE_EPOLLRDHUP
	struct epoll_event ev;

	/* Notify me of writeable events. */
	ev.events = EPOLLOUT;
	ev.data.u64 = 0;
	ev.data.fd = session->sd;

	if (epoll_ctl (relay.epoll_fd, EPOLL_CTL_MOD, session->sd, &ev) < 0) {
		return -1;
	}
#endif /* !HAVE_EPOLLRDHUP */

	session->offset = 0;

	return 0;
}

int prepare_for_reading (session_t *session)
{
#if !HAVE_EPOLLRDHUP
	struct epoll_event ev;

	/* Notify me of readable events. */
	ev.events = EPOLLIN;
	ev.data.u64 = 0;
	ev.data.fd = session->sd;

	if (epoll_ctl (relay.epoll_fd, EPOLL_CTL_MOD, session->sd, &ev) < 0) {
		return -1;
	}
#endif /* !HAVE_EPOLLRDHUP */

	session->offset = 0;

	return 0;
}

int send_message (session_t *session)
{
	/* Return values:
	 * -1: Error.
	 *  0: Send has been interrupted.
	 *  1: Message has been sent.
	 */

	transaction_t *transaction;
	ssize_t bytes;
	size_t zero_bytes_sent;

	transaction = &(session->transaction_list.transactions[session->ntransaction]);
	zero_bytes_sent = 0;

	do {
		bytes = sendfile (session->sd, transaction->fd, &transaction->offset, transaction->filesize - transaction->offset);
		if (bytes < 0) {
			if (errno == EAGAIN) {
				return 0;
			} else if (errno == EINTR) {
				/* A signal occurred before any data was transmitted. */
				/* Add ourself to the list of interrupted sessions. */
				relay.interrupted_sessions[relay.number_interrupted_sessions++] = session;

				return 0;
			} else {
				return -1;
			}
		} else if (bytes == 0) {
			if (++zero_bytes_sent >= 5) {
				return -1;
			}
		} else {
			session->last_read_write = server.current_time;

			zero_bytes_sent = 0;

			if (transaction->offset >= transaction->filesize) {
				/* We are done. */
				return 1;
			}
		}
	} while (1);
}
