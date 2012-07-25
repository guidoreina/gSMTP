#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "stream_copy.h"

#ifndef min
#define min(a, b) (((a) < (b))?(a):(b))
#endif

void stream_copy_init (stream_copy_t *stream_copy, input_stream_t *input_stream, int fd)
{
	stream_copy->input_stream = input_stream;
	stream_copy->fd = fd;

	stream_copy->write_ptr = input_stream->read_ptr;

	stream_copy->written = 0;
	stream_copy->zero_bytes_written = 0;

	stream_copy->error = 0;
}

int stream_copy_until_end (stream_copy_t *stream_copy)
{
	input_stream_t *input_stream;
	ssize_t bytes;
	ssize_t written;

	input_stream = stream_copy->input_stream;

	bytes = input_stream->read_end - input_stream->read_ptr;

	/* If we don't have data in the buffer... */
	if (bytes == 0) {
		/* Refill buffer. */
		bytes = input_stream_buffer_refill (input_stream);
		if (bytes < 0) {
			stream_copy->error = input_stream->error;
			return -1;
		} else if (bytes == 0) {
			/* End of input stream. */
			return 0;
		}
	}

	do {
		/* Write data. */
		do {
			written = write (stream_copy->fd, input_stream->read_ptr, bytes);
			if (written < 0) {
				stream_copy->error = errno;

				stream_copy->write_ptr = input_stream->read_ptr;

				return -1;
			} else if (written == 0) {
				stream_copy->zero_bytes_written++;
				if (stream_copy->zero_bytes_written >= 5) {
					stream_copy->error = EIO;

					stream_copy->write_ptr = input_stream->read_ptr;

					return -1;
				}
			} else {
				stream_copy->zero_bytes_written = 0;

				bytes -= written;

				stream_copy->written += written;
				input_stream->read_ptr += written;
			}
		} while (bytes > 0);

		/* Refill buffer. */
		bytes = input_stream_buffer_refill (input_stream);
		if (bytes < 0) {
			stream_copy->error = input_stream->error;

			stream_copy->write_ptr = input_stream->read_ptr;

			return -1;
		} else if (bytes == 0) {
			/* End of input stream. */
			stream_copy->write_ptr = input_stream->read_ptr;
			return 0;
		}
	} while (1);
}

int stream_copy_chunk (stream_copy_t *stream_copy, size_t *bytes)
{
	input_stream_t *input_stream;
	ssize_t n;
	ssize_t written;

	/* If we don't have anything to do... */
	if (*bytes == 0) {
		/* Don't do anything. */
		return 0;
	}

	input_stream = stream_copy->input_stream;

	n = input_stream->read_end - input_stream->read_ptr;

	/* If we don't have data in the buffer... */
	if (n == 0) {
		/* Refill buffer. */
		n = input_stream_buffer_refill (input_stream);
		if (n < 0) {
			stream_copy->error = input_stream->error;
			return -1;
		} else if (n == 0) {
			/* End of input stream. */
			return 0;
		}
	}

	do {
		/* Write data. */
		do {
			written = write (stream_copy->fd, input_stream->read_ptr, min (*bytes, n));
			if (written < 0) {
				stream_copy->error = errno;

				stream_copy->write_ptr = input_stream->read_ptr;

				return -1;
			} else if (written == 0) {
				stream_copy->zero_bytes_written++;
				if (stream_copy->zero_bytes_written >= 5) {
					stream_copy->error = EIO;

					stream_copy->write_ptr = input_stream->read_ptr;

					return -1;
				}
			} else {
				stream_copy->zero_bytes_written = 0;

				n -= written;
				*bytes -= written;

				stream_copy->written += written;
				input_stream->read_ptr += written;

				if (*bytes == 0) {
					stream_copy->write_ptr = input_stream->read_ptr;
					return 0;
				}
			}
		} while (n > 0);

		/* Refill buffer. */
		n = input_stream_buffer_refill (input_stream);
		if (n < 0) {
			stream_copy->error = input_stream->error;

			stream_copy->write_ptr = input_stream->read_ptr;

			return -1;
		} else if (n == 0) {
			/* End of input stream. */
			stream_copy->write_ptr = input_stream->read_ptr;
			return 0;
		}
	} while (1);
}

int stream_copy_until_needle (stream_copy_t *stream_copy, const char *needle, size_t len, size_t max_written, int *found, int *done)
{
	input_stream_t *input_stream;
	unsigned char *ptr;
	ssize_t bytes;
	ssize_t written;
	size_t n;

	/* If we don't have anything to do... */
	if (*done) {
		/* Don't do anything. */
		return 0;
	}

	input_stream = stream_copy->input_stream;

	/* Sanity check. */
	if (len > input_stream->size) {
		stream_copy->error = EINVAL;
		return -1;
	}

	bytes = input_stream->read_ptr - stream_copy->write_ptr;

	/* Write until we reach the read pointer. */
	while (bytes > 0) {
		written = write (stream_copy->fd, stream_copy->write_ptr, bytes);
		if (written < 0) {
			stream_copy->error = errno;
			return -1;
		} else if (written == 0) {
			stream_copy->zero_bytes_written++;
			if (stream_copy->zero_bytes_written >= 5) {
				stream_copy->error = EIO;
				return -1;
			}
		} else {
			stream_copy->zero_bytes_written = 0;

			bytes -= written;

			stream_copy->written += written;
			stream_copy->write_ptr += written;

			if (stream_copy->written > max_written) {
				stream_copy->error = EFBIG;
				return -1;
			}
		}
	}

	/* If needle was already found... */
	if (*found) {
		*done = 1;
		return 0;
	}

	do {
		/* Refill buffer. */
		n = input_stream->read_end - input_stream->read_ptr;
		if (n > 0) {
			memmove (input_stream->buf_base, input_stream->read_ptr, n);
		}

		bytes = read (input_stream->fd, input_stream->buf_base + n, input_stream->size - n);
		if (bytes < 0) {
			input_stream->error = errno;
			return -1;
		} else if (bytes == 0) {
			input_stream->end_of_file = 1;

			*done = 1;
			return 0;
		}

		input_stream->read_end = input_stream->buf_base + n + bytes;

		stream_copy->write_ptr = input_stream->buf_base;

		/* Seek needle. */
		ptr = (unsigned char *) memmem (input_stream->buf_base, n + bytes, needle, len);
		if (ptr) {
			*found = 1;

			input_stream->read_ptr = ptr;

			bytes = ptr - input_stream->buf_base;
			if (bytes == 0) {
				*done = 1;
				return 0;
			}
		} else {
			input_stream->read_ptr = input_stream->read_end - len + 1 ;
			bytes = input_stream->read_ptr - input_stream->buf_base;
		}

		/* Write until we reach the read pointer. */
		do {
			written = write (stream_copy->fd, stream_copy->write_ptr, bytes);
			if (written < 0) {
				stream_copy->error = errno;
				return -1;
			} else if (written == 0) {
				stream_copy->zero_bytes_written++;
				if (stream_copy->zero_bytes_written >= 5) {
					stream_copy->error = EIO;
					return -1;
				}
			} else {
				stream_copy->zero_bytes_written = 0;

				bytes -= written;

				stream_copy->written += written;
				stream_copy->write_ptr += written;

				if (stream_copy->written > max_written) {
					stream_copy->error = EFBIG;
					return -1;
				}
			}
		} while (bytes > 0);
	} while (!*found);

	return 0;
}
