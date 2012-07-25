#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include "server.h"
#include "handle_connection.h"
#include "delivery.h"
#include "switch_to_user.h"
#include "configuration.h"

#define BACKLOG 200

extern configuration_t conf;

static int set_max_file_descriptors (void);
static int create_listener_socket (unsigned short port);
static int setnonblocking (int fd);
static int create_connections (server_t *server);
static int create_connection (server_t *server, int client, struct sockaddr_in *sin);
static void remove_connection (server_t *server, int client);

int create_server (server_t *server, unsigned short port)
{
	struct epoll_event ev;

	domainlist_init (&server->domainlist);
	ip_list_init (&server->ip_list);
	server->port = port;
	server->listener = -1;
	server->epoll_fd = -1;
	server->running = 0;
	server->nfds = 0;
	server->index = NULL;
	server->events = NULL;
	server->connections = NULL;
	server->interrupted_connections = NULL;
	server->number_interrupted_connections = 0;
	server->current_time = 0;
	server->handle_alarm = 0;
	server->log_fd = -1;
	buffer_init (&server->logbuffer, 512);

	/* Load domain list. */
	if (domainlist_load (&server->domainlist, server->domains_directory) < 0) {
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't load host list.\n");
		return -1;
	}

	/* Get receiver's pid. */
	server->receiver_pid = getpid ();

	/* Create delivery process. */
	if ((server->delivery_pid = fork ()) < 0) {
		perror ("fork");

		domainlist_free (&server->domainlist);

		return -1;
	}

	/* If I am the delivery process... */
	if (server->delivery_pid == 0) {
		server->delivery_pid = getpid ();
		deliver_loop ();
	}

	/* Load IP list. */
	if (ip_list_load (&server->ip_list, &conf) < 0) {
		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't load IP list.\n");
		return -1;
	}

	if (server->log_mails) {
		server->log_fd = open (server->logfile,  O_CREAT | O_WRONLY | O_APPEND | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (server->log_fd < 0) {
			ip_list_free (&server->ip_list);
			domainlist_free (&server->domainlist);

			fprintf (stderr, "Couldn't open log file.\n");
			return -1;
		}
	}

	/* Set the maximum number of file descriptors. */
	if ((server->max_file_descriptors = set_max_file_descriptors ()) < 0) {
		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't set the soft limit of the maximum number of file descriptors.\n");
		return -1;
	}

	/* Create listener socket. */
	if ((server->listener = create_listener_socket (server->port)) < 0) {
		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't create listener socket on port: %u.\n", server->port);
		return -1;
	}

	/* Am I root? */
	if ((getuid () == 0) || (geteuid () == 0)) {
		if ((!server->user) || (!server->user[0])) {
			close (server->listener);
			server->listener = -1;

			if (server->log_fd != -1) {
				close (server->log_fd);
				server->log_fd = -1;
			}

			ip_list_free (&server->ip_list);
			domainlist_free (&server->domainlist);

			fprintf (stderr, "A user must be defined when running as root.\n");
			return -1;
		}

		if (switch_to_user (server->user) < 0) {
			close (server->listener);
			server->listener = -1;

			if (server->log_fd != -1) {
				close (server->log_fd);
				server->log_fd = -1;
			}

			ip_list_free (&server->ip_list);
			domainlist_free (&server->domainlist);

			return -1;
		}
	}

	/* Open epoll file descriptor. */
	if ((server->epoll_fd = epoll_create (server->max_file_descriptors)) < 0) {
		perror ("epoll_create");

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		return -1;
	}

	/* Add listener to the epoll descriptor. */
	ev.events = EPOLLIN;
	ev.data.u64 = 0;
	ev.data.fd = server->listener;
	if (epoll_ctl (server->epoll_fd, EPOLL_CTL_ADD, server->listener, &ev) < 0) {
		perror ("epoll_ctl");

		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		return -1;
	}

	/* Allocate memory for index. */
	server->index = (int *) malloc (server->max_file_descriptors * sizeof (int));
	if (!server->index) {
		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't allocate memory for index.\n");
		return -1;
	}

	/* Allocate memory for epoll events. */
	server->events = (struct epoll_event *) malloc (server->max_file_descriptors * sizeof (struct epoll_event));
	if (!server->events) {
		free (server->index);
		server->index = NULL;

		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't allocate memory for epoll events.\n");
		return -1;
	}

	/* Allocate memory for connections. */
	server->connections = (connection_t *) malloc (server->max_file_descriptors * sizeof (connection_t));
	if (!server->connections) {
		free (server->events);
		server->events = NULL;

		free (server->index);
		server->index = NULL;

		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't allocate memory for connections.\n");
		return -1;
	}

	/* Allocate memory for interrupted connections. */
	server->interrupted_connections = (connection_t **) malloc (server->max_file_descriptors * sizeof (connection_t *));
	if (!server->interrupted_connections) {
		free (server->connections);
		server->connections = NULL;

		free (server->events);
		server->events = NULL;

		free (server->index);
		server->index = NULL;

		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't allocate memory for interrupted connections.\n");
		return -1;
	}

	if (create_connections (server) < 0) {
		free (server->interrupted_connections);
		server->interrupted_connections = NULL;

		free (server->connections);
		server->connections = NULL;

		free (server->events);
		server->events = NULL;

		free (server->index);
		server->index = NULL;

		close (server->epoll_fd);
		server->epoll_fd = -1;

		close (server->listener);
		server->listener = -1;

		if (server->log_fd != -1) {
			close (server->log_fd);
			server->log_fd = -1;
		}

		ip_list_free (&server->ip_list);
		domainlist_free (&server->domainlist);

		fprintf (stderr, "Couldn't allocate memory for connections.\n");
		return -1;
	}

	return 0;
}

int delete_server (server_t *server)
{
	int i;

	if (server->running) {
		if (stop_server (server) < 0) {
			fprintf (stderr, "Couldn't stop server.\n");
			return -1;
		}
	}

	domainlist_free (&server->domainlist);
	ip_list_free (&server->ip_list);

	if (server->log_fd != -1) {
		close (server->log_fd);
		server->log_fd = -1;
	}

	if (server->listener != -1) {
		close (server->listener);
		server->listener = -1;
	}

	if (server->epoll_fd != -1) {
		close (server->epoll_fd);
		server->epoll_fd = -1;
	}

	server->nfds = 0;

	if (server->index) {
		free (server->index);
		server->index = NULL;
	}

	if (server->events) {
		free (server->events);
		server->events = NULL;
	}

	if (server->connections) {
		for (i = 0; i < server->max_file_descriptors; i++) {
			connection_free (&(server->connections[i]));
		}

		free (server->connections);
		server->connections = NULL;
	}

	if (server->interrupted_connections) {
		free (server->interrupted_connections);
		server->interrupted_connections = NULL;
	}

	server->number_interrupted_connections = 0;

	server->current_time = 0;
	server->handle_alarm = 0;

	buffer_free (&server->logbuffer);

	return 0;
}

void start_server (server_t *server)
{
	int epoll_fd;
	struct epoll_event *events;
	struct epoll_event event;
	int maxevents;
	int listener;
	int nfds;
	int client;
	struct sockaddr_in sin;
	socklen_t addrlen;
	time_t now;
	connection_t **interrupted_connections;
	connection_t *connection;
	int i;

#if DEBUG
	char peer[20];
#endif

	server->running = 1;

	epoll_fd = server->epoll_fd;
	events = server->events;
	maxevents = server->max_file_descriptors;
	listener = server->listener;
	interrupted_connections = server->interrupted_connections;

	/* Save current time. */
	server->current_time = time (NULL);
	gmtime_r (&server->current_time, &server->stm);

	if (server->log_mails) {
		localtime_r (&server->current_time, &server->local_time);
	}

	while (server->running) {
		if (server->handle_alarm) {
			now = time (NULL);
			if (server->current_time != now) {
				/* Save current time. */
				server->current_time = now;
				gmtime_r (&now, &server->stm);

				if (server->log_mails) {
					localtime_r (&now, &server->local_time);
				}

				/* Check whether some connection has been idle for a very long time. */
				i = 0;
				while (i < server->nfds) {
					connection = &(server->connections[server->index[i]]);
					if (now - connection->last_read_write > server->max_idle_time) {
#if DEBUG
						printf ("Removing connection with socket %d for having been idle for a very long time (%lu seconds).\n", connection->sd, now - connection->last_read_write);
#endif

						remove_connection (server, connection->sd);
					} else {
						i++;
					}
				}
			}

			server->handle_alarm = 0;
		}

		nfds = epoll_wait (epoll_fd, events, maxevents, -1);

		/* For each event... */
		for (i = 0; i < nfds; i++) {
			/* New connection? */
			if (events[i].data.fd == listener) {
				addrlen = sizeof (struct sockaddr);
				client = accept (listener, (struct sockaddr *) &sin, &addrlen);
				if (client < 0) {
					perror ("accept");
					continue;
				}

#if DEBUG
				if (inet_ntop (AF_INET, &sin.sin_addr, peer, sizeof (peer))) {
					fprintf (stdout, "Connection from: [%s:%u].\n", peer, ntohs (sin.sin_port));
				}
#endif /* DEBUG */

				if (create_connection (server, client, &sin) < 0) {
					close (client);
					break;
				}
			} else {
				if (handle_connection (&(server->connections[events[i].data.fd]), &(events[i])) < 0) {
					remove_connection (server, events[i].data.fd);
				}
			}
		}

		/* Handle interrupted connections. */
		i = 0;
		while (i < server->number_interrupted_connections) {
			event.events = EPOLLIN | EPOLLOUT;
			if (handle_connection (interrupted_connections[i], &event) < 0) {
				remove_connection (server, interrupted_connections[i]->sd);
			}

			if (server->number_interrupted_connections > 1) {
				interrupted_connections[0] = interrupted_connections[server->number_interrupted_connections - 1];
			}

			server->number_interrupted_connections--;
		}
	}
}

int stop_server (server_t *server)
{
	server->running = 0;

	return 0;
}

int set_max_file_descriptors (void)
{
	struct rlimit rlim;

	/* Get the hard limit of the maximum number of file descriptors. */
	if (getrlimit (RLIMIT_NOFILE, &rlim) < 0) {
		perror ("getrlimit");
		return -1;
	}

#if DEBUG
	fprintf (stdout, "Hard limit of the maximum number of file descriptors: %lu, soft limit: %lu.\n", rlim.rlim_max, rlim.rlim_cur);
#endif

	/* Set the soft limit of the maximum number of file descriptors. */
	rlim.rlim_cur = rlim.rlim_max;
	if (setrlimit (RLIMIT_NOFILE, &rlim) < 0) {
		perror ("setrlimit");
		return -1;
	}

	return rlim.rlim_cur;
}

int create_listener_socket (unsigned short port)
{
	int sd;
	int option = 1;
	struct sockaddr_in sin;

	/* Create socket. */
	if ((sd = socket (PF_INET, SOCK_STREAM, 0)) < 0) {
		perror ("socket");
		return -1;
	}

	/* Make listener socket non-blocking. */
	if (setnonblocking (sd) < 0) {
		close (sd);

		fprintf (stderr, "Couldn't make listener socket non-blocking.\n");
		return -1;
	}

	/* Reuse address. */
	if (setsockopt (sd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof (int)) < 0) {
		perror ("setsockopt");

		close (sd);
		return -1;
	}

	/* Fill struct sockaddr_in. */
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = htonl (INADDR_ANY);
	sin.sin_port = htons (port);
	memset (&(sin.sin_zero), 0, 8);

	/* Bind. */
	if (bind (sd, (struct sockaddr *) &sin, sizeof (struct sockaddr)) < 0) {
		perror ("bind");

		close (sd);
		return -1;
	}

	/* Listen. */
	if (listen (sd, BACKLOG) < 0) {
		perror ("listen");

		close (sd);
		return -1;
	}

	return sd;
}

int setnonblocking (int fd)
{
	int flags;

	flags = fcntl (fd, F_GETFL);
	flags |= O_NONBLOCK;
	if (fcntl (fd, F_SETFL, flags) < 0) {
		perror ("fcntl");
		return -1;
	}

	return 0;
}

int create_connections (server_t *server)
{
	size_t i, j;

	for (i = 0; i < server->max_file_descriptors; i++) {
		if (connection_init (&(server->connections[i]), 2 * TEXT_LINE_MAXLEN) < 0) {
			/* Couldn't allocate memory. */
			for (j = 0; j < i; j++) {
				free (server->connections[j].input_stream.buf_base);
			}

			return -1;
		}
	}

	return 0;
}

int create_connection (server_t *server, int client, struct sockaddr_in *sin)
{
	struct epoll_event ev;
	int flags;

	setnonblocking (client);

	/* Disable the Nagle algorithm. */
	flags = 1;
	setsockopt (client, IPPROTO_TCP, TCP_NODELAY, (const char *) &flags, sizeof (flags));

	/* Add connection to epoll descriptor. */

	/*
	 * man epoll:
	 * When used as an Edge triggered interface, for performance reasons, it is possible to add the file
	 * descriptor inside the epoll interface (EPOLL_CTL_ADD) once by specifying (EPOLLIN|EPOLLOUT). This
	 * allows you to avoid continuously switching between EPOLLIN and EPOLLOUT calling epoll_ctl(2) with
	 * EPOLL_CTL_MOD.
	 *
	 */

#if !HAVE_EPOLLRDHUP
	ev.events = EPOLLOUT;
#else
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
#endif

	ev.data.u64 = 0;
	ev.data.fd = client;
	if (epoll_ctl (server->epoll_fd, EPOLL_CTL_ADD, client, &ev) < 0) {
		perror ("epoll_ctl");
		return -1;
	}

	server->index[server->nfds] = client;

	server->connections[client].index = server->nfds;
	server->connections[client].sd = client;
	memcpy (&(server->connections[client].sin), sin, sizeof (struct sockaddr_in));

	server->connections[client].last_read_write = server->current_time;

	server->nfds++;

#if DEBUG
	fprintf (stdout, "# connections: %d.\n", server->nfds);
#endif

	return 0;
}

void remove_connection (server_t *server, int client)
{
	struct epoll_event ev;
	connection_t *connection;
	int index;

	/* Remove connection from epoll descriptor. */
	ev.events = 0;
	ev.data.u64 = 0;
	ev.data.fd = client;
	epoll_ctl (server->epoll_fd, EPOLL_CTL_DEL, client, &ev);

	connection = &(server->connections[client]);

	/* Save position of connection in index array. */
	index = connection->index;

	connection_reset (connection);

	server->nfds--;

	/* If there are more connections... */
	if (index < server->nfds) {
		/* place last connection in the slot we have just freed. */
		server->index[index] = server->index[server->nfds];
		server->connections[server->index[index]].index = index;
	}

#if DEBUG
	fprintf (stdout, "# connections: %d.\n", server->nfds);
#endif
}
