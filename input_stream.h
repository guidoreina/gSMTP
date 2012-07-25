#ifndef INPUT_STREAM_H
#define INPUT_STREAM_H

#ifndef EOF
#define EOF (-1)
#endif

typedef struct {
	int fd;                  /* File descriptor. */

	unsigned char *buf_base; /* Start of reserve area. */
	unsigned char *buf_end;  /* End of reserve area. */
	size_t size;             /* Size of reserve area. */

	unsigned char *read_ptr; /* Current read pointer. */
	unsigned char *read_end; /* End of get area. */

	int end_of_file;         /* End of file? */
	int error;               /* Error? */
} input_stream_t;

int input_stream_fdopen (input_stream_t *input_stream, int fd, size_t size);
void input_stream_fclose (input_stream_t *input_stream);

ssize_t input_stream_buffer_refill (input_stream_t *input_stream);

#define input_stream_feof(input_stream)     ((input_stream)->end_of_file)
#define input_stream_ferror(input_stream)   ((input_stream)->error)
#define input_stream_clearerr(input_stream) ((input_stream)->end_of_file = 0; (input_stream)->error = 0)

#define input_stream_getc(input_stream) (((input_stream)->read_ptr < (input_stream)->read_end)?(unsigned char) *(input_stream)->read_ptr++: \
					((input_stream)->end_of_file)?EOF: \
					(input_stream_buffer_refill (input_stream) <= 0)?EOF:(unsigned char) *(input_stream)->read_ptr++)

char *input_stream_fgets (input_stream_t *input_stream, char *line, size_t size, size_t *len);
ssize_t input_stream_getdelim (input_stream_t *input_stream, char **line, size_t *n, int delim);
#define input_stream_getline(input_stream, line, n) (input_stream_getdelim ((input_stream), line, n, '\n'))

ssize_t input_stream_discard_delim (input_stream_t *input_stream, int delim);
#define input_stream_discard_line(input_stream) (input_stream_discard_delim ((input_stream), '\n'))

size_t input_stream_fread (input_stream_t *input_stream, void *buffer, size_t count);

size_t input_stream_skip (input_stream_t *input_stream, size_t count);

#endif /* INPUT_STREAM_H */
