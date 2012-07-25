#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/nameser.h>
#include "relay.h"
#include "server.h"
#include "configuration.h"
#include "handle_session.h"
#include "parser.h"

#define RELAY_EVERY            2 /* seconds. */
#define MESSAGE_EXTENSION      ".eml"
#define MAX_MESSAGES_PER_BURST 10

#define SMTP_DEFAULT_PORT      25

extern server_t server;
extern configuration_t conf;
extern configuration_t mime_types;
extern dnscache_t dnscache;

relay_t relay;

static void stop (int nsignal);
static void handle_alarm (int nsignal);

static int relay_init (relay_t *relay);
static void relay_free (relay_t *relay);

static int do_relay (relay_t *relay);
static int read_pre_header (input_stream_t *input_stream, mail_transaction_t *mail_transaction, off_t *offset);
static void remove_session (relay_t *relay, int client);
static int send_messages (relay_t *relay);

void relay_loop (void)
{
	struct sigaction act;
	struct itimerval interval;

	if (relay_init (&relay) < 0) {
		domainlist_free (&server.domainlist);

		configuration_free (&conf);
		configuration_free (&mime_types);

		fprintf (stderr, "Couldn't create relay.\n");
		exit (-1);
	}

	/* Install signal handlers. */
	sigemptyset (&act.sa_mask);
	act.sa_flags = 0;

	act.sa_handler = stop;
	sigaction (SIGINT, &act, NULL);
	sigaction (SIGTERM, &act, NULL);

	act.sa_handler = handle_alarm;
	sigaction (SIGALRM, &act, NULL);

	/* Set alarm every second. */
	interval.it_interval.tv_sec = 1;
	interval.it_interval.tv_usec = 0;
	interval.it_value.tv_sec = 1;
	interval.it_value.tv_usec = 0;
	if (setitimer (ITIMER_REAL, &interval, NULL) < 0) {
		perror ("setitimer");

		domainlist_free (&server.domainlist);

		configuration_free (&mime_types);
		configuration_free (&conf);

		exit (-1);
	}

	server.running = 1;

	do {
		if (do_relay (&relay) < 0) {
			break;
		}

		sleep (RELAY_EVERY);

		if (server.running) {
			/* If the parent process is not alive... */
			if (kill (server.delivery_pid, 0) < 0) {
				break;
			}
		} else {
			break;
		}
	} while (1);

	relay_free (&relay);
	dnscache_free (&dnscache);

	domainlist_free (&server.domainlist);

	configuration_free (&conf);
	configuration_free (&mime_types);

	exit (0);
}

void stop (int nsignal)
{
	printf ("Relay process received signal %d.\nStopping...\n", nsignal);

	server.running = 0;
}

void handle_alarm (int nsignal)
{
	server.handle_alarm = 1;
}

int relay_init (relay_t *relay)
{
	input_stream_t *input_stream;
	struct rlimit rlim;
	size_t size;
	int i;

	relay->epoll_fd = -1;
	relay->nfds = 0;
	relay->index = NULL;
	relay->events = NULL;
	relay->sessions = NULL;
	relay->interrupted_sessions = NULL;
	relay->number_interrupted_sessions = 0;

	input_stream = &relay->input_stream;
	input_stream->fd = -1;
	input_stream->buf_base = NULL;
	input_stream->buf_end = NULL;
	input_stream->size = 0;

	input_stream->read_ptr = NULL;
	input_stream->read_end = NULL;

	input_stream->end_of_file = 0;
	input_stream->error = 0;

	/* Get the hard limit of the maximum number of file descriptors. */
	if (getrlimit (RLIMIT_NOFILE, &rlim) < 0) {
		perror ("getrlimit");
		return -1;
	}

	relay->max_file_descriptors = rlim.rlim_max;

	/* Open epoll file descriptor. */
	if ((relay->epoll_fd = epoll_create (relay->max_file_descriptors)) < 0) {
		perror ("epoll_create");
		return -1;
	}

	/* Allocate memory for index. */
	relay->index = (int *) malloc (relay->max_file_descriptors * sizeof (int));
	if (!relay->index) {
		close (relay->epoll_fd);
		relay->epoll_fd = -1;

		fprintf (stderr, "Couldn't allocate memory for index.\n");
		return -1;
	}

	/* Allocate memory for epoll events. */
	relay->events = (struct epoll_event *) malloc (relay->max_file_descriptors * sizeof (struct epoll_event));
	if (!relay->events) {
		free (relay->index);
		relay->index = NULL;

		close (relay->epoll_fd);
		relay->epoll_fd = -1;

		fprintf (stderr, "Couldn't allocate memory for epoll events.\n");
		return -1;
	}

	/* Allocate memory for sessions. */
	relay->sessions = (session_t *) malloc (relay->max_file_descriptors * sizeof (session_t));
	if (!relay->sessions) {
		free (relay->events);
		relay->events = NULL;

		free (relay->index);
		relay->index = NULL;

		close (relay->epoll_fd);
		relay->epoll_fd = -1;

		fprintf (stderr, "Couldn't allocate memory for connections.\n");
		return -1;
	}

	/* Allocate memory for interrupted sessions. */
	relay->interrupted_sessions = (session_t **) malloc (relay->max_file_descriptors * sizeof (session_t *));
	if (!relay->interrupted_sessions) {
		free (relay->sessions);
		relay->sessions = NULL;

		free (relay->events);
		relay->events = NULL;

		free (relay->index);
		relay->index = NULL;

		close (relay->epoll_fd);
		relay->epoll_fd = -1;

		fprintf (stderr, "Couldn't allocate memory for interrupted sessions.\n");
		return -1;
	}

	/* Allocate memory for input stream. */
	size = TEXT_LINE_MAXLEN + 1;
	input_stream->buf_base = (unsigned char *) malloc (size);
	if (!input_stream->buf_base) {
		free (relay->interrupted_sessions);
		relay->interrupted_sessions = NULL;

		free (relay->sessions);
		relay->sessions = NULL;

		free (relay->events);
		relay->events = NULL;

		free (relay->index);
		relay->index = NULL;

		close (relay->epoll_fd);
		relay->epoll_fd = -1;

		fprintf (stderr, "Couldn't allocate memory for input stream.\n");
		return -1;
	}

	input_stream->buf_end = input_stream->buf_base + size;
	input_stream->size = size;

	input_stream->read_ptr = input_stream->buf_base;
	input_stream->read_end = input_stream->buf_base;

	/* Initialize sessions. */
	for (i = 0; i < relay->max_file_descriptors; i++) {
		session_init (&(relay->sessions[i]));
	}

	return 0;
}

void relay_free (relay_t *relay)
{
	input_stream_t *input_stream;
	int i;

	if (relay->epoll_fd != -1) {
		close (relay->epoll_fd);
		relay->epoll_fd = -1;
	}

	relay->nfds = 0;

	if (relay->index) {
		free (relay->index);
		relay->index = NULL;
	}

	if (relay->events) {
		free (relay->events);
		relay->events = NULL;
	}

	if (relay->sessions) {
		for (i = 0; i < relay->max_file_descriptors; i++) {
			session_free (&(relay->sessions[i]));
		}

		free (relay->sessions);
		relay->sessions = NULL;
	}

	if (relay->interrupted_sessions) {
		free (relay->interrupted_sessions);
		relay->interrupted_sessions = NULL;
	}

	relay->number_interrupted_sessions = 0;

	input_stream = &relay->input_stream;
	if (input_stream->buf_base) {
		free (input_stream->buf_base);
		input_stream->buf_base = NULL;
	}

	input_stream->fd = -1;
	input_stream->buf_end = NULL;
	input_stream->size = 0;

	input_stream->read_ptr = NULL;
	input_stream->read_end = NULL;

	input_stream->end_of_file = 0;
	input_stream->error = 0;
}

int do_relay (relay_t *relay)
{
	DIR *relay_directory;
	struct dirent *mail;
	stringlist_t files;
	size_t nmessages;
	char oldpath[PATH_MAX + 1];
	char newpath[PATH_MAX + 1];
	input_stream_t *input_stream;
	mail_transaction_t mail_transaction;
	off_t offset;
	off_t filesize;
	domainlist_t *forward_paths;
	domain_t *domain;
	char *data;
	off_t index;
	session_t *session;
	transaction_list_t *transaction_list;
	transaction_t *transaction;
	eDnsStatus status;
	struct epoll_event ev;
	size_t len;
	int fd;
	int sd;
	size_t i, j, k;
	int already_connected;

	/* Open directory where are the messages to relay. */
	relay_directory = opendir (server.relay_directory);
	if (!relay_directory) {
		fprintf (stderr, "Couldn't open relay directory %s.\n", server.relay_directory);
		return -1;
	}

	stringlist_init (&files);
	nmessages = 0;

	/* Build a list of messages to relay. */
	while (((mail = readdir (relay_directory)) != NULL) && (nmessages < MAX_MESSAGES_PER_BURST)) {
		if (mail->d_name[0] == '.') {
			continue;
		}

		/* If not a message... */
		len = strlen (mail->d_name);
		if ((len < sizeof (MESSAGE_EXTENSION)) || (strcmp (mail->d_name + len - (sizeof (MESSAGE_EXTENSION) - 1), MESSAGE_EXTENSION) != 0)) {
			continue;
		}

		snprintf (oldpath, sizeof (oldpath), "%s/%s", server.relay_directory, mail->d_name);
		fd = open (oldpath, O_RDONLY);
		if (fd < 0) {
			continue;
		}

		if (stringlist_insert_string (&files, mail->d_name, fd) < 0) {
			close (fd);
			for (i = 0; i < files.used; i++) {
				close (files.strings[i].data);
			}

			stringlist_free (&files);

			closedir (relay_directory);

			return -1;
		}

		nmessages++;
	}

	closedir (relay_directory);

	if (!nmessages) {
		return 0;
	}

	input_stream = &relay->input_stream;

	mail_transaction_init (&mail_transaction);

	/* Save current time. */
	server.current_time = time (NULL);
	gmtime_r (&server.current_time, &server.stm);

	/* For each message to relay... */
	for (i = 0; i < nmessages; i++) {
		input_stream->fd = files.strings[i].data;
		input_stream->read_ptr = input_stream->buf_base;
		input_stream->read_end = input_stream->buf_base;

		input_stream->end_of_file = 0;
		input_stream->error = 0;

		if (read_pre_header (&relay->input_stream, &mail_transaction, &offset) < 0) {
			mail_transaction_free (&mail_transaction);
			close (relay->input_stream.fd);

			/* Move mail to error directory. */
			snprintf (oldpath, sizeof (oldpath), "%s/%.*s", server.relay_directory, files.strings[i].len, files.data + files.strings[i].string);
			snprintf (newpath, sizeof (newpath), "%s/%.*s", server.error_directory, files.strings[i].len, files.data + files.strings[i].string);
			rename (oldpath, newpath);

			/* Mark file as closed. */
			files.strings[i].data = -1;

			continue;
		}

		/* Get filesize. */
		filesize = lseek (relay->input_stream.fd, 0, SEEK_END);

		forward_paths = &mail_transaction.forward_paths;
		data = forward_paths->data.data;

		/* For each domain... */
		for (j = 0; j < forward_paths->used; j++) {
			domain = &(forward_paths->records[j]);

			/* Resolve DNS. */
			status = dnscache_lookup (&dnscache, data + domain->domain_name, T_MX, MAX_HOSTS, &index, server.current_time);
			if ((status == DNS_HOST_NOT_FOUND) || (status == DNS_NO_DATA)) {
				status = dnscache_lookup (&dnscache, data + domain->domain_name, T_A, MAX_HOSTS, &index, server.current_time);
			}

			if (status != DNS_SUCCESS) {
				/* The domain is unreachable. */
				continue;
			}

			/* We might be already connected to this SMTP server. */
			already_connected = 0;
			for (k = 0; k < relay->nfds; k++) {
				if (relay->sessions[relay->index[k]].dnscache == index) {
					already_connected = 1;
					break;
				}
			}

			/* If we weren't already connected... */
			if (!already_connected) {
				/* Connect to SMTP server. */
				if ((sd = connect_to_smtp_server (&(dnscache.records[index]))) < 0) {
					/* The domain is unreachable. */
					continue;
				}

				/* Add connection to epoll descriptor. */
				ev.events = EPOLLOUT;
				ev.data.u64 = 0;
				ev.data.fd = sd;
				if (epoll_ctl (relay->epoll_fd, EPOLL_CTL_ADD, sd, &ev) < 0) {
					perror ("epoll_ctl");

					close (sd);

					break;
				}

				session = &(relay->sessions[sd]);

				/* Save domain. */
				if (buffer_append_string (&session->domain, data + domain->domain_name) < 0) {
					buffer_free (&session->domain);

					/* Remove connection from epoll descriptor. */
					ev.events = 0;
					ev.data.u64 = 0;
					ev.data.fd = sd;
					epoll_ctl (relay->epoll_fd, EPOLL_CTL_DEL, sd, &ev);

					close (sd);

					break;
				}

				session->index = relay->nfds;
				session->sd = sd;
				session->dnscache = index;
			} else {
				session = &(relay->sessions[relay->index[k]]);
			}

			if (session_allocate_transactions (session) < 0) {
				session_free_transactions (session);
				buffer_free (&session->domain);

				session->dnscache = -1;

				/* Remove connection from epoll descriptor. */
				ev.events = 0;
				ev.data.u64 = 0;
				ev.data.fd = session->sd;
				epoll_ctl (relay->epoll_fd, EPOLL_CTL_DEL, session->sd, &ev);

				close (session->sd);
				session->sd = -1;

				session->index = -1;

				break;
			}

			transaction_list = &session->transaction_list;
			transaction = &(transaction_list->transactions[transaction_list->used]);

			buffer_init (&transaction->reverse_path, 32);
			if (buffer_append_string (&transaction->reverse_path, mail_transaction.reverse_path) < 0) {
				buffer_free (&transaction->reverse_path);
				session_free_transactions (session);
				buffer_free (&session->domain);

				session->dnscache = -1;

				/* Remove connection from epoll descriptor. */
				ev.events = 0;
				ev.data.u64 = 0;
				ev.data.fd = session->sd;
				epoll_ctl (relay->epoll_fd, EPOLL_CTL_DEL, session->sd, &ev);

				close (session->sd);
				session->sd = -1;

				session->index = -1;

				break;
			}

			/* For each forward path... */
			stringlist_init (&transaction->forward_paths);
			for (k = 0; k < domain->used; k++) {
				if (stringlist_insert_string (&transaction->forward_paths, data + domain->local_parts[k], 0) < 0) {
					stringlist_free (&transaction->forward_paths);
					buffer_free (&transaction->reverse_path);
					session_free_transactions (session);
					buffer_free (&session->domain);

					session->dnscache = -1;

					/* Remove connection from epoll descriptor. */
					ev.events = 0;
					ev.data.u64 = 0;
					ev.data.fd = session->sd;
					epoll_ctl (relay->epoll_fd, EPOLL_CTL_DEL, session->sd, &ev);

					close (session->sd);
					session->sd = -1;

					session->index = -1;

					break;
				}
			}

			/* If we couldn't allocate memory... */
			if (k != domain->used) {
				break;
			}

			transaction->fd = files.strings[i].data;
			transaction->message_offset = offset;
			transaction->offset = offset;
			transaction->filesize = filesize;

			transaction_list->used++;

			if (!already_connected) {
				session->last_read_write = server.current_time;

				relay->index[relay->nfds++] = session->sd;
			}
		}

		if (j != forward_paths->used) {
			mail_transaction_free (&mail_transaction);

			/* Remove sessions.*/
			for (j = 0; j < relay->nfds;) {
				remove_session (relay, relay->index[j]);
			}

			/* Close files. */
			for (j = 0; j < files.used; j++) {
				if (files.strings[j].data != -1) {
					close (files.strings[j].data);
				}
			}

			stringlist_free (&files);

			return -1;
		}

		mail_transaction_free (&mail_transaction);
	}

	if (relay->nfds > 0) {
		send_messages (relay);
	}

	/* TODO: Send a message to the reverse-path(s) in case we couldn't send some of the message(s). */

	/* Close and remove files. */
	for (i = 0; i < files.used; i++) {
		if (files.strings[i].data != -1) {
			close (files.strings[i].data);

			snprintf (oldpath, sizeof (oldpath), "%s/%.*s", server.relay_directory, files.strings[i].len, files.data + files.strings[i].string);
			unlink (oldpath);
		}
	}

	stringlist_free (&files);

	return 0;
}

int read_pre_header (input_stream_t *input_stream, mail_transaction_t *mail_transaction, off_t *offset)
{
	unsigned char *argument;
	unsigned char *local_part;
	size_t local_part_len;
	unsigned char *domain;
	size_t domainlen;
	size_t size;

	char line[TEXT_LINE_MAXLEN + 1];
	size_t len;

	int error;
	int ret;

	size = 0;
	error = 0;

	do {
		/* Read line. */
		if ((!input_stream_fgets (input_stream, line, sizeof (line), &len)) || (line[len - 1] != '\n')) {
			return -1;
		}

		size += len;

		/* End of pre-header? */
		if ((len == 1) || ((len == 2) && (line[0] == '\r'))) {
			/* If there is not reverse-path or no recipients... */
			if ((!mail_transaction->reverse_path[0]) || (mail_transaction->forward_paths.used == 0)) {
				return -1;
			}

			*offset = size;

			return 0;
		}

		/* Parse SMTP command. */
		if (((ret = parse_smtp_command ((unsigned char *) line, &argument, &error)) < 0) || (error != 0)) {
			return -1;
		}

		if ((eSmtpCommand) ret == MAIL) {
			/* If there was another "MAIL FROM: <reverse_path>" line... */
			if (mail_transaction->reverse_path[0]) {
				return -1;
			}

			/* Valid reverse path? */
			if (parse_reverse_path (argument, &local_part, &local_part_len, &domain, &domainlen, NULL, NULL) < 0) {
				return -1;
			}

			/* Null reverse path? */
			if (!local_part) {
				memcpy (mail_transaction->reverse_path, "<>", 3);
			} else {
				mail_transaction_set_reverse_path (mail_transaction, (const char *) local_part, local_part_len, (const char *) domain, domainlen);
			}
		} else if ((eSmtpCommand) ret == RCPT) {
			/* Valid forward path? */
			if (parse_forward_path (argument, &local_part, &local_part_len, &domain, &domainlen) < 0) {
				return -1;
			}

			local_part[local_part_len] = 0;
			domain[domainlen] = 0;

			if (mail_transaction_add_forward_path (mail_transaction, (const char *) local_part, local_part_len, (const char *) domain, domainlen) < 0) {
				return -1;
			}
		} else {
			return -1;
		}
	} while (1);
}

int connect_to_smtp_server (dnscache_entry_t *dnscache_entry)
{
	rr_t *rr_list;
	struct sockaddr_in addr;
	struct hostent *host;
	int sd;
	int flags;
	size_t i;

	sd = socket (PF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		return -1;
	}

	/* Make socket non-blocking. */
	flags = fcntl (sd, F_GETFL);
	flags |= O_NONBLOCK;
	if (fcntl (sd, F_SETFL, flags) < 0) {
		close (sd);
		return -1;
	}

	/* Disable the Nagle algorithm. */
	flags = 1;
	setsockopt (sd, IPPROTO_TCP, TCP_NODELAY, (const char *) &flags, sizeof (flags));

	addr.sin_port = htons (SMTP_DEFAULT_PORT);
	memset (&addr.sin_zero, 0, 8);

	/* Connect. */
	rr_list = dnscache_entry->rr_list;
	for (i = 0; i < dnscache_entry->nhosts; i++) {
		if (rr_list[i].type == T_A) {
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = rr_list[i].u.addr;
		} else {
			host = gethostbyname (rr_list[i].u.name.data);
			if (!host) {
				continue;
			}

			addr.sin_family = host->h_addrtype;
			memcpy (&addr.sin_addr, host->h_addr_list[0], host->h_length);
		}

		if ((connect (sd, (struct sockaddr *) &addr, sizeof (struct sockaddr)) < 0) && (errno != EINPROGRESS)) {
			/* Try with next host of rr list. */
			continue;
		}

		/* Connected. */
		return sd;
	}

	/* We couldn't connect. */
	close (sd);

	return -1;
}

void remove_session (relay_t *relay, int client)
{
	struct epoll_event ev;
	session_t *session;
	int index;

	/* Remove session from epoll descriptor. */
	ev.events = 0;
	ev.data.u64 = 0;
	ev.data.fd = client;
	epoll_ctl (relay->epoll_fd, EPOLL_CTL_DEL, client, &ev);

	session = &(relay->sessions[client]);

	/* Save position of session in index array. */
	index = session->index;

	session_reset (session);

	relay->nfds--;

	/* If there are more sessions... */
	if (index < relay->nfds) {
		relay->index[index] = relay->index[relay->nfds];
		relay->sessions[relay->index[index]].index = index;
	}

#if DEBUG
	fprintf (stdout, "# sessions: %d.\n", relay->nfds);
#endif
}

int send_messages (relay_t *relay)
{
	int epoll_fd;
	struct epoll_event *events;
	struct epoll_event event;
	int maxevents;
	int nfds;
	time_t now;
	session_t **interrupted_sessions;
	session_t *session;
	int i;

	epoll_fd = relay->epoll_fd;
	events = relay->events;
	maxevents = relay->max_file_descriptors;
	interrupted_sessions = relay->interrupted_sessions;

	while ((server.running) && (relay->nfds > 0)) {
		if (server.handle_alarm) {
			now = time (NULL);
			if (server.current_time != now) {
				/* Save current time. */
				server.current_time = now;
				gmtime_r (&now, &server.stm);

				/* Check whether some session has been idle for a very long time. */
				i = 0;
				while (i < relay->nfds) {
					session = &(relay->sessions[relay->index[i]]);
					if (now - session->last_read_write > server.max_idle_time) {
#if DEBUG
						printf ("Removing session with socket %d for having been idle for a very long time (%lu seconds).\n", session->sd, now - session->last_read_write);
#endif

						remove_session (relay, session->sd);
					} else {
						i++;
					}
				}
			}

			server.handle_alarm = 0;
		}

		nfds = epoll_wait (epoll_fd, events, maxevents, -1);

		/* For each event... */
		for (i = 0; i < nfds; i++) {
			if (handle_session (&(relay->sessions[events[i].data.fd]), &(events[i])) < 0) {
				remove_session (relay, events[i].data.fd);
			}
		}

		/* Handle interrupted sessions. */
		i = 0;
		while (i < relay->number_interrupted_sessions) {
			event.events = EPOLLIN | EPOLLOUT;
			if (handle_session (interrupted_sessions[i], &event) < 0) {
				remove_session (relay, interrupted_sessions[i]->sd);
			}

			if (relay->number_interrupted_sessions > 1) {
				interrupted_sessions[0] = interrupted_sessions[relay->number_interrupted_sessions - 1];
			}

			relay->number_interrupted_sessions--;
		}
	}

	return 0;
}
