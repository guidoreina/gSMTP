#ifndef BUFFER_H
#define BUFFER_H

typedef struct {
	char *data;
	size_t size;
	size_t used;

	size_t buffer_increment;
} buffer_t;

void buffer_init (buffer_t *buffer, size_t buffer_increment);
void buffer_free (buffer_t *buffer);
void buffer_reset (buffer_t *buffer);

void buffer_set_buffer_increment (buffer_t *buffer, size_t buffer_increment);
int buffer_allocate (buffer_t *buffer, size_t size);

int buffer_append_char (buffer_t *buffer, char c);
int buffer_append_string (buffer_t *buffer, const char *string);
int buffer_append_size_bounded_string (buffer_t *buffer, const char *string, size_t len);

int buffer_format (buffer_t *buffer, const char *format, ...);

#endif /* BUFFER_H */
