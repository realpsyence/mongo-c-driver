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


#ifndef MONGOC_CURSOR_PRIVATE_H
#define MONGOC_CURSOR_PRIVATE_H


#include <bson.h>

#include "mongoc-client.h"
#include "mongoc-buffer-private.h"
#include "mongoc-rpc-private.h"


BSON_BEGIN_DECLS


typedef struct mongoc_cursor_interface
{
   mongoc_cursor_t * (*clone)(const mongoc_cursor_t * cursor);

   void (*destroy)(mongoc_cursor_t *cursor);

   bool (*more)(mongoc_cursor_t *cursor);

   bool (*next)(mongoc_cursor_t *cursor,
                       const bson_t   **bson);

   bool (*error)(mongoc_cursor_t *cursor,
                        bson_error_t    *error);

   void (*get_host)(mongoc_cursor_t    *cursor,
                    mongoc_host_list_t *host);
} mongoc_cursor_interface_t;


struct _mongoc_cursor_t
{
   mongoc_client_t     *client;

   uint32_t        hint;
   uint32_t        stamp;

   bool          is_command   : 1;
   bool          sent         : 1;
   bool          done         : 1;
   bool          failed       : 1;
   bool          end_of_event : 1;
   bool          in_exhaust   : 1;

   bson_t               query;
   bson_t               fields;

   mongoc_read_prefs_t *read_prefs;

   mongoc_query_flags_t flags;
   uint32_t        skip;
   uint32_t        limit;
   uint32_t        count;
   uint32_t        batch_size;

   char                 ns[140];
   uint32_t        nslen;

   bson_error_t         error;

   mongoc_rpc_t         rpc;
   mongoc_buffer_t      buffer;
   bson_reader_t       *reader;

   mongoc_cursor_interface_t  iface;
   void                      *iface_data;
};


mongoc_cursor_t *
_mongoc_cursor_new (mongoc_client_t           *client,
                    const char                *db_and_collection,
                    mongoc_query_flags_t       flags,
                    uint32_t              skip,
                    uint32_t              limit,
                    uint32_t              batch_size,
                    bool                is_command,
                    const bson_t              *query,
                    const bson_t              *fields,
                    const mongoc_read_prefs_t *read_prefs)
BSON_GNUC_INTERNAL;

mongoc_cursor_t *
_mongoc_cursor_clone (const mongoc_cursor_t *cursor)
BSON_GNUC_INTERNAL;


void
_mongoc_cursor_destroy (mongoc_cursor_t *cursor)
BSON_GNUC_INTERNAL;


bool
_mongoc_cursor_more (mongoc_cursor_t *cursor)
BSON_GNUC_INTERNAL;


bool
_mongoc_cursor_next (mongoc_cursor_t  *cursor,
                     const bson_t    **bson)
BSON_GNUC_INTERNAL;


bool
_mongoc_cursor_error (mongoc_cursor_t *cursor,
                     bson_error_t    *error)
BSON_GNUC_INTERNAL;


void
_mongoc_cursor_get_host (mongoc_cursor_t    *cursor,
                         mongoc_host_list_t *host)
BSON_GNUC_INTERNAL;


BSON_END_DECLS


#endif /* MONGOC_CURSOR_PRIVATE_H */
