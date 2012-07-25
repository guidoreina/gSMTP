#ifndef STREAM_COPY_H
#define STREAM_COPY_H

#include "input_stream.h"

typedef struct {
	input_stream_t *input_stream; /* Input stream. */
	int fd;                       /* Destination file descriptor. */

	unsigned char *write_ptr;

	size_t written;
	size_t zero_bytes_written;

	int error;
} stream_copy_t;

void stream_copy_init (stream_copy_t *stream_copy, input_stream_t *input_stream, int fd);

int stream_copy_until_end (stream_copy_t *stream_copy);
int stream_copy_chunk (stream_copy_t *stream_copy, size_t *bytes);
int stream_copy_until_needle (stream_copy_t *stream_copy, const char *needle, size_t len, size_t max_written, int *found, int *done);

#endif /* STREAM_COPY_H */
