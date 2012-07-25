#ifndef PARSER_H
#define PARSER_H

typedef enum {
	BDAT = 0,
	DATA,
	EHLO,
	EXPN,
	HELO,
	HELP,
	MAIL,
	NOOP,
	QUIT,
	RCPT,
	RSET,
	VRFY} eSmtpCommand;

typedef struct {
	char *command;
	size_t len;
} smtp_command_t;

int parse_smtp_command (unsigned char *line, unsigned char **argument, int *error);
int parse_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen, int *size_parameter, size_t *size_value);
int parse_domain (const unsigned char *domain, size_t *domainlen);
int parse_reverse_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen, int *size_parameter, size_t *size_value);
int parse_forward_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen);
int parse_help (const unsigned char *command);
int parse_bdat (const unsigned char *argument, size_t *chunk_size, int *last);

int valid_domain (const unsigned char *domain);
int valid_local_part (const unsigned char *local_part);

#endif /* PARSER_H */
