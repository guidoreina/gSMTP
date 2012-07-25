#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "input_stream.h"

#ifndef min
#define min(a, b) (((a) < (b))?(a):(b))
#endif

int input_stream_fdopen (input_stream_t *input_stream, int fd, size_t size)
{
	input_stream->fd = fd;

	input_stream->buf_base = (unsigned char *) malloc (size);
	if (!input_stream->buf_base) {
		input_stream->error = ENOMEM;
		return -1;
	}

	input_stream->buf_end = input_stream->buf_base + size;
	input_stream->size = size;

	input_stream->read_ptr = input_stream->buf_base;
	input_stream->read_end = input_stream->read_ptr;

	input_stream->end_of_file = 0;
	input_stream->error = 0;

	return 0;
}

void input_stream_fclose (input_stream_t *input_stream)
{
	if (input_stream->fd != -1) {
		close (input_stream->fd);
	}

	if (input_stream->buf_base) {
		free (input_stream->buf_base);
	}
}

ssize_t input_stream_buffer_refill (input_stream_t *input_stream)
{
	ssize_t nread;

	nread = read (input_stream->fd, input_stream->buf_base, input_stream->size);
	if (nread < 0) {
		/* man read(2)
		 * On error, -1 is returned, and errno is set appropriately.
		 */
		input_stream->error = errno;
	} else if (nread == 0) {
		/* man read(2)
		 * Zero indicates end of file.
		 * man recv(2)
		 * The return value will be 0 when the peer has performed an orderly shutdown.
		 */
		input_stream->end_of_file = 1;
	} else {
		/* Update pointers. */
		input_stream->read_ptr = input_stream->buf_base;
		input_stream->read_end = input_stream->buf_base + nread;
	}

	/* Return the number of characters read. */
	return nread;
}

char *input_stream_fgets (input_stream_t *input_stream, char *line, size_t size, size_t *len)
{
	ssize_t nread;
	size_t count;
	char c;

	count = 0;

	/* While we have space... */
	while (count + 1 < size) {
		/* If we don't have enough data in the buffer... */
		if (input_stream->read_ptr == input_stream->read_end) {
			/* Refill buffer. */
			nread = input_stream_buffer_refill (input_stream);
			if (nread < 0) {
				/* man fgets(3)
				 * fgets() return NULL on error.
				 */
				*len = count;

				return NULL;
			} else if (nread == 0) {
				/* End of file. */
				break;
			}
		}

		c = *input_stream->read_ptr++;
		line[count++] = c;

		/* End of line? */
		if (c == '\n') {
			break;
		}
	}

	if (count == 0) {
		/* man fgets(3)
		 * fgets() return NULL when end of file occurs while no characters have been read.
		 */
		return NULL;
	}

	/* NUL terminate string. */
	line[count] = 0;

	*len = count;

	return line;
}

ssize_t input_stream_getdelim (input_stream_t *input_stream, char **line, size_t *n, int delim)
{
	char *buffer;
	size_t size;
	size_t count;
	ssize_t nread;
	int c;

	if ((!line) || (!n)) {
		/* Invalid argument. */
		input_stream->error = EINVAL;
		return -1;
	}

	/* Sanity check. */
	if (!*line) {
		*n = 0;
	}

	count = 0;

	do {
		/* If we don't have enough data in the buffer... */
		if (input_stream->read_ptr == input_stream->read_end) {
			/* Refill buffer. */
			nread = input_stream_buffer_refill (input_stream);
			if (nread <= 0) {
				/* man getdelim(3)
				 * getdelim() return -1 on failure to read a line (including end of file condition).
				 */
				return -1;
			}
		}

		/* If *line is not big enough... */
		if (count + 1 >= *n) {
			size = *n + 120;
			buffer = (char *) realloc (*line, size);
			if (!buffer) {
				input_stream->error = ENOMEM;
				return -1;
			}

			*line = buffer;
			*n = size;
		}

		c = *input_stream->read_ptr++;
		(*line)[count++] = c;

		/* End of line? */
		if (c == delim) {
			break;
		}
	} while (1);

	/* NUL terminate string. */
	(*line)[count] = 0;

	return count;
}

ssize_t input_stream_discard_delim (input_stream_t *input_stream, int delim)
{
	unsigned char *ptr;
	ssize_t bytes;
	size_t count;

	bytes = input_stream->read_end - input_stream->read_ptr;

	/* If we don't have enough data in the buffer... */
	if (bytes == 0) {
		bytes = input_stream_buffer_refill (input_stream);
		if (bytes <= 0) {
			return bytes;
		}
	}

	count = 0;

	do {
		ptr = input_stream->read_ptr;
		input_stream->read_ptr = (unsigned char *) memchr (ptr, delim, bytes);
		if (input_stream->read_ptr) {
			input_stream->read_ptr++;
			count += (input_stream->read_ptr - ptr);
			break;
		}

		count += bytes;

		bytes = input_stream_buffer_refill (input_stream);
		if (bytes <= 0) {
			return bytes;
		}
	} while (1);

	return count;
}

size_t input_stream_fread (input_stream_t *input_stream, void *buffer, size_t count)
{
	ssize_t bytes;
	size_t nread;

	if (count == 0) {
		/* Nothing to do. */
		return 0;
	}

	bytes = input_stream->read_end - input_stream->read_ptr;

	/* If we have enough data in the buffer... */
	if ((unsigned) bytes >= count) {
		memcpy (buffer, input_stream->read_ptr, count);
		input_stream->read_ptr += count;

		return count;
	}

	if (bytes > 0) {
		memcpy (buffer, input_stream->read_ptr, bytes);
		input_stream->read_ptr += bytes;
		nread = bytes;
	} else {
		nread = 0;
	}

	do {
		/* Read directly in user's buffer. */
		bytes = read (input_stream->fd, (char *) buffer + nread, count - nread);
		if (bytes < 0) {
			input_stream->error = errno;
			return nread;
		} else if (bytes == 0) {
			input_stream->end_of_file = 1;
			return nread;
		}

		nread += bytes;
	} while (nread < count);

	return count;
}

size_t input_stream_skip (input_stream_t *input_stream, size_t count)
{
	ssize_t bytes;
	size_t nread;

	if (count == 0) {
		/* Nothing to do. */
		return 0;
	}

	bytes = input_stream->read_end - input_stream->read_ptr;

	/* If we have enough data in the buffer... */
	if ((unsigned) bytes >= count) {
		input_stream->read_ptr += count;
		return count;
	}

	if (bytes > 0) {
		input_stream->read_ptr += bytes;
		nread = bytes;
	} else {
		nread = 0;
	}

	do {
		bytes = read (input_stream->fd, input_stream->buf_base, min (count - nread, input_stream->size));
		if (bytes < 0) {
			input_stream->error = errno;
			return nread;
		} else if (bytes == 0) {
			input_stream->end_of_file = 1;
			return nread;
		}

		nread += bytes;
	} while (nread < count);

	return count;
}
