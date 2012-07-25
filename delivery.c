#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include "delivery.h"
#include "server.h"
#include "configuration.h"
#include "parser.h"
#include "switch_to_user.h"
#include "relay.h"

#define DELIVER_EVERY     5 /* seconds. */
#define MESSAGE_EXTENSION ".eml"

extern server_t server;
extern configuration_t conf;
extern configuration_t mime_types;

static void stop (int nsignal);
static void handle_sigusr1 (int nsignal);

static int deliver (void);
static int deliver_mail (const char *path, const char *filename);
static int read_pre_header (input_stream_t *input_stream, mail_transaction_t *delivery, mail_transaction_t *relay);

static int open_files (size_t nfds, int *fd_vector, mail_transaction_t *delivery, mail_transaction_t *relay, const char *filename);
static void close_and_remove_files (size_t nfds, int *fd_vector, mail_transaction_t *delivery, mail_transaction_t *relay, const char *filename);

static int write_relay_pre_header (int fd, const char *reverse_path, mail_transaction_t *relay);
static int copy_file_to_recipients (input_stream_t *input_stream, size_t nfds, int *fd_vector);

void deliver_loop (void)
{
	struct sigaction act;

	/* Am I root? */
	if ((getuid () == 0) || (geteuid () == 0)) {
		if ((!server.user) || (!server.user[0])) {
			domainlist_free (&server.domainlist);

			configuration_free (&conf);
			configuration_free (&mime_types);

			fprintf (stderr, "A user must be defined when running as root.\n");
			exit (-1);
		}

		if (switch_to_user (server.user) < 0) {
			domainlist_free (&server.domainlist);

			configuration_free (&conf);
			configuration_free (&mime_types);

			exit (-1);
		}
	}

	/* Create relay process. */
	if ((server.relay_pid = fork ()) < 0) {
		perror ("fork");

		domainlist_free (&server.domainlist);

		configuration_free (&conf);
		configuration_free (&mime_types);

		exit (-1);
	}

	/* If I am the relay process... */
	if (server.relay_pid == 0) {
		relay_loop ();
	}

	/* Install signal handlers. */
	sigemptyset (&act.sa_mask);
	act.sa_flags = 0;

	act.sa_handler = stop;
	sigaction (SIGINT, &act, NULL);
	sigaction (SIGTERM, &act, NULL);

	act.sa_handler = handle_sigusr1;
	sigaction (SIGUSR1, &act, NULL);

	server.running = 1;

	do {
		if (deliver () < 0) {
			break;
		}

		sleep (DELIVER_EVERY);

		if (server.running) {
			/* If the parent process is not alive... */
			if (kill (server.receiver_pid, 0) < 0) {
				break;
			}
		} else {
			break;
		}
	} while (1);

	domainlist_free (&server.domainlist);

	configuration_free (&conf);
	configuration_free (&mime_types);

	exit (0);
}

void stop (int nsignal)
{
	printf ("Delivery process received signal %d.\nStopping...\n", nsignal);

	server.running = 0;

	/* Wait for relay process. */
	waitpid (server.relay_pid, NULL, 0);
}

void handle_sigusr1 (int nsignal)
{
	/* We got mail. */
}

int deliver (void)
{
	DIR *delivery;
	struct dirent *mail;
	char oldpath[PATH_MAX + 1];
	char newpath[PATH_MAX + 1];
	size_t len;

	delivery = opendir (server.received_directory);
	if (!delivery) {
		fprintf (stderr, "Couldn't open received directory %s.\n", server.received_directory);
		return -1;
	}

	/* For each message to deliver/relay... */
	while ((mail = readdir (delivery)) != NULL) {
		if (mail->d_name[0] == '.') {
			continue;
		}

		/* If not a message... */
		len = strlen (mail->d_name);
		if ((len < sizeof (MESSAGE_EXTENSION)) || (strcmp (mail->d_name + len - (sizeof (MESSAGE_EXTENSION) - 1), MESSAGE_EXTENSION) != 0)) {
			continue;
		}

		snprintf (oldpath, sizeof (oldpath), "%s/%s", server.received_directory, mail->d_name);

		if (deliver_mail (oldpath, mail->d_name) < 0) {
			/* Move mail to error directory. */
			snprintf (newpath, sizeof (newpath), "%s/%s", server.error_directory, mail->d_name);
			rename (oldpath, newpath);
		} else {
			unlink (oldpath);
		}
	}

	closedir (delivery);

	return 0;
}

int deliver_mail (const char *path, const char *filename)
{
	input_stream_t input_stream;
	mail_transaction_t delivery;
	mail_transaction_t relay;
	domainlist_t *forward_paths;
	int fd; /* Input file. */
	int *fd_vector;
	size_t nfds;
	size_t i;

	/* Open received message. */
	fd = open (path, O_RDONLY);
	if (fd < 0) {
		fprintf (stderr, "Couldn't open message file %s.\n", path);
		return -1;
	}

	/* Open stream. */
	if (input_stream_fdopen (&input_stream, fd, 16 * 1024) < 0) {
		/* Couldn't allocate memory. */
		close (fd);
		return -1;
	}

	mail_transaction_init (&delivery);
	mail_transaction_init (&relay);

	/* Read message pre-header. */
	if (read_pre_header (&input_stream, &delivery, &relay) < 0) {
		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* If there is not reverse-path. */
	if (!delivery.reverse_path[0]) {
		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* Compute how many files we will have to generate. */
	forward_paths = &delivery.forward_paths;
	nfds = 0;

	for (i = 0; i < forward_paths->used; i++) {
		nfds += forward_paths->records[i].used;
	}

	if (relay.forward_paths.used > 0) {
		nfds++;
	}

	/* If there are not recipients... */
	if (nfds == 0) {
		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* Allocate memory for file descriptors. */
	fd_vector = (int *) malloc (nfds * sizeof (int));
	if (!fd_vector) {
		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* Initialize file descriptors. */
	for (i = 0; i < nfds; i++) {
		fd_vector[i] = -1;
	}

	/* Open output files. */
	if (open_files (nfds, fd_vector, &delivery, &relay, filename) < 0) {
		close_and_remove_files (nfds, fd_vector, &delivery, &relay, filename);
		free (fd_vector);

		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* If we have to relay... */
	if (relay.forward_paths.used > 0) {
		/* Write pre-header. */
		if (write_relay_pre_header (fd_vector[nfds - 1], delivery.reverse_path, &relay) < 0) {
			close_and_remove_files (nfds, fd_vector, &delivery, &relay, filename);
			free (fd_vector);

			mail_transaction_free (&delivery);
			mail_transaction_free (&relay);

			input_stream_fclose (&input_stream);
			return -1;
		}
	}

	/* Copy message to recipients. */
	if (copy_file_to_recipients (&input_stream, nfds, fd_vector) < 0) {
		close_and_remove_files (nfds, fd_vector, &delivery, &relay, filename);
		free (fd_vector);

		mail_transaction_free (&delivery);
		mail_transaction_free (&relay);

		input_stream_fclose (&input_stream);
		return -1;
	}

	/* Close files. */
	for (i = 0; i < nfds; i++) {
		close (fd_vector[i]);
	}

	free (fd_vector);

	mail_transaction_free (&delivery);
	mail_transaction_free (&relay);

	input_stream_fclose (&input_stream);

	return 0;
}

int read_pre_header (input_stream_t *input_stream, mail_transaction_t *delivery, mail_transaction_t *relay)
{
	mail_transaction_t *mail_transaction;

	unsigned char *argument;
	unsigned char *local_part;
	size_t local_part_len;
	unsigned char *domain;
	size_t domainlen;

	char line[TEXT_LINE_MAXLEN + 1];
	size_t len;

	int error;
	int ret;

	error = 0;

	do {
		/* Read line. */
		if ((!input_stream_fgets (input_stream, line, sizeof (line), &len)) || (line[len - 1] != '\n')) {
			return -1;
		}

		/* End of pre-header? */
		if ((len == 1) || ((len == 2) && (line[0] == '\r'))) {
			return 0;
		}

		/* Parse SMTP command. */
		if (((ret = parse_smtp_command ((unsigned char *) line, &argument, &error)) < 0) || (error != 0)) {
			return -1;
		}

		if ((eSmtpCommand) ret == MAIL) {
			/* If there was another "MAIL FROM: <reverse_path>" line... */
			if (delivery->reverse_path[0]) {
				return -1;
			}

			/* Valid reverse path? */
			if (parse_reverse_path (argument, &local_part, &local_part_len, &domain, &domainlen, NULL, NULL) < 0) {
				return -1;
			}

			/* Null reverse path? */
			if (!local_part) {
				memcpy (delivery->reverse_path, "<>", 3);
			} else {
				mail_transaction_set_reverse_path (delivery, (const char *) local_part, local_part_len, (const char *) domain, domainlen);
			}
		} else if ((eSmtpCommand) ret == RCPT) {
			/* Valid forward path? */
			if (parse_forward_path (argument, &local_part, &local_part_len, &domain, &domainlen) < 0) {
				return -1;
			}

			local_part[local_part_len] = 0;
			domain[domainlen] = 0;

			/* If I know nothing about: <local_part>@<domain>... */
			if (domainlist_search (&server.domainlist, (const char *) local_part, (const char *) domain) < 0) {
				mail_transaction = relay;
			} else {
				mail_transaction = delivery;
			}

			if (mail_transaction_add_forward_path (mail_transaction, (const char *) local_part, local_part_len, (const char *) domain, domainlen) < 0) {
				return -1;
			}
		} else {
			return -1;
		}
	} while (1);
}

int open_files (size_t nfds, int *fd_vector, mail_transaction_t *delivery, mail_transaction_t *relay, const char *filename)
{
	domainlist_t *forward_paths;
	domain_t *domain;
	char *data;

	char path[PATH_MAX + 1];
	size_t idx;
	size_t i, j;

	/* Open output files. */
	forward_paths = &delivery->forward_paths;
	data = forward_paths->data.data;

	idx = 0;

	/* Open files for local delivery. */
	for (i = 0; i < forward_paths->used; i++) {
		domain = &(forward_paths->records[i]);
		for (j = 0; j < domain->used; j++) {
			/* Open file. */
			snprintf (path, sizeof (path), "%s/%s/%s/%s", server.domains_directory, data + domain->domain_name, data + domain->local_parts[j], filename);
			fd_vector[idx] = open (path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			if (fd_vector[idx] < 0) {
				return -1;
			}

			idx++;
		}
	}

	/* If we have to relay... */
	if (relay->forward_paths.used > 0) {
		/* Open file for relay. */
		snprintf (path, sizeof (path), "%s/%s", server.relay_directory, filename);
		fd_vector[idx] = open (path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		if (fd_vector[idx] < 0) {
			return -1;
		}
	}

	return 0;
}

void close_and_remove_files (size_t nfds, int *fd_vector, mail_transaction_t *delivery, mail_transaction_t *relay, const char *filename)
{
	domainlist_t *forward_paths;
	domain_t *domain;
	char *data;

	char path[PATH_MAX + 1];
	size_t i, j;

	for (i = 0; i < nfds; i++) {
		if (fd_vector[i] != -1) {
			/* Close file. */
			close (fd_vector[i]);
		}
	}

	forward_paths = &delivery->forward_paths;
	data = forward_paths->data.data;

	for (i = 0; i < forward_paths->used; i++) {
		domain = &(forward_paths->records[i]);
		for (j = 0; j < domain->used; j++) {
			/* Remove file. */
			snprintf (path, sizeof (path), "%s/%s/%s/%s", server.domains_directory, data + domain->domain_name, data + domain->local_parts[j], filename);
			unlink (path);
		}
	}

	/* If we have to relay... */
	if (relay->forward_paths.used > 0) {
		/* Remove file. */
		snprintf (path, sizeof (path), "%s/%s", server.relay_directory, filename);
		unlink (path);
	}
}

int write_relay_pre_header (int fd, const char *reverse_path, mail_transaction_t *relay)
{
	domainlist_t *forward_paths;
	domain_t *domain;
	char *data;

	buffer_t buffer;
	size_t i, j;

	buffer_init (&buffer, 512);

	/* Add reverse path to the pre-header. */
	if (buffer_format (&buffer, "MAIL FROM: %s\r\n", reverse_path) < 0) {
		/* Couldn't allocate memory. */
		buffer_free (&buffer);
		return -1;
	}

	forward_paths = &relay->forward_paths;
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

	/* Write buffer to disk. */
	if (write (fd, buffer.data, buffer.used) != buffer.used) {
		/* Couldn't write. */
		buffer_free (&buffer);
		return -1;
	}

	buffer_free (&buffer);

	return 0;
}

int copy_file_to_recipients (input_stream_t *input_stream, size_t nfds, int *fd_vector)
{
	char buffer[20 * 1024];
	size_t bytes;
	size_t i;

	do {
		/* Read chunk from input stream... */
		bytes = input_stream_fread (input_stream, buffer, sizeof (buffer));
		if (bytes <= 0) {
			break;
		}

		/* and write it to the recipients. */
		for (i = 0; i < nfds; i++) {
			if (write (fd_vector[i], buffer, bytes) != bytes) {
				return -1;
			}
		}
	} while (1);

	return 0;
}
