#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "server.h"
#include "configuration.h"
#include "parser.h"
#include "dnscache.h"

#define CONFIG_FILE     "SmtpServer.conf"
#define MIME_TYPES_FILE "mime.conf"

#define MAX_IDLE_TIME   300

static void stop (int nsignal);
static void handle_alarm (int nsignal);

server_t server;
configuration_t conf;
configuration_t mime_types;
dnscache_t dnscache;

int main ()
{
	struct sigaction act;
	struct itimerval interval;
	struct stat buf;
	const char *string;
	int port;
	int max_idle_time;
	size_t max_recipients;
	size_t max_message_size;
	size_t max_transactions;
	char filename[PATH_MAX + 1];
	unsigned char *local_part;
	size_t local_part_len;
	unsigned char *domain;
	size_t domainlen;

	/* Initialize configuration. */
	configuration_init (&conf, 1);
	configuration_init (&mime_types, 1);

	dnscache_init (&dnscache);

	/* Load configuration. */
	if (configuration_load (&conf, CONFIG_FILE) < 0) {
		configuration_free (&conf);

		fprintf (stderr, "Couldn't load config file %s.\n", CONFIG_FILE);
		return -1;
	}

	/* Load MIME types. */
	if (configuration_load (&mime_types, MIME_TYPES_FILE) < 0) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "Couldn't load MIME types file %s.\n", MIME_TYPES_FILE);
		return -1;
	}

	/* Get port to bind to. */
	string = configuration_get_value (&conf, "General", "Port", NULL);
	if (!string) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "Couldn't get port to bind to.\n");
		return -1;
	}

	port = atoi (string);
	if ((port < 1) || (port > 65535)) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "Port to bind to (%s) out of range (1 <= port <= 65535).\n", string);
		return -1;
	}

	/* Get directory where the domains are. */
	server.domains_directory = configuration_get_value (&conf, "General", "DomainsDirectory", NULL);
	if ((!server.domains_directory) || (!server.domains_directory[0])) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify the directory which contains the domains.\n");
		return -1;
	}

	if (stat (server.domains_directory, &buf) < 0) {
		fprintf (stderr, "The directory %s doesn't exist.\n", server.domains_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		fprintf (stderr, "%s is not a directory but a file.\n", server.domains_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	/* Get directory where incoming messages will be stored. */
	server.incoming_directory = configuration_get_value (&conf, "General", "IncomingDirectory", NULL);
	if ((!server.incoming_directory) || (!server.incoming_directory[0])) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify where the incoming messages will be stored.\n");
		return -1;
	}

	if (stat (server.incoming_directory, &buf) < 0) {
		fprintf (stderr, "The directory %s doesn't exist.\n", server.incoming_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		fprintf (stderr, "%s is not a directory but a file.\n", server.incoming_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	/* Get directory where received messages will be stored. */
	server.received_directory = configuration_get_value (&conf, "General", "ReceivedDirectory", NULL);
	if ((!server.received_directory) || (!server.received_directory[0])) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify where the received messages will be stored.\n");
		return -1;
	}

	if (stat (server.received_directory, &buf) < 0) {
		fprintf (stderr, "The directory %s doesn't exist.\n", server.received_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		fprintf (stderr, "%s is not a directory but a file.\n", server.received_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	/* Get directory where messages to be relayed will be stored. */
	server.relay_directory = configuration_get_value (&conf, "General", "RelayDirectory", NULL);
	if ((!server.relay_directory) || (!server.relay_directory[0])) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify where the messages to be relayed will be stored.\n");
		return -1;
	}

	if (stat (server.relay_directory, &buf) < 0) {
		fprintf (stderr, "The directory %s doesn't exist.\n", server.relay_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		fprintf (stderr, "%s is not a directory but a file.\n", server.relay_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	/* Get directory where messages will be stored when delivery/relay fails. */
	server.error_directory = configuration_get_value (&conf, "General", "ErrorDirectory", NULL);
	if ((!server.error_directory) || (!server.error_directory[0])) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify where the messages that couldn't be delivered/relayed will be stored.\n");
		return -1;
	}

	if (stat (server.error_directory, &buf) < 0) {
		fprintf (stderr, "The directory %s doesn't exist.\n", server.error_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		fprintf (stderr, "%s is not a directory but a file.\n", server.error_directory);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	/* Get postmaster's mail address. */
	string = configuration_get_value (&conf, "General", "Postmaster", NULL);
	if ((!string) || (!*string)) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "You must specify the mail address of the postmaster.\n");
		return -1;
	}

	if (parse_path ((unsigned char *) string, &local_part, &local_part_len, &domain, &domainlen, NULL, NULL) < 0) {
		fprintf (stderr, "The postmaster's mail address %s is not valid.\n", string);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	snprintf (filename, sizeof (filename), "%s/%.*s/%.*s", server.domains_directory, domainlen, (const char *) domain, local_part_len, (const char *) local_part);

	if (stat (filename, &buf) < 0) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "The directory %s doesn't exist.\n", filename);
		return -1;
	}

	if (!S_ISDIR (buf.st_mode)) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "%s is not a directory but a file.\n", filename);
		return -1;
	}

	memcpy (server.postmaster.local_part, local_part, local_part_len);
	server.postmaster.local_part[local_part_len] = 0;
	server.postmaster.local_part_len = local_part_len;

	memcpy (server.postmaster.domain, domain, domainlen);
	server.postmaster.domain[domainlen] = 0;
	server.postmaster.domainlen = domainlen;

	/* Get MaxIdleTime. */
	string = configuration_get_value (&conf, "General", "MaxIdleTime", NULL);
	if (!string) {
		max_idle_time = MAX_IDLE_TIME;
	} else {
		max_idle_time = atoi (string);
		if ((max_idle_time < 1) || (max_idle_time > 900)) {
			max_idle_time = MAX_IDLE_TIME;
		}
	}

	server.max_idle_time = max_idle_time;

	/* Get the maximum number of recipients per transaction. */
	string = configuration_get_value (&conf, "General", "MaxRecipients", NULL);
	if (!string) {
		max_recipients = MAX_RECIPIENTS;
	} else {
		max_recipients = atoi (string);
		if ((max_recipients < 1) || (max_recipients > MAX_RECIPIENTS)) {
			max_recipients = MAX_RECIPIENTS;
		}
	}

	/* Get the maximum message size. */
	string = configuration_get_value (&conf, "General", "MaxMessageSize", NULL);
	if (!string) {
		max_message_size = MAX_MESSAGE_SIZE;
	} else {
		max_message_size = atoi (string);
		if ((max_message_size < 1) || (max_message_size > MAX_MESSAGE_SIZE)) {
			max_message_size = MAX_MESSAGE_SIZE;
		}
	}

	/* Get the maximum number of transactions. */
	string = configuration_get_value (&conf, "General", "MaxTransactions", NULL);
	if (!string) {
		max_transactions = MAX_TRANSACTIONS;
	} else {
		max_transactions = atoi (string);
		if ((max_transactions < 1) || (max_transactions > MAX_TRANSACTIONS)) {
			max_transactions = MAX_TRANSACTIONS;
		}
	}

	/* Get whether we have to log mails. */
	string = configuration_get_value (&conf, "General", "LogMails", NULL);
	if (!string) {
		server.log_mails = 1;
	} else if (strcasecmp (string, "Enabled") == 0) {
		server.log_mails = 1;
	} else if (strcasecmp (string, "Disabled") == 0) {
		server.log_mails = 0;
	} else {
		fprintf (stderr, "LogMails is neither \"Enabled\" nor \"Disabled\"... taking \"Enabled\".\n");
		server.log_mails = 1;
	}

	if (server.log_mails) {
		/* Get log file. */
		string = configuration_get_value (&conf, "General", "LogFile", NULL);
		if ((!string) || (!*string)) {
			configuration_free (&mime_types);
			configuration_free (&conf);

			fprintf (stderr, "You must specify the log file.\n");
			return -1;
		}

		server.logfile = string;
	}

	/* Get user which we will switch to when running as root. */
	server.user = configuration_get_value (&conf, "General", "User");

	/* Create server. */
	if (create_server (&server, port) < 0) {
		configuration_free (&mime_types);
		configuration_free (&conf);

		fprintf (stderr, "Couldn't create server.\n");
		return -1;
	}

	server.nfile = 0;
	server.max_recipients = max_recipients;
	server.max_message_size = max_message_size;
	server.max_transactions = max_transactions;

	/* Install signal handlers. */
	sigemptyset (&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &act, NULL);

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

		delete_server (&server);

		configuration_free (&mime_types);
		configuration_free (&conf);

		return -1;
	}

	start_server (&server);

	delete_server (&server);

	dnscache_free (&dnscache);

	configuration_free (&conf);
	configuration_free (&mime_types);

	printf ("Exiting.\n");

	return 0;
}

void stop (int nsignal)
{
	printf ("Signal %d received.\nStopping...\n", nsignal);

	stop_server (&server);

	/* Wait for delivery process. */
	waitpid (server.delivery_pid, NULL, 0);
}

void handle_alarm (int nsignal)
{
	server.handle_alarm = 1;
}
