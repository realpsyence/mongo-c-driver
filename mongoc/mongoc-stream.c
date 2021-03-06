/*
 * Copyright 2013 MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define _GNU_SOURCE

#include <bson.h>

#include "mongoc-array-private.h"
#include "mongoc-buffer-private.h"
#include "mongoc-error.h"
#include "mongoc-flags.h"
#include "mongoc-log.h"
#include "mongoc-opcode.h"
#include "mongoc-rpc-private.h"
#include "mongoc-stream.h"
#include "mongoc-stream-private.h"


#ifndef MONGOC_DEFAULT_TIMEOUT_MSEC
#define MONGOC_DEFAULT_TIMEOUT_MSEC (60L * 60L * 1000L)
#endif


/**
 * mongoc_stream_close:
 * @stream: A mongoc_stream_t.
 *
 * Closes the underlying file-descriptor used by @stream.
 *
 * Returns: 0 on success, -1 on failure.
 */
int
mongoc_stream_close (mongoc_stream_t *stream)
{
   bson_return_val_if_fail(stream, -1);
   return stream->close(stream);
}


/**
 * mongoc_stream_destroy:
 * @stream: A mongoc_stream_t.
 *
 * Frees any resources referenced by @stream, including the memory allocation
 * for @stream.
 */
void
mongoc_stream_destroy (mongoc_stream_t *stream)
{
   bson_return_if_fail(stream);
   stream->destroy(stream);
}


/**
 * mongoc_stream_flush:
 * @stream: A mongoc_stream_t.
 *
 * Flushes the data in the underlying stream to the transport.
 *
 * Returns: 0 on success, -1 on failure.
 */
int
mongoc_stream_flush (mongoc_stream_t *stream)
{
   bson_return_val_if_fail(stream, -1);
   return stream->flush(stream);
}


/**
 * mongoc_stream_writev:
 * @stream: A mongoc_stream_t.
 * @iov: An array of iovec to write to the stream.
 * @iovcnt: The number of elements in @iov.
 *
 * Writes an array of iovec buffers to the underlying stream.
 *
 * Returns: the number of bytes written, or -1 upon failure.
 */
ssize_t
mongoc_stream_writev (mongoc_stream_t *stream,
                      struct iovec    *iov,
                      size_t           iovcnt,
                      int32_t     timeout_msec)
{
   bson_return_val_if_fail(stream, -1);
   bson_return_val_if_fail(iov, -1);
   bson_return_val_if_fail(iovcnt, -1);

   BSON_ASSERT(stream->writev);

   if (timeout_msec < 0) {
      timeout_msec = MONGOC_DEFAULT_TIMEOUT_MSEC;
   }

   return stream->writev(stream, iov, iovcnt, timeout_msec);
}


/**
 * mongoc_stream_readv:
 * @stream: A mongoc_stream_t.
 * @iov: An array of iovec containing the location and sizes to read.
 * @iovcnt: the number of elements in @iov.
 * @min_bytes: the minumum number of bytes to return, or -1.
 *
 * Reads into the various buffers pointed to by @iov and associated
 * buffer lengths.
 *
 * If @min_bytes is specified, then at least min_bytes will be returned unless
 * eof is encountered.  This may result in ETIMEDOUT
 *
 * Returns: the number of bytes read or -1 on failure.
 */
ssize_t
mongoc_stream_readv (mongoc_stream_t *stream,
                     struct iovec    *iov,
                     size_t           iovcnt,
                     size_t           min_bytes,
                     int32_t     timeout_msec)
{
   bson_return_val_if_fail(stream, -1);
   bson_return_val_if_fail(iov, -1);
   bson_return_val_if_fail(iovcnt, -1);

   BSON_ASSERT(stream->readv);

   if (timeout_msec < 0) {
      timeout_msec = MONGOC_DEFAULT_TIMEOUT_MSEC;
   }

   return stream->readv(stream, iov, iovcnt, min_bytes, timeout_msec);
}


/**
 * mongoc_stream_read:
 * @stream: A mongoc_stream_t.
 * @buf: A buffer to write into.
 * @count: The number of bytes to write into @buf.
 * @min_bytes: The minimum number of bytes to receive
 *
 * Simplified access to mongoc_stream_readv(). Creates a single iovec
 * with the buffer provided.
 *
 * If @min_bytes is specified, then at least min_bytes will be returned unless
 * eof is encountered.  This may result in ETIMEDOUT
 *
 * Returns: -1 on failure, otherwise the number of bytes read.
 */
ssize_t
mongoc_stream_read (mongoc_stream_t *stream,
                    void            *buf,
                    size_t           count,
                    size_t           min_bytes,
                    int32_t     timeout_msec)
{
   struct iovec iov;

   bson_return_val_if_fail(stream, -1);
   bson_return_val_if_fail(buf, -1);

   iov.iov_base = buf;
   iov.iov_len = count;

   BSON_ASSERT(stream->readv);

   return mongoc_stream_readv(stream, &iov, 1, min_bytes, timeout_msec);
}


/**
 * mongoc_stream_cork:
 * @stream: (in): A mongoc_stream_t.
 *
 * Corks a stream, preventing packets from being sent immediately. This is
 * useful if you need to send multiple messages together as a single packet.
 *
 * Call mongoc_stream_uncork() after writing your data.
 *
 * Returns: 0 on success, -1 on failure.
 */
int
mongoc_stream_cork (mongoc_stream_t *stream)
{
   bson_return_val_if_fail(stream, -1);
   return stream->cork ? stream->cork(stream) : 0;
}


/**
 * mongoc_stream_uncork:
 * @stream: (in): A mongoc_stream_t.
 *
 * Uncorks a stream, previously corked with mongoc_stream_cork().
 *
 * Returns: 0 on success, -1 on failure.
 */
int
mongoc_stream_uncork (mongoc_stream_t *stream)
{
   bson_return_val_if_fail(stream, -1);
   return stream->uncork ? stream->uncork(stream) : 0;
}


int
mongoc_stream_setsockopt (mongoc_stream_t *stream,
                          int              level,
                          int              optname,
                          void            *optval,
                          socklen_t        optlen)
{
   bson_return_val_if_fail(stream, -1);

   if (stream->setsockopt) {
      return stream->setsockopt(stream, level, optname, optval, optlen);
   }

   return 0;
}
