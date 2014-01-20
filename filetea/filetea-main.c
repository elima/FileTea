/*
 * filetea-main.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2013-2014, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License at http://www.gnu.org/licenses/agpl.html
 * for more details.
 */

#include <evd.h>

#include "filetea-protocol.h"

#define DEFAULT_SERVICE_URL "http://localhost:8080"
#define SERVICE_HOST        "localhost:8080"
#define BLOCK_SIZE          4096

struct SharedFile
{
  GFile *file;
  const gchar *file_type;
  guint64 file_time_modified;
  FileteaSource *source;
};

struct PushRequest
{
  struct SharedFile *shared_file;
  EvdHttpConnection *conn;
  gchar *transfer_url;
  gboolean is_chunked;
  goffset byte_start;
  goffset byte_end;
  GInputStream *input_stream;
  void *buf;
  gsize total_sent;
  GCancellable *cancellable;
  gboolean keep_alive;
};

struct SharedFile shared_file;

static FileteaProtocol *protocol;
static EvdConnectionPool *conn_pool;
static gint reconnect_timeout = 1000;

static gchar *ws_service_url = NULL;
static gboolean tls_enabled = FALSE;

static gchar *service_url = NULL;
static gboolean is_public = FALSE;

static GOptionEntry entries[] = {
  { "service", 's', 0, G_OPTION_ARG_STRING, &service_url, "Target service URL, '" DEFAULT_SERVICE_URL "' by default" },
  { "public", 'p', 0, G_OPTION_ARG_NONE, &is_public, "Share the file publicly" },
  { NULL }
};

static gboolean    transport_reconnect       (gpointer user_data);

static void        protocol_register_sources (void);

static void        push_request_read_block   (struct PushRequest *push_req);
static void        push_request_free         (struct PushRequest *push_req);

static void
transport_on_open (GObject      *obj,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  EvdTransport *transport = EVD_TRANSPORT (user_data);
  GError *error = NULL;

  if (! evd_transport_open_finish (EVD_TRANSPORT (obj),
                                   res,
                                   &error))
    {
      g_printerr ("Error connecting to FileTea server: %s\n", error->message);
      g_error_free (error);

      evd_timeout_add (NULL,
                       reconnect_timeout,
                       G_PRIORITY_LOW,
                       transport_reconnect,
                       transport);
      return;
    }
}

static gboolean
transport_reconnect (gpointer user_data)
{
  EvdTransport *transport = EVD_TRANSPORT (user_data);

  evd_transport_open (EVD_TRANSPORT (transport),
                      ws_service_url,
                      NULL,
                      transport_on_open,
                      NULL);
  return FALSE;
}

static void
protocol_on_register_sources (GObject      *obj,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  GList *sources = NULL;
  GError *error = NULL;

  if (! filetea_protocol_register_sources_finish (FILETEA_PROTOCOL (obj),
                                                  res,
                                                  &sources,
                                                  &error))
    {
      g_printerr ("Failed to register sources: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      GList *node;
      FileteaSource *source;

      node = sources;
      while (node != NULL)
        {
          gchar *url;

          source = FILETEA_SOURCE (node->data);

          error = filetea_source_get_error (source);
          if (error != NULL)
            {
              g_printerr ("Error registering source '%s': %s\n",
                          filetea_source_get_name (source),
                          error->message);

              g_object_unref (source);
              source = NULL;
              protocol_register_sources ();
            }
          else
            {
              url = g_strdup_printf ("%s/%s",
                                     service_url,
                                     filetea_source_get_id (source));
              g_print ("%s\n", url);
              g_free (url);
            }

          node = g_list_next (node);
        }
    }

  g_list_free (sources);
}

static void
protocol_register_sources (void)
{
  GList *sources = NULL;

  sources = g_list_append (sources, shared_file.source);

  filetea_protocol_register_sources (protocol,
                                   filetea_source_get_peer (shared_file.source),
                                   sources,
                                   NULL,
                                   protocol_on_register_sources,
                                   NULL);
}

static void
push_request_free (struct PushRequest *push_req)
{
  if (push_req->buf != NULL)
    g_slice_free1 (BLOCK_SIZE, push_req->buf);

  if (push_req->input_stream != NULL)
    g_object_unref (push_req->input_stream);

  if (push_req->conn != NULL)
    {
      if (! push_req->keep_alive)
        g_io_stream_close (G_IO_STREAM (push_req->conn), NULL, NULL);

      g_object_unref (push_req->conn);
    }

  if (push_req->cancellable != NULL)
    g_object_unref (push_req->cancellable);
  g_free (push_req->transfer_url);

  g_slice_free (struct PushRequest, push_req);
}

static void
push_request_on_response (GObject      *obj,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  struct PushRequest *push_req = user_data;
  SoupMessageHeaders *headers;
  guint status_code;
  GError *error = NULL;

  headers =
    evd_http_connection_read_response_headers_finish (EVD_HTTP_CONNECTION (obj),
                                                      res,
                                                      NULL,
                                                      &status_code,
                                                      NULL,
                                                      &error);
  if (headers == NULL)
    {
      g_printerr ("Error reading file push reponse headers: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      soup_message_headers_free (headers);

      /* recycle the connection */
      push_req->keep_alive = TRUE;
      evd_connection_pool_recycle (conn_pool, EVD_CONNECTION (push_req->conn));
    }

  push_request_free (push_req);
}

static void
push_request_on_block_read (GObject      *obj,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  struct PushRequest *push_req = user_data;
  GError *error = NULL;
  gssize size;

  if (g_io_stream_is_closed (G_IO_STREAM (push_req->conn)))
    {
      push_request_free (push_req);
      return;
    }

  size = g_input_stream_read_finish (G_INPUT_STREAM (obj), res, &error);
  if (size < 0)
    {
      g_printerr ("Error reading from file: %s\n", error->message);
      g_error_free (error);

      /* abort and free push request */
      push_request_free (push_req);
    }
  else if (size > 0)
    {
      gsize bytes_left;

      push_req->total_sent += size;
      bytes_left = filetea_source_get_size (push_req->shared_file->source) - push_req->total_sent;

      if (! evd_http_connection_write_content (push_req->conn,
                                               (gchar *) push_req->buf,
                                               size,
                                               bytes_left > 0,
                                               &error))
        {
          g_printerr ("Error sending file contents: %s\n", error->message);
          g_error_free (error);

          /* abort and free push request */
          push_request_free (push_req);
        }
      else if (bytes_left > 0)
        {
          /* continue reading */
          push_request_read_block (push_req);
        }
      else
        {
          /* done pushing file, read response headers */
          evd_http_connection_read_response_headers (push_req->conn,
                                                     NULL,
                                                     push_request_on_response,
                                                     push_req);
        }
    }
  else
    {
      g_assert_not_reached ();
    }
}

static void
push_request_conn_can_write (EvdConnection *conn, gpointer user_data)
{
  struct PushRequest *push_req = user_data;

  g_signal_handlers_disconnect_by_func (conn,
                                        "write",
                                        push_request_conn_can_write);

  push_request_read_block (push_req);
}

static void
push_request_read_block (struct PushRequest *push_req)
{
  if (evd_connection_get_max_writable (EVD_CONNECTION (push_req->conn)) == 0)
    {
      g_signal_connect (push_req->conn,
                        "write",
                        G_CALLBACK (push_request_conn_can_write),
                        push_req);
      return;
    }

  g_input_stream_read_async (push_req->input_stream,
                             push_req->buf,
                             BLOCK_SIZE,
                             G_PRIORITY_DEFAULT,
                             push_req->cancellable,
                             push_request_on_block_read,
                             push_req);
}

static void
push_request_on_file_open (GObject      *obj,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  GFileInputStream *input_stream;
  GError *error = NULL;
  struct PushRequest *push_req = user_data;

  input_stream = g_file_read_finish (G_FILE (obj),
                                     res,
                                     &error);
  if (input_stream == NULL)
    {
      g_printerr ("Error opening file for reading: %s\n", error->message);
      g_error_free (error);

      push_request_free (push_req);
      return;
    }

  push_req->input_stream = G_INPUT_STREAM (input_stream);

  /* start reading from file */
  push_req->buf = g_slice_alloc (BLOCK_SIZE);
  push_request_read_block (push_req);
}

static void
push_request_on_headers_sent (GObject      *obj,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  struct PushRequest *push_req = user_data;
  GError *error = NULL;

  if (! evd_http_connection_write_request_headers_finish (EVD_HTTP_CONNECTION (obj),
                                                          res,
                                                          &error))
    {
      g_printerr ("Error writing transfer request headers: %s\n", error->message);
      g_error_free (error);

      push_request_free (push_req);
      return;
    }

  g_file_read_async (shared_file.file,
                     G_PRIORITY_DEFAULT,
                     NULL,
                     push_request_on_file_open,
                     push_req);
}

static void
on_connection (GObject      *obj,
               GAsyncResult *res,
               gpointer      user_data)
{
  GError *error = NULL;
  EvdHttpRequest *request;
  struct PushRequest *push_req = user_data;

  push_req->conn = EVD_HTTP_CONNECTION
    (evd_connection_pool_get_connection_finish (EVD_CONNECTION_POOL (obj),
                                                res,
                                                &error));
  if (push_req->conn == NULL)
    {
      g_printerr ("Error setting up data connection: %s\n", error->message);
      g_error_free (error);

      push_request_free (push_req);
      return;
    }

  request = evd_http_request_new (SOUP_METHOD_POST, push_req->transfer_url);
  g_free (push_req->transfer_url);
  push_req->transfer_url = NULL;

  SoupMessageHeaders *headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
  soup_message_headers_replace (headers, "Connection", "keep-alive");

  evd_http_connection_write_request_headers (push_req->conn,
                                             request,
                                             NULL,
                                             push_request_on_headers_sent,
                                             push_req);
  g_object_unref (request);
}

static void
on_shared_file_info (GObject      *obj,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  GFileInfo *file_info;
  GError *error = NULL;
  struct PushRequest *push_req = user_data;

  file_info = g_file_query_info_finish (G_FILE (obj),
                                        res,
                                        &error);
  if (file_info == NULL)
    {
      g_printerr ("Error quering file info: %s\n", error->message);
      g_error_free (error);
      return;
    }

  filetea_source_set_size (shared_file.source,
                           g_file_info_get_attribute_uint64 (file_info,
                                                             "standard::size"));
  g_object_unref (file_info);

  /* get an HTTP connection with the service */
  evd_connection_pool_get_connection (conn_pool,
                                      NULL,
                                      on_connection,
                                      push_req);
}

static void
protocol_seeder_push_request (FileteaProtocol *protocol,
                              GAsyncResult    *result,
                              const gchar     *source_id,
                              const gchar     *transfer_id,
                              gboolean         is_chunked,
                              SoupRange       *byte_range,
                              gpointer         user_data)
{
  struct PushRequest *push_req;

  g_assert_cmpint (g_strcmp0 (filetea_source_get_id (shared_file.source),
                              source_id),
                   ==, 0);

  push_req = g_slice_new0 (struct PushRequest);
  push_req->shared_file = &shared_file;
  push_req->transfer_url = g_strdup_printf ("%s/%s", service_url, transfer_id);
  push_req->is_chunked = is_chunked;
  if (is_chunked)
    {
      g_assert (byte_range != NULL);
      push_req->byte_start = byte_range->start;
      push_req->byte_end = byte_range->end;
    }

  /* query file size again, in case it changed until last registration */
  g_file_query_info_async (shared_file.file,
                           "standard::size",
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL,
                           on_shared_file_info,
                           push_req);

  if (is_chunked)
    {
      /* @TODO: handle partial requests */
    }
}

static void
transport_on_new_peer (EvdTransport *transport,
                       EvdPeer      *_peer,
                       gpointer      user_data)
{
  filetea_source_set_peer (shared_file.source, _peer);

  protocol_register_sources ();
}

static void
transport_on_peer_closed (EvdTransport *transport,
                          EvdPeer      *_peer,
                          gboolean      gracefully,
                          gpointer      user_data)
{
  evd_timeout_add (NULL,
                   reconnect_timeout,
                   G_PRIORITY_LOW,
                   transport_reconnect,
                   transport);
}

static gchar *
get_websocket_service_url (const gchar *service_url)
{
  SoupURI *uri;
  guint port;
  gchar *ws_url;

  uri = soup_uri_new (service_url);
  port = soup_uri_get_port (uri);

  if (g_strcmp0 (soup_uri_get_scheme (uri), "http") == 0)
    {
      soup_uri_set_scheme (uri, "ws");
    }
  else if (g_strcmp0 (soup_uri_get_scheme (uri), "https") == 0)
    {
      soup_uri_set_scheme (uri, "wss");
      tls_enabled = TRUE;
    }
  else
    {
      soup_uri_free (uri);
      return NULL;
    }

  soup_uri_set_port (uri, port);
  soup_uri_set_path (uri, "/transport/ws");
  ws_url = soup_uri_to_string (uri, FALSE);
  soup_uri_free (uri);

  return ws_url;
}

gint
main (gint argc, gchar *argv[])
{
  gint exit_status = 0;
  EvdDaemon *evd_daemon = NULL;

  GError *error = NULL;
  GOptionContext *context = NULL;
  GFileInfo *file_info = NULL;
  FileteaProtocolVTable vtable;
  EvdJsonrpc *rpc;
  EvdWebsocketClient *transport = NULL;

  gchar *file_name;
  gchar *base_name;
  const gchar *file_type;
  gsize file_size;

  if (! evd_tls_init (&error))
    {
      g_printerr ("Error initializing TLS: %s\n", error->message);
      g_error_free (error);
      exit_status = -1;
      goto out;
    }

  /* parse command line args */
  context = g_option_context_new ("- low friction file sharing client");
  g_option_context_add_main_entries (context, entries, NULL);
  if (! g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing commandline options: %s\n", error->message);
      g_error_free (error);
      exit_status = -1;
      goto out;
    }
  g_option_context_free (context);

  if (service_url == NULL)
    service_url = g_strdup (DEFAULT_SERVICE_URL);

  if (argc > 2)
    {
      /* sharing more than one file is currently not supported */
      g_printerr ("Error parsing commandline options: Sharing more than one file is currently not supported\n");
      exit_status = -1;
      goto out;
    }
  else if (argc > 1)
    {
      /* one file has been specified, use it as source */
      file_name = g_strdup (argv[1]);
    }
  else
    {
      /* use standard input as data source, currently not supported */
      g_printerr ("Error parsing commandline options: No file specified and reading from standard input is currently not supported\n");
      exit_status = -1;
      goto out;
    }

  /* resolve WebSocket service URL */
  ws_service_url = get_websocket_service_url (service_url);
  if (ws_service_url == NULL)
    {
      g_printerr ("Error, invalid service URL '%s'\n", service_url);
      exit_status = -1;
      goto out;
    }

  /* query file info */
  shared_file.file = g_file_new_for_path (file_name);
  file_info = g_file_query_info (shared_file.file,
                                 "standard::size,standard::content-type,time::modified",
                                 G_FILE_QUERY_INFO_NONE,
                                 NULL,
                                 &error);
  if (file_info == NULL)
    {
      g_printerr ("Error quering file info: %s\n", error->message);
      g_error_free (error);
      exit_status = -1;
      goto out;
    }

  file_size =
    g_file_info_get_attribute_uint64 (file_info, "standard::size");
  file_type =
    g_file_info_get_attribute_string (file_info, "standard::content-type");
  shared_file.file_time_modified =
    g_file_info_get_attribute_uint64 (file_info, "time::modified");

  base_name = g_path_get_basename (file_name);
  g_free (file_name);
  shared_file.source = filetea_source_new (NULL,
                                           base_name,
                                           file_type,
                                           file_size,
                                           FILETEA_SOURCE_FLAGS_CHUNKABLE |
                                           FILETEA_SOURCE_FLAGS_PUBLIC,
                                           NULL);
  g_free (base_name);
  g_object_unref (file_info);

  /* protocol */
  vtable.seeder_push_request = protocol_seeder_push_request;

  protocol = filetea_protocol_new (&vtable,
                                   NULL,
                                   NULL);
  rpc = filetea_protocol_get_rpc (protocol);

  /* transport */
  transport = evd_websocket_client_new ();
  evd_ipc_mechanism_use_transport (EVD_IPC_MECHANISM (rpc),
                                   EVD_TRANSPORT (transport));

  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (transport_on_new_peer),
                    NULL);
  g_signal_connect (transport,
                    "peer-closed",
                    G_CALLBACK (transport_on_peer_closed),
                    NULL);

  transport_reconnect (transport);

  /* connection pool */
  conn_pool = evd_connection_pool_new (SERVICE_HOST,
                                       EVD_TYPE_HTTP_CONNECTION);
  if (tls_enabled)
    evd_connection_pool_set_tls_autostart (conn_pool, TRUE);

  /* main daemon */
  evd_daemon = evd_daemon_get_default (&argc, &argv);

  /* start the show */
  if ((exit_status = evd_daemon_run (evd_daemon, &error)) < 0)
    {
      g_printerr ("Error running daemon: %s\n", error->message);
      g_error_free (error);
    }

 out:
  /* free stuff */
  if (shared_file.file != NULL)
    g_object_unref (shared_file.file);

  if (evd_daemon != NULL)
    g_object_unref (evd_daemon);
  if (protocol != NULL)
    g_object_unref (protocol);
  if (transport != NULL)
    g_object_unref (transport);
  if (conn_pool != NULL)
    g_object_unref (conn_pool);

  if (shared_file.source != NULL)
    g_object_unref (shared_file.source);

  g_free (service_url);
  g_free (ws_service_url);

  evd_tls_deinit ();

  g_debug ("\nFileTea client terminated\n");

  return exit_status;
}
