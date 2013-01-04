#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "stream.h"


typedef struct {
  char *file;
  int fd;
} razor2_stream_file_t;

typedef struct {
  const char *str;
  size_t len;
} razor2_stream_mem_t;


//#define RAZOR2_FLAG_NONE (0x00)
//#define RAZOR2_FLAG_SEEKABLE (0x01)
// this isn't right... all in all I want to implement a read only stream, so
// the writable flag doesn't actually belong here
//#define RAZOR2_FLAG_WRITEABLE (0X01)
// the pipe stream would just read everything into buffer first... and then
// acts the same as the memory mapped stream... we only support seekable
// backends the rest is actually mapped to


struct _razor_stream {
  enum {
    RAZOR2_STREAM_NONE = 0,
    RAZOR2_STREAM_FILE,
    RAZOR2_STREAM_MEM,
    RAZOR2_STREAM_PIPE
  } type;

  int64_t off; /* start */
  int64_t lim; /* end */

//  union {
//    razor2_stream_file_t file;
//    razor2_stream_mem_t mem;
//    razor2_stream_pipe_t pipe;
//  } props;

  razor_stream_getc_func getc_func;
  razor_stream_gets_func gets_func;
};


// create the memory interface first and go from there!
// well we also need to decide how we want the stream to look...
// do we just want to abstract io, or do we actually want to also
// provide some higher level functions?!?!

int
razor2_stream_file (razor2_stream_t **stream, const char *file)
{
  // implement
}

int
razor2_stream_mem (razor2_stream_t **stream, const char *str, size_t len)
{
  razor_stream_t *ptr;

  assert (stream);
  assert (str);

  if (! (ptr = calloc (1, sizeof (razor_stream_t)))) {
    return (errno);
  }

  ptr->type = RAZOR2_STREAM_MEM;
  ptr->flags = RAZOR2_FLAG_SEEKABLE;
  ptr->props.mem.str = str;
  ptr->props.mem.len = len;
  ptr->getc_func = &razor2_stream_mem_getc;
  ptr->gets_func = &razor2_stream_mem_gets;

  *stream = ptr;
  return (0);
}

/* pipe functionality is emulated by reading all input from the file
   descriptor and writing it to memory, so that the input becomes seekable */

#define RAZOR2_BLOCK_SIZE 4096

int
razor2_stream_pipe (razor2_stream_t **stream, int fd)
{
  int err;
  char *alloc_str, *str;
  size_t alloc_len, len, pos;
  ssize_t cnt;

  assert (stream);

  str = NULL;
  len = 0;

  for (loop = 1; loop;) {
again:
    /* allocate more memory if needed */
    if (pos >= len) {
      /* calculate if amount of memory is withing limits */
      if (len > (SIZE_MAX - (RAZOR2_BLOCK_SIZE + 1))) {
        err = ENOMEM;
        goto error;
      }
      alloc_len = len + RAZOR2_BLOCK_SIZE;
      /* allocate memory */
      alloc_str = realloc (str, alloc_len + 1);
      if (! alloc_str) {
        err = errno;
        goto error;
      }
      str = alloc_str;
      len = alloc_len;
      /* zero out newly allocated memory */
      memset (str + pos, '\0', (len - pos) + 1);
      goto again;
    }

    err = errno;
    errno = 0;

    cnt = read (fd, str+pos, len-pos);
    if (cnt > 0) {
      pos += (size_t) cnt;
    }
    if (pos < len) {
      if (errno && errno != EINTR) {
        err = errno;
        goto error;
      } else {
        /* end of file */
        loop = 0;
      }
    }

    errno = err;
  }

  if ((err = razor2_stream_mem (stream, str, pos)) == 0) {
    *stream->type = RAZOR2_STREAM_PIPE;
    return (0);
  }

error:
  if (str)
    free (str);
  return (err);
}

#undef RAZOR2_BLOCK_SIZE

int
razor2_stream_mem_getc (int *chr, razor_stream_t *stream)
{
  razor2_stream_mem_t *props;

  assert (chr);
  assert (stream && (stream->type == RAZOR2_STREAM_MEM ||
                     stream->type == RAZOR2_STREAM_PIPE));

  props = &stream->props.mem;

  if (props->pos >= props->len)
    *chr = 0;
  else
    *chr = props->str[ (props->pos++) ];

  return (0);
}

//
//int
//razor2_stream_mem_gets (char **str, size_t *len, razor2_stream_t *stream)
//{
//  int loop;
//  razor2_stream_mem_t *props;
//  size_t pos;
//
//  assert (str);
//  assert (len);
//  assert (stream && stream->type == RAZOR2_STREAM_MEM);
//
//  props = &stream->props.mem;
//
//  for (loop = 1, pos = props->pos; loop && pos < props->len; pos++) {
//    if (props->str[pos] == '\n')
//      loop = 0;
//  }
//
//  /* we copy the line and null terminate it for safety */
//  //
//
//
  // is this really the right way to go about it?!?!
  // while it's not necessary to copy the data... it's probably best to do so
  // anyway to keep safe... right?!?!

  // 0123456  0123456
  // foobar   foobar
  //  ^  ^     ^    ^
  //  1234     123456



  // implement
//}


/* generic interfaces */
//
//int
//razor2_stream_eof (razor2_stream_t *stream)
//{
//  // implement
//}
//
//int
//razor2_stream_getc (int *chr, razor_stream_t *stream)
//{
//  assert (stream);
//
//  return (stream->getc_func (chr, stream));
//}
//
//int
//razor2_stream_gets (char **str, size_t *len, razor_stream_t *stream)
//{
//  assert (stream);
//
//  return (stream->gets_func (str, len, stream));
//
//  // we copy data from our internal buffer first
//  // of course this just uses FILE stuff >> i don't know
//  // if it's a file we can pass it
//
//  // we do removal or carriage return near end here directly
//}







































































