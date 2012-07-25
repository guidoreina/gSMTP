#ifndef SERVER_H
#define SERVER_H

#include "connection.h"
#include "domainlist.h"
#include "ip_list.h"

typedef struct {
	domainlist_t domainlist;
	ip_list_t ip_list; /* List of IPs that can do relay. */

	int port; /* Port to bind to. */
	int listener; /* Listener socket. */
	int epoll_fd; /* epoll file descriptor. */

	int running;

	int max_file_descriptors;
	unsigned nfds; /* # of file descriptors. */

	int *index;
	struct epoll_event *events;

	connection_t *connections;

	connection_t **interrupted_connections;
	size_t number_interrupted_connections;

	time_t current_time;
	struct tm stm;
	struct tm local_time;

	size_t nfile;

	int handle_alarm;

	time_t max_idle_time;

	const char *domains_directory;
	const char *incoming_directory;
	const char *received_directory;
	const char *relay_directory;
	const char *error_directory;

	size_t max_recipients;
	size_t max_message_size;
	size_t max_transactions;

	int log_mails;
	const char *logfile;
	int log_fd;
	buffer_t logbuffer;

	const char *user;

	struct {
		unsigned char local_part[LOCAL_PART_MAXLEN + 1];
		size_t local_part_len;
		unsigned char domain[DOMAIN_MAXLEN + 1];
		size_t domainlen;
	} postmaster;

	pid_t receiver_pid;
	pid_t delivery_pid;
	pid_t relay_pid;
} server_t;

int create_server (server_t *server, unsigned short port);
int delete_server (server_t *server);

void start_server (server_t *server);
int stop_server (server_t *server);

#endif /* SERVER_H */
