#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "buffer.h"

#define DEFAULT_BUFFER_INCREMENT 64

void buffer_init (buffer_t *buffer, size_t buffer_increment)
{
	buffer->data = NULL;
	buffer->size = 0;
	buffer->used = 0;

	if (buffer_increment == 0) {
		buffer->buffer_increment = DEFAULT_BUFFER_INCREMENT;
	} else {
		buffer->buffer_increment = buffer_increment;
	}
}

void buffer_free (buffer_t *buffer)
{
	if (buffer->data) {
		free (buffer->data);
		buffer->data = NULL;
	}

	buffer->size = 0;
	buffer->used = 0;
}

void buffer_reset (buffer_t *buffer)
{
	buffer->used = 0;
}

void buffer_set_buffer_increment (buffer_t *buffer, size_t buffer_increment)
{
	if (buffer_increment == 0) {
		buffer->buffer_increment = DEFAULT_BUFFER_INCREMENT;
	} else {
		buffer->buffer_increment = buffer_increment;
	}
}

int buffer_allocate (buffer_t *buffer, size_t size)
{
	char *data;
	size_t quotient, remainder;

	if (size == 0) {
		return 0;
	}

	quotient = size / buffer->buffer_increment;
	remainder = size % buffer->buffer_increment;
	if (remainder != 0) {
		quotient++;
	}

	size = quotient * buffer->buffer_increment;

	if (buffer->used + size <= buffer->size) {
		return 0;
	}

	size += buffer->size;
	data = (char *) realloc (buffer->data, size);
	if (!data) {
		return -1;
	}

	buffer->data = data;
	buffer->size = size;

	return 0;
}

int buffer_append_char (buffer_t *buffer, char c)
{
	if (buffer_allocate (buffer, 1) < 0) {
		return -1;
	}

	buffer->data[buffer->used++] = c;

	return 0;
}

int buffer_append_string (buffer_t *buffer, const char *string)
{
	if (!string) {
		return 0;
	}

	return buffer_append_size_bounded_string (buffer, string, strlen (string));
}

int buffer_append_size_bounded_string (buffer_t *buffer, const char *string, size_t len)
{
	if (len == 0) {
		return 0;
	}

	if (buffer_allocate (buffer, len) < 0) {
		return -1;
	}

	memcpy (buffer->data + buffer->used, string, len);
	buffer->used += len;

	return 0;
}

int buffer_format (buffer_t *buffer, const char *format, ...)
{
	va_list ap;
	int n, size;

	if (buffer_allocate (buffer, DEFAULT_BUFFER_INCREMENT) < 0) {
		return -1;
	}

	size = buffer->size - buffer->used;

	while (1) {
		va_start (ap, format);

		n = vsnprintf (buffer->data + buffer->used, size, format, ap);

		va_end (ap);

		if (n > -1) {
			if (n < size) {
				buffer->used += n;
				break;
			}

			size = n + 1;
		} else {
			size *= 2;
		}

		if (buffer_allocate (buffer, size) < 0) {
			return -1;
		}
	}

	return 0;
}
