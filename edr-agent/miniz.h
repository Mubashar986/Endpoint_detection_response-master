/* miniz.h v2.2.0 - MIT License */
#ifndef MINIZ_H
#define MINIZ_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return status codes. */
enum {
  MZ_OK = 0,
  MZ_STREAM_END = 1,
  MZ_NEED_DICT = 2,
  MZ_ERRNO = -1,
  MZ_STREAM_ERROR = -2,
  MZ_DATA_ERROR = -3,
  MZ_MEM_ERROR = -4,
  MZ_BUF_ERROR = -5,
  MZ_VERSION_ERROR = -6,
  MZ_PARAM_ERROR = -10000
};

/* Compression/Decompression stream structure. */
typedef struct mz_stream_s {
  const unsigned char *next_in;
  unsigned int avail_in;
  unsigned long total_in;

  unsigned char *next_out;
  unsigned int avail_out;
  unsigned long total_out;

  char *msg;
  struct mz_internal_state *state;

  void *(*zalloc)(void *opaque, unsigned int items, unsigned int size);
  void (*zfree)(void *opaque, void *address);
  void *opaque;

  int data_type;
  unsigned long adler;
  unsigned long reserved;
} mz_stream;

typedef mz_stream *mz_streamp;

/* Initialize stream for compression. */
int mz_deflateInit(mz_streamp pStream, int level);

/* Compress data. */
int mz_deflate(mz_streamp pStream, int flush);

/* End compression. */
int mz_deflateEnd(mz_streamp pStream);

/* Helper: Compress entire buffer in one go. */
unsigned long mz_compressBound(unsigned long sourceLen);
int mz_compress(unsigned char *pDest, unsigned long *pDest_len, const unsigned char *pSource, unsigned long source_len);

/* Helper: Calculate CRC32 */
unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* MINIZ_H */
