#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "stream.h"

/* the macros below are copied from gnulib and are licensed under lgpl */

/* True if the arithmetic type T is signed.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))

/* Minimum and maximum values for integer types and expressions.  These
   macros have undefined behavior if T is signed and has padding bits.
   If this is a problem for you, please let us know how to fix it for
   your host.  */

/* The maximum and minimum values for the integer type T.  */
#define TYPE_MINIMUM(t)                                                 \
  ((t) (! TYPE_SIGNED (t)                                               \
        ? (t) 0                                                         \
        : TYPE_SIGNED_MAGNITUDE (t)                                     \
        ? ~ (t) 0                                                       \
        : ~ TYPE_MAXIMUM (t)))
#define TYPE_MAXIMUM(t)                                                 \
  ((t) (! TYPE_SIGNED (t)                                               \
        ? (t) -1                                                        \
        : ((((t) 1 << (sizeof (t) * CHAR_BIT - 2)) - 1) * 2 + 1)))


#define OFF_T_MAX TYPE_MAXIMUM (off_t)

typedef enum {
  RAZOR2_STREAM_NONE = 0,
  RAZOR2_STREAM_FILE,
  RAZOR2_STREAM_MEM,
  RAZOR2_STREAM_PIPE
} razor2_stream_type_t;

typedef struct {
  char fn[PATH_MAX+1];
  int fd;
} razor2_stream_file_t;

typedef struct {
  char *buf;
  size_t len;
  size_t pos;
} razor2_stream_mem_t;

typedef ssize_t(*razor2_stream_read_t)(void *, size_t, razor2_stream_t *);
typedef int(*razor2_stream_seek_t)(razor2_stream_t *, off_t, int);
typedef off_t(*razor2_stream_tell_t)(razor2_stream_t *);
typedef int(*razor2_stream_close_t)(razor2_stream_t *);

struct _razor2_stream {
  razor2_stream_type_t type; /* stream type */
  int eof; /* end of file */
  int err; /* error number */
  razor2_stream_read_t read;
  razor2_stream_seek_t seek;
  razor2_stream_tell_t tell;
  razor2_stream_close_t close;
  union {
    razor2_stream_file_t file;
    razor2_stream_mem_t mem;
  } data;
};

static ssize_t razor2_stream_file_read (void *, size_t, razor2_stream_t *);
static int razor2_stream_file_seek (razor2_stream_t *, off_t, int);
static off_t razor2_stream_file_tell (razor2_stream_t *);
static int razor2_stream_file_close (razor2_stream_t *);
static ssize_t razor2_stream_mem_read (void *, size_t, razor2_stream_t *);
static int razor2_stream_mem_seek (razor2_stream_t *, off_t, int);
static off_t razor2_stream_mem_tell (razor2_stream_t *);
static int razor2_stream_mem_close (razor2_stream_t *);
static int razor2_stream_pipe_close (razor2_stream_t *);

int
razor2_stream_file (razor2_stream_t **stm, const char *fn)
{
  int err;
  razor2_stream_t *ptr;

  assert (stm);
  assert (fn);

  if (! (ptr = calloc (1, sizeof (razor2_stream_t))))
    goto error;
  if (! (realpath (fn, ptr->data.file.fn)))
    goto error;
  if ((ptr->data.file.fd = open (ptr->data.file.fn, O_RDONLY)) < 0)
    goto error;

  ptr->type = RAZOR2_STREAM_FILE;
  ptr->read = &razor2_stream_file_read;
  ptr->seek = &razor2_stream_file_seek;
  ptr->tell = &razor2_stream_file_tell;
  ptr->close = &razor2_stream_file_close;
  *stm = ptr;
  return (0);
error:
  err = errno;
  if (ptr)
    free (ptr);
  return (err);
}

int
razor2_stream_mem (razor2_stream_t **stm, const char *str, size_t len)
{
  razor2_stream_t *ptr;

  assert (stm);
  assert (str);

  if (! (ptr = calloc (1, sizeof (razor2_stream_t)))) {
    return (errno);
  }

  ptr->type = RAZOR2_STREAM_MEM;
  ptr->read = &razor2_stream_mem_read;
  ptr->seek = &razor2_stream_mem_seek;
  ptr->tell = &razor2_stream_mem_tell;
  ptr->close = &razor2_stream_mem_close;
  ptr->data.mem.buf = (char *)str;
  ptr->data.mem.len = len;
  *stm = ptr;
  return (0);
}

/* pipe functionality is emulated by reading everything from the file
   descriptor and storing it in memory to make it seekable. doing that here
   saves us a lot of work in the upper layers */

#define RAZOR2_BLOCK_SIZE 4096

int
razor2_stream_pipe (razor2_stream_t **stm, int fd)
{
  int err, loop;
  char *alloc_str, *str;
  size_t alloc_len, len, pos;
  ssize_t cnt;

  assert (stm);

  str = NULL;
  len = 0;
  pos = 0;

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

  if ((err = razor2_stream_mem (stm, str, pos)) == 0) {
    (*stm)->type = RAZOR2_STREAM_PIPE;
    (*stm)->close = &razor2_stream_pipe_close;
    return (0);
  }

error:
  if (str)
    free (str);
  return (err);
}

#undef RAZOR2_BLOCK_SIZE

static ssize_t
razor2_stream_file_read (void *buf, size_t len, razor2_stream_t *stm)
{
  assert (buf);
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_FILE);

  int err, loop;
  size_t pos;
  ssize_t cnt;

  err = errno;

  for (loop = 1, cnt = 0, pos = 0; loop && pos < len; ) {
    errno = 0;
    cnt = read (stm->data.file.fd, buf + pos, len - pos);
    /* increase position */
    if (cnt > 0) {
      pos += (size_t)cnt;
    }
    /* evaluate status */
    if (pos < len) {
      if (errno == 0) {
        stm->eof = 1;
        loop = 0;
      } else if (errno != EINTR) {
        stm->err = errno;
        loop = 0;
      }
    }
  }

  cnt = len - pos;
  if (stm->err) {
    if (cnt < 1) {
      cnt = -1;
    }
  } else {
    errno = err;
  }

  return (cnt);
}

static ssize_t
razor2_stream_mem_read (void *buf, size_t len, razor2_stream_t *stm)
{
  assert (buf);
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_MEM ||
          stm->type == RAZOR2_STREAM_PIPE);

  /* end of stream */
  if (stm->data.mem.pos >= stm->data.mem.len) {
    stm->eof = 1;
    return (0);
  }

  if (len > (stm->data.mem.len - stm->data.mem.pos))
    len = stm->data.mem.len - stm->data.mem.pos;

  memcpy (buf, stm->data.mem.buf + stm->data.mem.pos, len);
  stm->data.mem.pos += len;

  if (stm->data.mem.pos >= stm->data.mem.len)
    stm->eof = 1;

  return ((ssize_t) len);
}

ssize_t
razor2_stream_read (void *buf, size_t len, razor2_stream_t *stm)
{
  assert (stm && stm->read);
  return stm->read (buf, len, stm);
}

static int
razor2_stream_file_seek (razor2_stream_t *stm, off_t off, int src)
{
  off_t ret;

  assert (stm);
  assert (stm->type == RAZOR2_STREAM_FILE);

  ret = lseek (stm->data.file.fd, off, src);
  if (ret != 0)
    stm->err = errno;

  return (ret);
}

static int
razor2_stream_mem_seek (razor2_stream_t *stm, off_t off, int src)
{
  size_t max;

  assert (stm);
  assert (stm->type == RAZOR2_STREAM_MEM ||
          stm->type == RAZOR2_STREAM_PIPE);

  switch (src) {
    /* position is n bytes from start */
    case SEEK_SET:
      if (off < 0 || (uintmax_t)off >= (uintmax_t)stm->data.mem.len)
        goto beyond;
      stm->data.mem.pos = (size_t)off;
      break;
    /* position is n bytes from current offset */
    case SEEK_CUR:
      if (off > 0) {
        max = stm->data.mem.len - stm->data.mem.pos;
        if ((uintmax_t)off >= (uintmax_t)max)
          goto beyond;
        stm->data.mem.pos += (size_t)off;
      } else if (off < 0) {
        if ((uintmax_t)(off * -1) > (uintmax_t)stm->data.mem.pos)
          goto limit;
        stm->data.mem.pos -= (size_t)(off * -1);
      }
      break;
    /* position is n bytes from end */
    case SEEK_END:
      if (off > 0 || (uintmax_t)(off * -1) > (uintmax_t)stm->data.mem.len)
        goto limit;
      stm->data.mem.pos = stm->data.mem.len - (size_t)(off * -1);
      break;
    default:
      errno = EINVAL;
      stm->err = errno;
      return (-1);
  }

  return (0);
beyond:
  errno = EFBIG;
  stm->err = errno;
  return (-1);
limit:
  errno = EINVAL;
  stm->err = errno;
  return (-1);
}

/* move stream offset */
int
razor2_stream_seek (razor2_stream_t *stm, off_t off, int src)
{
  assert (stm && stm->seek);
  return stm->seek (stm, off, src);
}

static off_t
razor2_stream_file_tell (razor2_stream_t *stm)
{
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_FILE);

  return lseek (stm->data.file.fd, 0, SEEK_CUR);
}

static off_t
razor2_stream_mem_tell (razor2_stream_t *stm)
{
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_MEM ||
          stm->type == RAZOR2_STREAM_PIPE);

  if ((uintmax_t)stm->data.mem.pos > (uintmax_t)OFF_T_MAX) {
    errno = EOVERFLOW;
    stm->err = errno;
    return (-1);
  }

  return (off_t)stm->data.mem.pos;
}

/* return stream offset */
off_t
razor2_stream_tell (razor2_stream_t *stm)
{
  assert (stm && stm->tell);
  return stm->tell (stm);
}

int razor2_stream_eof (razor2_stream_t *);
int razor2_stream_error (razor2_stream_t *);

static int
razor2_stream_file_close (razor2_stream_t *stm)
{
  int err;

  assert (stm);
  assert (stm->type == RAZOR2_STREAM_FILE);

  err = 0;

  if (stm && stm->type == RAZOR2_STREAM_FILE) {
    if (stm->data.file.fd >= 0 && (err = close (stm->data.file.fd)) != 0) {
      stm->err = errno;
      return (-1);
    }
    free (stm);
  }
  return (0);
}

static int
razor2_stream_mem_close (razor2_stream_t *stm)
{
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_MEM);

  if (stm && stm->type == RAZOR2_STREAM_MEM) {
    free (stm);
  }
  return (0);
}

static int
razor2_stream_pipe_close (razor2_stream_t *stm)
{
  assert (stm);
  assert (stm->type == RAZOR2_STREAM_PIPE);

  if (stm && stm->type == RAZOR2_STREAM_PIPE) {
    if (stm->data.mem.buf)
      free (stm->data.mem.buf);
    free (stm);
  }
  return (0);
}

int
razor2_stream_close (razor2_stream_t *stm)
{
  assert (stm && stm->close);
  return stm->close (stm);
}

int
razor2_stream_error (razor2_stream_t *stm)
{
  assert (stm);
  if (stm && stm->err)
    return (stm->err);
  return (0);
}

int
razor2_stream_eof (razor2_stream_t *stm)
{
  assert (stm);
  return stm->eof ? 1 : 0;
}

