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

#include "mock-server.h"
#include "mongoc-buffer-private.h"
#include "mongoc-stream-unix.h"
#include "mongoc-trace.h"


struct _mock_server_t
{
   mock_server_handler_t  handler;
   void                  *handler_data;

   mongoc_thread_t          main_thread;
   bool            using_main_thread;

   const char            *address;

   uint16_t          port;
   mongoc_fd_t              socket;

   int                    last_response_id;

   bool            isMaster;
   int                    minWireVersion;
   int                    maxWireVersion;
   int                    maxBsonObjectSize;
   int                    maxMessageSizeBytes;
};


void
mock_server_reply_simple (mock_server_t        *server,
                          mongoc_stream_t      *client,
                          const mongoc_rpc_t   *request,
                          mongoc_reply_flags_t  flags,
                          const bson_t         *doc)
{
   struct iovec *iov;
   mongoc_array_t ar;
   mongoc_rpc_t r = {{ 0 }};
   size_t expected = 0;
   size_t n_written;
   int iovcnt;
   int i;

   BSON_ASSERT (server);
   BSON_ASSERT (request);
   BSON_ASSERT (client);
   BSON_ASSERT (doc);

   _mongoc_array_init (&ar, sizeof (struct iovec));

   r.reply.msg_len = 0;
   r.reply.request_id = ++server->last_response_id;
   r.reply.response_to = request->header.request_id;
   r.reply.opcode = MONGOC_OPCODE_REPLY;
   r.reply.flags = 0;
   r.reply.cursor_id = 0;
   r.reply.start_from = 0;
   r.reply.n_returned = 1;
   r.reply.documents = bson_get_data (doc);
   r.reply.documents_len = doc->len;

   _mongoc_rpc_gather (&r, &ar);
   _mongoc_rpc_swab_to_le (&r);

   iov = ar.data;
   iovcnt = (int)ar.len;

   for (i = 0; i < iovcnt; i++) {
      expected += iov[i].iov_len;
   }

   n_written = mongoc_stream_writev (client, iov, iovcnt, -1);

   assert (n_written == expected);

   _mongoc_array_destroy (&ar);
}


static bool
handle_ping (mock_server_t   *server,
             mongoc_stream_t *client,
             mongoc_rpc_t    *rpc,
             const bson_t    *doc)
{
   bson_t reply = BSON_INITIALIZER;

   bson_append_int32 (&reply, "ok", 2, 1);
   mock_server_reply_simple (server, client, rpc, MONGOC_REPLY_NONE, &reply);
   bson_destroy (&reply);

   return true;
}


static bool
handle_ismaster (mock_server_t   *server,
                 mongoc_stream_t *client,
                 mongoc_rpc_t    *rpc,
                 const bson_t    *doc)
{
   bson_t reply_doc = BSON_INITIALIZER;
   time_t now = time (NULL);

   BSON_ASSERT (server);
   BSON_ASSERT (client);
   BSON_ASSERT (rpc);
   BSON_ASSERT (doc);

   bson_append_bool (&reply_doc, "ismaster", -1, server->isMaster);
   bson_append_int32 (&reply_doc, "maxBsonObjectSize", -1,
                      server->maxBsonObjectSize);
   bson_append_int32 (&reply_doc, "maxMessageSizeBytes", -1,
                      server->maxMessageSizeBytes);
   bson_append_int32 (&reply_doc, "minWireVersion", -1,
                      server->minWireVersion);
   bson_append_int32 (&reply_doc, "maxWireVersion", -1,
                      server->maxWireVersion);
   bson_append_double (&reply_doc, "ok", -1, 1.0);
   bson_append_time_t (&reply_doc, "localtime", -1, now);

   mock_server_reply_simple (server, client, rpc, MONGOC_REPLY_NONE, &reply_doc);

   bson_destroy (&reply_doc);

   return true;
}


static bool
handle_command (mock_server_t   *server,
                mongoc_stream_t *client,
                mongoc_rpc_t    *rpc)
{
   int32_t len;
   bool ret = false;
   bson_iter_t iter;
   const char *key;
   bson_t doc;

   BSON_ASSERT (rpc);

   if (rpc->header.opcode != MONGOC_OPCODE_QUERY) {
      return false;
   }

   memcpy (&len, rpc->query.query, 4);
   len = BSON_UINT32_FROM_LE (len);
   if (!bson_init_static (&doc, rpc->query.query, len)) {
      return false;
   }

   if (!bson_iter_init (&iter, &doc) || !bson_iter_next (&iter)) {
      return false;
   }

   key = bson_iter_key (&iter);

   if (!strcasecmp (key, "ismaster")) {
      ret = handle_ismaster (server, client, rpc, &doc);
   } else if (!strcasecmp (key, "ping")) {
      ret = handle_ping (server, client, rpc, &doc);
   }

   bson_destroy (&doc);

   return ret;
}


static void *
mock_server_worker (void *data)
{
   mongoc_buffer_t buffer;
   mongoc_stream_t *stream;
   mock_server_t *server;
   mongoc_rpc_t rpc;
   bson_error_t error;
   int32_t msg_len;
   void **closure = data;

   BSON_ASSERT(closure);

   server = closure[0];
   stream = closure[1];

   _mongoc_buffer_init(&buffer, NULL, 0, NULL);

again:
   if (_mongoc_buffer_fill (&buffer, stream, 4, INT_MAX, &error) == -1) {
      MONGOC_WARNING ("%s", error.message);
      goto failure;
   }

   assert (buffer.len >= 4);

   memcpy(&msg_len, buffer.data + buffer.off, 4);
   msg_len = BSON_UINT32_FROM_LE(msg_len);

   if (msg_len < 16) {
      MONGOC_WARNING ("No data");
      goto failure;
   }

   if (_mongoc_buffer_fill (&buffer, stream, msg_len, INT_MAX, &error) == -1) {
      MONGOC_WARNING ("%s", error.message);
      goto failure;
   }

   assert (buffer.len >= msg_len);

   DUMP_BYTES (buffer, buffer.data + buffer.off, buffer.len);

   if (!_mongoc_rpc_scatter(&rpc, buffer.data + buffer.off, msg_len)) {
      MONGOC_WARNING ("Failed to scatter");
      goto failure;
   }

   _mongoc_rpc_swab_from_le(&rpc);

   if (!handle_command(server, stream, &rpc)) {
      server->handler(server, stream, &rpc, server->handler_data);
   }

   memmove (buffer.data, buffer.data + buffer.off + msg_len,
            buffer.len - msg_len);
   buffer.off = 0;
   buffer.len -= msg_len;

   goto again;

failure:
   mongoc_stream_close(stream);
   mongoc_stream_destroy(stream);
   bson_free(closure);

   return NULL;
}


static void
dummy_handler (mock_server_t   *server,
               mongoc_stream_t *stream,
               mongoc_rpc_t    *rpc,
               void            *user_data)
{
}


mock_server_t *
mock_server_new (const char            *address,
                 uint16_t          port,
                 mock_server_handler_t  handler,
                 void                  *handler_data)
{
   mock_server_t *server;

   if (!address) {
      address = "127.0.0.1";
   }

   if (!port) {
      port = 27017;
   }

   server = bson_malloc0(sizeof *server);
   server->handler = handler ? handler : dummy_handler;
   server->handler_data = handler_data;
   server->socket = MONGOC_FD_INVALID;
   server->address = address;
   server->port = port;

   server->minWireVersion = 0;
   server->maxWireVersion = 0;
   server->isMaster = true;
   server->maxBsonObjectSize = 16777216;
   server->maxMessageSizeBytes = 48000000;

   return server;
}


int
mock_server_run (mock_server_t *server)
{
   struct sockaddr_in saddr;
   mongoc_stream_t *stream;
   mongoc_thread_t thread;
   void **closure;
   int optval;
   mongoc_fd_t sd;
   mongoc_fd_t cd;

   bson_return_val_if_fail(server, -1);
   bson_return_val_if_fail(! mongoc_fd_is_valid(server->socket), -1);

   sd = mongoc_socket(AF_INET, SOCK_STREAM, 0);
   if (! mongoc_fd_is_valid(sd)) {
      perror("Failed to create socket.");
      return -1;
   }

   optval = 1;
   mongoc_setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

   memset(&saddr, 0, sizeof saddr);

   saddr.sin_family = AF_INET;
   saddr.sin_port = htons(server->port);
   /*
    * TODO: Parse server->address.
    */
   saddr.sin_addr.s_addr = htonl(INADDR_ANY);

   if (-1 == mongoc_bind(sd, (struct sockaddr *)&saddr, sizeof saddr)) {
      perror("Failed to bind socket");
      return -1;
   }

   if (-1 == mongoc_listen(sd, 10)) {
      perror("Failed to put socket into listen mode");
      return 3;
   }

   server->socket = sd;

   for (;;) {
      cd = mongoc_accept(server->socket, NULL, NULL);
      if (! mongoc_fd_is_valid(cd)) {
         perror("Failed to accept client socket");
         return -1;
      }

      stream = mongoc_stream_unix_new(cd);
      closure = bson_malloc0(sizeof(void*) * 2);
      closure[0] = server;
      closure[1] = stream;

      mongoc_thread_create(&thread, mock_server_worker, closure);
   }

   mongoc_close(server->socket);
   server->socket = MONGOC_FD_INVALID;

   return 0;
}


static void *
main_thread (void *data)
{
   mock_server_t *server = data;

   mock_server_run (server);

   return NULL;
}


void
mock_server_run_in_thread (mock_server_t *server)
{
   BSON_ASSERT (server);

   server->using_main_thread = true;
   mongoc_thread_create (&server->main_thread, main_thread, server);
}


void
mock_server_quit (mock_server_t *server,
                  int            code)
{
   bson_return_if_fail(server);

   /*
    * TODO: Exit server loop.
    */
}


void
mock_server_destroy (mock_server_t *server)
{
   if (server) {
      bson_free(server);
   }
}


void
mock_server_set_wire_version (mock_server_t *server,
                              int32_t   min_wire_version,
                              int32_t   max_wire_version)
{
   BSON_ASSERT (server);

   server->minWireVersion = min_wire_version;
   server->maxWireVersion = max_wire_version;
}
