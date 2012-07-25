#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "constants.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)        (sizeof (x) / sizeof (*x))
#endif

#ifndef IS_ALPHA
#define IS_ALPHA(x)          (((x >= 'A') && (x <= 'Z')) || ((x >= 'a') && (x <= 'z')))
#endif

#ifndef IS_DIGIT
#define IS_DIGIT(x)          ((x >= '0') && (x <= '9'))
#endif

#ifndef IS_WHITE_SPACE
#define IS_WHITE_SPACE(x)    ((x == ' ') || (x == '\t'))
#endif

#ifndef SKIP_WHITE_SPACES
#define SKIP_WHITE_SPACES(x) while (IS_WHITE_SPACE(*x)) x++
#endif

#ifndef IS_ATEXT
#define IS_ATEXT(x)          ((IS_ALPHA(x)) || (IS_DIGIT(x)) || (x == '!') || \
                             (x == '#') || (x == '$') || (x == '%') || (x == '&') || \
                             (x == '\'') || (x == '*') || (x == '+') || (x == '-') || \
                             (x == '/') || (x == '=') || (x == '?') || (x == '^') || \
                             (x == '_' ) || (x == '`') || (x == '{') || (x == '|') || \
                             (x == '}') || (x == '~'))
#endif

#ifndef IS_TEXT
#define IS_TEXT(x)           (((x >= 1) && (x <= 9)) || (x == 11) || (x == 12) || ((x >= 14) && (x <= 127)))
#endif

#ifndef IS_NO_WS_CTL
#define IS_NO_WS_CTL(x)      (((x >= 1) && (x <= 8)) || (x == 11) || (x == 12) || ((x >= 14) && (x <= 31)) || (x == 127))
#endif

#ifndef IS_QTEXT
#define IS_QTEXT(x)          ((IS_NO_WS_CTL(x)) || (x == 33) || ((x >= 35) && (x <= 91)) || ((x >= 93) && (x <= 126)))
#endif


static smtp_command_t smtp_commands[] = {
	{"BDAT", 4},
	{"DATA", 4},
	{"EHLO", 4},
	{"EXPN", 4},
	{"HELO", 4},
	{"HELP", 4},
	{"MAIL", 4},
	{"NOOP", 4},
	{"QUIT", 4},
	{"RCPT", 4},
	{"RSET", 4},
	{"VRFY", 4}
};

static unsigned char *get_parameter (unsigned char *ptr, const char *param, size_t paramlen, unsigned char **value, size_t *valuelen);

int parse_smtp_command (unsigned char *line, unsigned char **argument, int *error)
{
	eSmtpCommand smtp_command;
	unsigned char *end;
	size_t command_len;
	int i, j, pivot;
	int ret;

	while (IS_WHITE_SPACE (*line)) {
		line++;
	}

	end = line;
	while (*end > ' ') {
		end++;
	}

	command_len = end - line;
	if (command_len < 4) {
		/* 500 Syntax error, command unrecognized. */
		*error = 500;
		return -1;
	}

	i = 0;
	j = ARRAY_SIZE (smtp_commands) - 1;

	while (i <= j) {
		pivot = (i + j) / 2;
		ret = strncasecmp ((const char *) line, smtp_commands[pivot].command, command_len);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			if (command_len < smtp_commands[pivot].len) {
				j = pivot - 1;
			} else if (command_len == smtp_commands[pivot].len) {
				SKIP_WHITE_SPACES (end);

				smtp_command = (eSmtpCommand) pivot;
				switch (smtp_command) {
					case BDAT:
						if (*end == '\r') {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						} else {
							*argument = end;
						}

						break;
					case DATA:
						/* RFC 2821
						 * http://www.faqs.org/rfcs/rfc2821.html
						 * 4.1.1 Command Semantics and Syntax.
						 * Several commands (RSET, DATA, QUIT) are specified as not permitting
						 * parameters. In the absence of specific extensions offered by the
						 * server and accepted by the client, clients MUST NOT send such
						 * parameters and servers SHOULD reject commands containing them as
						 * having invalid syntax.
						 */
						if ((*end != '\r') || (*(end + 1) != '\n')) {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						}

						break;
					case EHLO:
					case HELO:
						if (*end == '\r') {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						} else {
							*argument = end;
						}

						break;
					case EXPN:
						if (*end == '\r') {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						} else {
							*argument = end;
						}

						break;
					case HELP:
						/* If the HELP command has an argument... */
						if (*end != '\r') {
							*argument = end;
						} else {
							*argument = NULL;
						}

						break;
					case MAIL:
						if (strncasecmp ((const char *) end, "FROM:", 5) != 0) {
							/* 500 Syntax error, command unrecognized. */
							*error = 500;
						} else {
							end += 5;
							SKIP_WHITE_SPACES (end);
							if (*end == '\r') {
								/* 501 Syntax error in parameters or arguments. */
								*error = 501;
							} else {
								*argument = end;
							}
						}

						break;
					case NOOP:
						break;
					case QUIT:
						if ((*end != '\r') || (*(end + 1) != '\n')) {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						}

						break;
					case RCPT:
						if (strncasecmp ((const char *) end, "TO:", 3) != 0) {
							/* 500 Syntax error, command unrecognized. */
							*error = 500;
						} else {
							end += 3;
							SKIP_WHITE_SPACES (end);
							if (*end == '\r') {
								/* 501 Syntax error in parameters or arguments. */
								*error = 501;
							} else {
								*argument = end;
							}
						}

						break;
					case RSET:
						if ((*end != '\r') || (*(end + 1) != '\n')) {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						}

						break;
					case VRFY:
						if (*end == '\r') {
							/* 501 Syntax error in parameters or arguments. */
							*error = 501;
						} else {
							*argument = end;
						}

						break;
				}

				return (int) smtp_command;
			} else {
				i = pivot + 1;
			}
		} else {
			i = pivot + 1;
		}
	}

	/* 500 Syntax error, command unrecognized. */
	*error = 500;

	return -1;
}

unsigned char *get_parameter (unsigned char *ptr, const char *param, size_t paramlen, unsigned char **value, size_t *valuelen)
{
	do {
		SKIP_WHITE_SPACES (ptr);
		if (*ptr < ' ') {
			break;
		}

		if ((strncasecmp ((const char *) ptr, param, paramlen) == 0) && (ptr[paramlen] == '=')) {
			ptr += (paramlen + 1);
			if (*ptr <= ' ') {
				return NULL;
			}

			*value = ptr;

			do {
				ptr++;
			} while (*ptr > ' ');

			*valuelen = ptr - *value;

			return ptr;
		} else {
			/* Skip parameter. */
			do {
				ptr++;
			} while (*ptr > ' ');
		}
	} while (1);

	*value = NULL;

	return ptr;
}

int parse_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen, int *size_parameter, size_t *size_value)
{
	/* RFC 2821
	 * http://www.faqs.org/rfcs/rfc2821.html
	 * 4.1.2 Command Argument Syntax
	 * Reverse-path = Path
	 * Forward-path = Path
	 * Path = "<" [ A-d-l ":" ] Mailbox ">"
	 * A-d-l = At-domain *( "," A-d-l )
	 * At-domain = "@" domain
	 * domain = (sub-domain 1*("." sub-domain)) / address-literal
	 * sub-domain = Let-dig [Ldh-str]
	 * address-literal = "[" IPv4-address-literal /
	 *                       IPv6-address-literal /
	 *                       General-address-literal "]"
	 * Ldh-str = *( ALPHA / DIGIT / "-" ) Let-dig
	 * Let-dig = ALPHA / DIGIT
	 * Mailbox = Local-part "@" Domain
	 * Local-part = Dot-string / Quoted-string
	 * Dot-string = Atom *("." Atom)
	 * Atom = 1*atext
	 * Quoted-string = DQUOTE *qcontent DQUOTE
	 * qcontent = qtext / quoted-pair
	 * qtext = NO-WS-CTL / %d33 / %d35-91 / %d93-126
	 * quoted-pair = ("\" text) / obs-qp
	 * NO-WS-CTL = %d1-8 / %d11 / %d12 / %d14-31 / %d127
	 */

	unsigned char *ptr;
	unsigned char last_char;
	unsigned char *value;
	size_t valuelen;
	int quoted;
	int state;
	int n;
	int ndots;
	size_t len;

	ptr = path;

	if (*ptr == '<') {
		quoted = 1;

		last_char = '>';

		ptr++;
	} else if (*ptr == '\'') {
		quoted = 1;

		last_char = '\'';

		ptr++;
	} else {
		quoted = 0;

		/* Just to remove warnings... */
		last_char = 0;
	}

	if (*ptr == '@') {
		/* [ A-d-l ":" ]
		 * Skip source route.
		 */
		while ((*ptr != '\r') && (*ptr != ':')) {
			ptr++;
		}

		if (*ptr != ':') {
			return -1;
		}

		ptr++;
	}

	/* Parse Local-part. */
	if (*ptr == '"') {
		/* Local-part = Quoted-string */
		ptr++;

		*local_part = ptr;
		len = 0;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '\\') {
					state = 1;
				} else if (*ptr == '"') {
					break;
				} else if (!IS_QTEXT (*ptr)) {
					return -1;
				}
			} else {
				if (!IS_TEXT (*ptr)) {
					return -1;
				}

				state = 0;
			}

			if (++len > LOCAL_PART_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);

		/* If the local part is too short... */
		if (ptr == *local_part) {
			return -1;
		}

		*local_part_len = len;

		ptr++;

		if (*ptr != '@') {
			return -1;
		}
	} else {
		/* Local-part = Dot-string */
		if (!IS_ATEXT (*ptr)) {
			return -1;
		}

		*local_part = ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '.') {
					state = 1;
				} else if (*ptr == '@') {
					break;
				} else if (!IS_ATEXT (*ptr)) {
					return -1;
				}
			} else {
				if (!IS_ATEXT (*ptr)) {
					return -1;
				}

				state = 0;
			}

			if (++len > LOCAL_PART_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);

		*local_part_len = len;
	}

	/* Skip '@'. */
	ptr++;

	/* Parse domain. */
	if (*ptr == '[') {
		/* Domain = address-literal */
		*domain = ptr++;
		len = 1;

		ndots = 0;

		state = 0;

		do {
			if (state == 0) {
				if (!IS_DIGIT (*ptr)) {
					return -1;
				}

				n = *ptr - '0';

				state = 1;
			} else {
				if (*ptr == '.') {
					if (++ndots > 3) {
						return -1;
					}

					state = 0;
				} else if (IS_DIGIT (*ptr)) {
					n = (n * 10) + (*ptr - '0');
					if (n > 255) {
						return -1;
					}
				} else if (*ptr == ']') {
					break;
				} else {
					return -1;
				}
			}

			len++;
			ptr++;
		} while (1);

		if (ndots != 3) {
			return -1;
		}

		len++;
		ptr++;

		if (quoted) {
			if (*ptr != last_char) {
				return -1;
			}
		} else if ((!IS_WHITE_SPACE (*ptr)) && (*ptr != '\r')) {
			return -1;
		}
	} else {
		/* Domain = (sub-domain 1*("." sub-domain)) */
		if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
			return -1;
		}

		*domain = ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '-') {
					state = 1;
				} else if (*ptr == '.') {
					state = 2;
				} else if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					if (quoted) {
						if (*ptr != last_char) {
							return -1;
						}

						break;
					} else if ((!*ptr) || (IS_WHITE_SPACE (*ptr)) || (*ptr == '\r')) {
						break;
					} else {
						return -1;
					}
				}
			} else if (state == 1) {
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			} else {
				/* state = 2 */
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			}

			if (++len > DOMAIN_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);
	}

	if (*local_part_len + 1 + len > PATH_MAXLEN) {
		return -1;
	}

	*domainlen = len;

	if (size_parameter) {
		if (!get_parameter (ptr, "size", 4, &value, &valuelen)) {
			return -1;
		}

		/* If the parameter 'size' is present... */
		if (value) {
			n = 0;
			while ((*value >= '0') && (*value <= '9')) {
				n = (n * 10) + (*value - '0');
				value++;
			}

			if (*value > ' ') {
				/* Not a number. */
				return -1;
			}

			*size_parameter = 1;
			*size_value = n;
		} else {
			*size_parameter = 0;
		}
	}

	return 0;
}

int parse_domain (const unsigned char *domain, size_t *domainlen)
{
	const unsigned char *ptr;
	int state;
	int n;
	int ndots;
	size_t len;

	ptr = domain;

	if (*ptr == '[') {
		/* Domain = address-literal */
		ptr++;
		len = 1;

		ndots = 0;

		state = 0;

		do {
			if (state == 0) {
				if (!IS_DIGIT (*ptr)) {
					return -1;
				}

				n = *ptr - '0';

				state = 1;
			} else {
				if (*ptr == '.') {
					if (++ndots > 3) {
						return -1;
					}

					state = 0;
				} else if (IS_DIGIT (*ptr)) {
					n = (n * 10) + (*ptr - '0');
					if (n > 255) {
						return -1;
					}
				} else if (*ptr == ']') {
					break;
				} else {
					return -1;
				}
			}

			len++;
			ptr++;
		} while (1);

		if (ndots != 3) {
			return -1;
		}

		len++;
		ptr++;

		if ((!IS_WHITE_SPACE (*ptr)) && (*ptr != '\r')) {
			return -1;
		}
	} else {
		/* Domain = (sub-domain 1*("." sub-domain)) */
		if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
			return -1;
		}

		ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '-') {
					state = 1;
				} else if (*ptr == '.') {
					state = 2;
				} else if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					if ((IS_WHITE_SPACE (*ptr)) || (*ptr == '\r')) {
						break;
					} else {
						return -1;
					}
				}
			} else if (state == 1) {
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			} else {
				/* state = 2 */
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			}

			if (++len > DOMAIN_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);
	}

	*domainlen = len;

	SKIP_WHITE_SPACES (ptr);

	if ((*ptr != '\r') || (*(ptr + 1) != '\n')) {
		return -1;
	}

	return 0;
}

int parse_reverse_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen, int *size_parameter, size_t *size_value)
{
	unsigned char *value;
	size_t valuelen;
	size_t n;

	/* Null reverse-path? */
	if ((*path == '<') && (*(path + 1) == '>')) {
		if (*(path + 2) > ' ') {
			return -1;
		}

		*local_part = NULL;
		*local_part_len = 0;
		*domain = NULL;
		*domainlen = 0;

		if (size_parameter) {
			if (!get_parameter (path + 2, "size", 4, &value, &valuelen)) {
				return -1;
			}

			/* If the parameter 'size' is present... */
			if (value) {
				n = 0;
				while ((*value >= '0') && (*value <= '9')) {
					n = (n * 10) + (*value - '0');
					value++;
				}

				if (*value > ' ') {
					/* Not a number. */
					return -1;
				}

				*size_parameter = 1;
				*size_value = n;
			} else {
				*size_parameter = 0;
			}
		}

		return 0;
	}

	return parse_path (path, local_part, local_part_len, domain, domainlen, size_parameter, size_value);
}

int parse_forward_path (unsigned char *path, unsigned char **local_part, size_t *local_part_len, unsigned char **domain, size_t *domainlen)
{
	unsigned char *ptr;
	unsigned char last_char;
	int quoted;
	int source_route;
	int state;
	int n;
	int ndots;
	size_t len;

	ptr = path;
	source_route = 0;

	if (*ptr == '<') {
		quoted = 1;

		last_char = '>';

		ptr++;
	} else if (*ptr == '\'') {
		quoted = 1;

		last_char = '\'';

		ptr++;
	} else {
		quoted = 0;

		/* Just to remove warnings... */
		last_char = 0;
	}

	if (*ptr == '@') {
		/* [ A-d-l ":" ]
		 * Skip source route.
		 */
		source_route = 1;

		while ((*ptr != '\r') && (*ptr != ':')) {
			ptr++;
		}

		if (*ptr != ':') {
			return -1;
		}

		ptr++;
	}

	/* Parse Local-part. */
	if (*ptr == '"') {
		/* Local-part = Quoted-string */
		ptr++;

		*local_part = ptr;
		len = 0;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '\\') {
					state = 1;
				} else if (*ptr == '"') {
					break;
				} else if (!IS_QTEXT (*ptr)) {
					return -1;
				}
			} else {
				if (!IS_TEXT (*ptr)) {
					return -1;
				}

				state = 0;
			}

			if (++len > LOCAL_PART_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);

		/* If the local part is too short... */
		if (ptr == *local_part) {
			return -1;
		}

		*local_part_len = len;

		ptr++;

		if (*ptr != '@') {
			return -1;
		}
	} else {
		/* Local-part = Dot-string */
		if (!IS_ATEXT (*ptr)) {
			return -1;
		}

		*local_part = ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '.') {
					state = 1;
				} else if (*ptr == '@') {
					break;
				} else if ((*ptr == '>') && (quoted) && (!source_route) && (last_char == '>')) {
					if ((len != sizeof ("postmaster") - 1) || (strncasecmp ((const char *) *local_part, "postmaster", len) != 0)) {
						return -1;
					}

					*local_part_len = len;
					*domain = NULL;
					*domainlen = 0;

					return 0;
				} else if (!IS_ATEXT (*ptr)) {
					return -1;
				}
			} else {
				if (!IS_ATEXT (*ptr)) {
					return -1;
				}

				state = 0;
			}

			if (++len > LOCAL_PART_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);

		*local_part_len = len;
	}

	/* Skip '@'. */
	ptr++;

	/* Parse domain. */
	if (*ptr == '[') {
		/* Domain = address-literal */
		*domain = ptr++;
		len = 1;

		ndots = 0;

		state = 0;

		do {
			if (state == 0) {
				if (!IS_DIGIT (*ptr)) {
					return -1;
				}

				n = *ptr - '0';

				state = 1;
			} else {
				if (*ptr == '.') {
					if (++ndots > 3) {
						return -1;
					}

					state = 0;
				} else if (IS_DIGIT (*ptr)) {
					n = (n * 10) + (*ptr - '0');
					if (n > 255) {
						return -1;
					}
				} else if (*ptr == ']') {
					break;
				} else {
					return -1;
				}
			}

			len++;
			ptr++;
		} while (1);

		if (ndots != 3) {
			return -1;
		}

		len++;
		ptr++;

		if (quoted) {
			if (*ptr != last_char) {
				return -1;
			}
		} else if ((!IS_WHITE_SPACE (*ptr)) && (*ptr != '\r')) {
			return -1;
		}
	} else {
		/* Domain = (sub-domain 1*("." sub-domain)) */
		if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
			return -1;
		}

		*domain = ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '-') {
					state = 1;
				} else if (*ptr == '.') {
					state = 2;
				} else if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					if (quoted) {
						if (*ptr != last_char) {
							return -1;
						}

						break;
					} else if ((IS_WHITE_SPACE (*ptr)) || (*ptr == '\r')) {
						break;
					} else {
						return -1;
					}
				}
			} else if (state == 1) {
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			} else {
				/* state = 2 */
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			}

			if (++len > DOMAIN_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);
	}

	if (*local_part_len + 1 + len > PATH_MAXLEN) {
		return -1;
	}

	*domainlen = len;

	return 0;
}

int parse_help (const unsigned char *command)
{
	const unsigned char *ptr;
	size_t len;
	int i, j, pivot;
	int ret;

	len = 0;

	ptr = command;
	while (*ptr > ' ') {
		len++;
		ptr++;
	}

	if (len < 4) {
		return -1;
	}

	SKIP_WHITE_SPACES (ptr);

	if ((*ptr != '\r') || (*(ptr + 1) != '\n')) {
		return -1;
	}

	i = 0;
	j = ARRAY_SIZE (smtp_commands) - 1;

	while (i <= j) {
		pivot = (i + j) / 2;
		ret = strncasecmp ((const char *) command, smtp_commands[pivot].command, len);
		if (ret < 0) {
			j = pivot - 1;
		} else if (ret == 0) {
			if (len < smtp_commands[pivot].len) {
				j = pivot - 1;
			} else if (len == smtp_commands[pivot].len) {
				return pivot;
			} else {
				i = pivot + 1;
			}
		} else {
			i = pivot + 1;
		}
	}

	return -1;
}

int parse_bdat (const unsigned char *argument, size_t *chunk_size, int *last)
{
	const unsigned char *ptr;
	size_t size;

	ptr = argument;
	if (!IS_DIGIT (*ptr)) {
		return -1;
	}

	size = 0;

	do {
		size = (size * 10) + (*ptr - '0');

		ptr++;
	} while (IS_DIGIT (*ptr));

	SKIP_WHITE_SPACES (ptr);

	if ((*ptr == '\r') && (*(ptr + 1) == '\n')) {
		*chunk_size = size;
		*last = 0;

		return 0;
	}

	if (strncasecmp ((const char *) ptr, "LAST", 4) != 0) {
		return -1;
	}

	ptr += 4;

	SKIP_WHITE_SPACES (ptr);

	if ((*ptr != '\r') || (*(ptr + 1) != '\n')) {
		return -1;
	}

	*chunk_size = size;
	*last = 1;

	return 0;
}

int valid_domain (const unsigned char *domain)
{
	const unsigned char *ptr;
	int state;
	int n;
	int ndots;
	size_t len;

	ptr = domain;

	if (*ptr == '[') {
		/* Domain = address-literal */
		ptr++;

		ndots = 0;

		state = 0;

		do {
			if (state == 0) {
				if (!IS_DIGIT (*ptr)) {
					return -1;
				}

				n = *ptr - '0';

				state = 1;
			} else {
				if (*ptr == '.') {
					if (++ndots > 3) {
						return -1;
					}

					state = 0;
				} else if (IS_DIGIT (*ptr)) {
					n = (n * 10) + (*ptr - '0');
					if (n > 255) {
						return -1;
					}
				} else if (*ptr == ']') {
					break;
				} else {
					return -1;
				}
			}

			ptr++;
		} while (1);

		if (ndots != 3) {
			return -1;
		}

		ptr++;
	} else {
		/* Domain = (sub-domain 1*("." sub-domain)) */
		if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
			return -1;
		}

		ptr++;
		len = 1;

		state = 0;

		do {
			if (state == 0) {
				if (*ptr == '-') {
					state = 1;
				} else if (*ptr == '.') {
					state = 2;
				} else if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					break;
				}
			} else if (state == 1) {
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			} else {
				/* state = 2 */
				if ((!IS_ALPHA (*ptr)) && (!IS_DIGIT (*ptr))) {
					return -1;
				}

				state = 0;
			}

			if (++len > DOMAIN_MAXLEN) {
				return -1;
			}

			ptr++;
		} while (1);
	}

	if (*ptr) {
		return -1;
	}

	return 0;
}

int valid_local_part (const unsigned char *local_part)
{
	const unsigned char *ptr;
	int state;
	size_t len;

	ptr = local_part;

	if (!IS_ATEXT (*ptr)) {
		return -1;
	}

	ptr++;
	len = 1;

	state = 0;

	do {
		if (state == 0) {
			if (*ptr == '.') {
				state = 1;
			} else if (!*ptr) {
				break;
			} else if (!IS_ATEXT (*ptr)) {
				return -1;
			}
		} else {
			if (!IS_ATEXT (*ptr)) {
				return -1;
			}

			state = 0;
		}

		if (++len > LOCAL_PART_MAXLEN) {
			return -1;
		}

		ptr++;
	} while (1);

	return 0;
}
