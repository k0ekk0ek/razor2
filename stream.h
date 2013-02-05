#ifndef RAZOR2_STREAM_H_INCLUDED
#define RAZOR2_STREAM_H_INCLUDED

/* extremeley simple read-only io abstraction layer for use in razor2 that
   mimics stdio.h behaviour */

typedef struct _razor2_stream razor2_stream_t;

int razor2_stream_file (razor2_stream_t **, const char *);
int razor2_stream_mem (razor2_stream_t **, const char *, size_t);
int razor2_stream_pipe (razor2_stream_t **, int);

int razor2_stream_eof (razor2_stream_t *);
int razor2_stream_error (razor2_stream_t *);
ssize_t razor2_stream_read (void *, size_t, razor2_stream_t *);
int razor2_stream_seek (razor2_stream_t *, off_t, int);
off_t razor2_stream_tell (razor2_stream_t *);
int razor2_stream_close (razor2_stream_t *);

#endif

