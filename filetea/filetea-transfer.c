/*
 * filetea-transfer.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2011-2014, Igalia S.L.
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

#include "filetea-transfer.h"

G_DEFINE_TYPE (FileteaTransfer, filetea_transfer, G_TYPE_OBJECT)

#define FILETEA_TRANSFER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           FILETEA_TYPE_TRANSFER, \
                                           FileteaTransferPrivate))

#define BLOCK_SIZE 0x4000

#define START_TIMEOUT 30000 /* in miliseconds */

/* private data */
struct _FileteaTransferPrivate
{
  gchar *id;
  guint status;

  EvdWebService *web_service;
  FileteaSource *source;
  EvdHttpConnection *source_conn;
  EvdHttpConnection *target_conn;
  EvdPeer *target_peer;
  GCancellable *cancellable;

  gchar *action;
  gboolean is_chunked;
  SoupRange byte_range;

  gchar *buf;
  gsize transfer_len;
  gsize transferred;
  gdouble bandwidth;

  GSimpleAsyncResult *result;

  gboolean download;

  guint timeout_src_id;
};

static void     filetea_transfer_class_init         (FileteaTransferClass *class);
static void     filetea_transfer_init               (FileteaTransfer *self);

static void     filetea_transfer_finalize           (GObject *obj);
static void     filetea_transfer_dispose            (GObject *obj);


static void     target_connection_on_close          (EvdHttpConnection *conn,
                                                     gpointer           user_data);
static void     source_connection_on_close          (EvdHttpConnection *conn,
                                                     gpointer           user_data);

static void     filetea_transfer_read               (FileteaTransfer *self);
static void     filetea_transfer_flush_target       (FileteaTransfer *self);
static void     filetea_transfer_complete           (FileteaTransfer *self);

static void
filetea_transfer_class_init (FileteaTransferClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = filetea_transfer_dispose;
  obj_class->finalize = filetea_transfer_finalize;

  g_type_class_add_private (obj_class, sizeof (FileteaTransferPrivate));
}

static void
filetea_transfer_init (FileteaTransfer *self)
{
  FileteaTransferPrivate *priv;

  priv = FILETEA_TRANSFER_GET_PRIVATE (self);
  self->priv = priv;

  priv->timeout_src_id = 0;
}

static void
filetea_transfer_dispose (GObject *obj)
{
  FileteaTransfer *self = FILETEA_TRANSFER (obj);

  if (self->priv->source != NULL)
    {
      g_object_unref (self->priv->source);
      self->priv->source = NULL;
    }

  if (self->priv->target_conn != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->target_conn,
                                            target_connection_on_close,
                                            self);
      g_object_unref (self->priv->target_conn);
      self->priv->target_conn = NULL;
    }

  if (self->priv->source_conn != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->source_conn,
                                            source_connection_on_close,
                                            self);
      g_object_unref (self->priv->source_conn);
      self->priv->source_conn = NULL;
    }

  if (self->priv->target_peer != NULL)
    {
      g_object_unref (self->priv->target_peer);
      self->priv->target_peer = NULL;
    }

  G_OBJECT_CLASS (filetea_transfer_parent_class)->dispose (obj);
}

static void
filetea_transfer_finalize (GObject *obj)
{
  FileteaTransfer *self = FILETEA_TRANSFER (obj);

  g_free (self->priv->id);
  g_free (self->priv->action);

  if (self->priv->buf != NULL)
    g_slice_free1 (BLOCK_SIZE, self->priv->buf);

  if (self->priv->cancellable != NULL)
    g_object_unref (self->priv->cancellable);

  if (self->priv->timeout_src_id != 0)
    {
      g_source_remove (self->priv->timeout_src_id);
      self->priv->timeout_src_id = 0;
    }

  g_object_unref (self->priv->web_service);

  G_OBJECT_CLASS (filetea_transfer_parent_class)->finalize (obj);
}

static void
target_connection_on_close (EvdHttpConnection *conn, gpointer user_data)
{
  FileteaTransfer *self = user_data;

  g_simple_async_result_set_error (self->priv->result,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CLOSED,
                                   "Target connection dropped");

  self->priv->status = FILETEA_TRANSFER_STATUS_TARGET_ABORTED;

  filetea_transfer_complete (self);
}

static void
source_connection_on_close (EvdHttpConnection *conn, gpointer user_data)
{
  FileteaTransfer *self = user_data;

  g_simple_async_result_set_error (self->priv->result,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CLOSED,
                                   "Source connection dropped");

  self->priv->status = FILETEA_TRANSFER_STATUS_SOURCE_ABORTED;

  filetea_transfer_complete (self);
}

static void
filetea_transfer_on_target_can_write (EvdConnection *target_conn,
                                      gpointer       user_data)
{
  FileteaTransfer *self = user_data;

  evd_connection_unlock_close (EVD_CONNECTION (self->priv->source_conn));
  filetea_transfer_read (self);
}

static void
filetea_transfer_on_read (GObject      *obj,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  FileteaTransfer *self = user_data;
  gssize size;
  GError *error = NULL;
  EvdStreamThrottle *throttle;

  size = g_input_stream_read_finish (G_INPUT_STREAM (obj), res, &error);
  if (size < 0)
    {
      g_printerr ("ERROR reading from source: %s\n", error->message);

      g_simple_async_result_take_error (self->priv->result, error);
      self->priv->status = FILETEA_TRANSFER_STATUS_ERROR;
      filetea_transfer_complete (self);
      goto out;
    }

  if (! evd_http_connection_write_content (self->priv->target_conn,
                                           self->priv->buf,
                                           size,
                                           TRUE,
                                           &error))
    {
      g_printerr ("ERROR writing to target: %s\n", error->message);

      g_simple_async_result_take_error (self->priv->result, error);
      self->priv->status = FILETEA_TRANSFER_STATUS_ERROR;
      filetea_transfer_complete (self);

      goto out;
    }

  self->priv->transferred += size;

  throttle =
    evd_io_stream_get_input_throttle (EVD_IO_STREAM (self->priv->target_conn));
  self->priv->bandwidth = evd_stream_throttle_get_actual_bandwidth (throttle);

  g_assert (self->priv->transferred <= self->priv->transfer_len);
  if (self->priv->transferred == self->priv->transfer_len)
    {
      /* finished reading, send HTTP response to source */
      g_signal_handlers_disconnect_by_func (self->priv->source_conn,
                                            source_connection_on_close,
                                            self);

      if (! evd_web_service_respond (self->priv->web_service,
                                     self->priv->source_conn,
                                     SOUP_STATUS_OK,
                                     NULL,
                                     NULL,
                                     0,
                                     &error))
        {
          g_printerr ("Error sending response to source: %s\n", error->message);
          g_error_free (error);
          self->priv->status = FILETEA_TRANSFER_STATUS_ERROR;
        }

      filetea_transfer_flush_target (self);
    }
  else
    {
      filetea_transfer_read (self);
    }

 out:
  g_object_unref (self);
}

static void
filetea_transfer_read (FileteaTransfer *self)
{
  GInputStream *stream;

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self->priv->source_conn));

  if (g_input_stream_has_pending (stream))
    {
      g_warning ("has pending\n");
      return;
    }

  if (evd_connection_get_max_writable (EVD_CONNECTION (self->priv->target_conn)) > 0)
    {
      gssize size;

      size = MIN (BLOCK_SIZE,
                  filetea_source_get_size (self->priv->source) - self->priv->transferred);
      if (size <= 0)
        return;

      g_object_ref (self);
      g_input_stream_read_async (stream,
                                 self->priv->buf,
                                 (gsize) size,
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 filetea_transfer_on_read,
                                 self);
    }
  else
    {
      evd_connection_lock_close (EVD_CONNECTION (self->priv->source_conn));
    }
}

static void
filetea_transfer_on_target_flushed (GObject      *obj,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
  FileteaTransfer *self = user_data;
  GError *error = NULL;

  if (! g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, &error))
    {
      /* if the stream was closed during flush it is not exactly an error,
         since it can be expected depending on how fast the target closed
         the connection after receiving all the body content */
      if (error->code != G_IO_ERROR_CLOSED)
        {
          g_printerr ("ERROR flushing target: %s\n", error->message);
          g_error_free (error);
        }
    }

  self->priv->status = FILETEA_TRANSFER_STATUS_COMPLETED;

  g_signal_handlers_disconnect_by_func (self->priv->target_conn,
                                        filetea_transfer_on_target_can_write,
                                        self);

  filetea_transfer_complete (self);

  g_object_unref (self);
}

static void
filetea_transfer_flush_target (FileteaTransfer *self)
{
  GOutputStream *stream;

  stream = g_io_stream_get_output_stream (G_IO_STREAM (self->priv->target_conn));

  g_signal_handlers_disconnect_by_func (self->priv->target_conn,
                                        target_connection_on_close,
                                        self);

  g_object_ref (self);
  g_output_stream_flush_async (stream,
                               G_PRIORITY_DEFAULT,
                               NULL,
                               filetea_transfer_on_target_flushed,
                               self);
}

static void
filetea_transfer_complete (FileteaTransfer *self)
{
  g_signal_handlers_disconnect_by_func (self->priv->target_conn,
                                        target_connection_on_close,
                                        self);
  if (self->priv->source_conn != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->source_conn,
                                            source_connection_on_close,
                                            self);

      if (self->priv->status != FILETEA_TRANSFER_STATUS_COMPLETED &&
          ! g_io_stream_is_closed (G_IO_STREAM (self->priv->source_conn)))
        {
          g_io_stream_close (G_IO_STREAM (self->priv->source_conn), NULL, NULL);
        }
    }

  if (self->priv->status != FILETEA_TRANSFER_STATUS_COMPLETED &&
      ! g_io_stream_is_closed (G_IO_STREAM (self->priv->target_conn)))
    {
      g_io_stream_close (G_IO_STREAM (self->priv->target_conn), NULL, NULL);
    }

  if (self->priv->result != NULL)
    {
      g_simple_async_result_complete_in_idle (self->priv->result);
      g_object_unref (self->priv->result);
      self->priv->result = NULL;
    }
}

static gboolean
on_transfer_start_timeout (gpointer user_data)
{
  FileteaTransfer *self = FILETEA_TRANSFER (user_data);

  self->priv->timeout_src_id = 0;

  if (self->priv->result != NULL)
    {
      g_simple_async_result_set_error (self->priv->result,
                                       G_IO_ERROR,
                                       G_IO_ERROR_TIMED_OUT,
                                       "Timeout starting transfer");

      evd_web_service_respond (self->priv->web_service,
                               self->priv->target_conn,
                               SOUP_STATUS_REQUEST_TIMEOUT,
                               NULL,
                               NULL,
                               0,
                               NULL);

      filetea_transfer_flush_target (self);
    }

  return FALSE;
}

/* public methods */

FileteaTransfer *
filetea_transfer_new (FileteaSource       *source,
                      EvdWebService       *web_service,
                      EvdHttpConnection   *target_conn,
                      const gchar         *action,
                      gboolean             is_chunked,
                      SoupRange           *range,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  FileteaTransfer *self;

  g_return_val_if_fail (FILETEA_IS_SOURCE (source), NULL);
  g_return_val_if_fail (EVD_IS_WEB_SERVICE (web_service), NULL);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (target_conn), NULL);

  self = g_object_new (FILETEA_TYPE_TRANSFER, NULL);

  self->priv->result = g_simple_async_result_new (G_OBJECT (self),
                                                  callback,
                                                  user_data,
                                                  filetea_transfer_new);

  self->priv->source = g_object_ref (source);
  self->priv->web_service = g_object_ref (web_service);

  if (cancellable != NULL)
    self->priv->cancellable = g_object_ref (cancellable);

  self->priv->target_conn = g_object_ref (target_conn);
  g_signal_connect (target_conn,
                    "close",
                    G_CALLBACK (target_connection_on_close),
                    self);

  self->priv->id = evd_uuid_new ();
  self->priv->action = g_strdup (action);
  self->priv->is_chunked = is_chunked;
  if (is_chunked)
    {
      g_warning ("Chunked!\n");

      self->priv->byte_range.start = MAX (range->start, 0);
      self->priv->byte_range.end = range->end;
    }

  self->priv->status = FILETEA_TRANSFER_STATUS_NOT_STARTED;

  self->priv->timeout_src_id = evd_timeout_add (NULL,
                                                START_TIMEOUT,
                                                G_PRIORITY_LOW,
                                                on_transfer_start_timeout,
                                                self);

  return self;
}

const gchar *
filetea_transfer_get_id (FileteaTransfer *self)
{
  g_return_val_if_fail (FILETEA_IS_TRANSFER (self), NULL);

  return self->priv->id;
}

void
filetea_transfer_set_source_conn (FileteaTransfer   *self,
                                  EvdHttpConnection *conn)
{
  g_return_if_fail (FILETEA_IS_TRANSFER (self));
  g_return_if_fail (EVD_IS_HTTP_CONNECTION (conn));

  self->priv->source_conn = g_object_ref (conn);

  g_signal_connect (self->priv->source_conn,
                    "close",
                    G_CALLBACK (source_connection_on_close),
                    self);
}

void
filetea_transfer_start (FileteaTransfer *self)
{
  SoupMessageHeaders *headers;
  GError *error = NULL;
  gint status = SOUP_STATUS_OK;

  g_return_if_fail (FILETEA_IS_TRANSFER (self));
  g_return_if_fail (self->priv->source_conn != NULL);

  if (self->priv->timeout_src_id != 0)
    {
      g_source_remove (self->priv->timeout_src_id);
      self->priv->timeout_src_id = 0;
    }

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  if (g_strcmp0 (self->priv->action, "open") != 0)
    {
      gchar *st;
      gchar *decoded_file_name;

      decoded_file_name = soup_uri_decode (filetea_source_get_name (self->priv->source));
      st = g_strdup_printf ("attachment; filename=\"%s\"", decoded_file_name);
      g_free (decoded_file_name);
      soup_message_headers_replace (headers, "Content-disposition", st);
      g_free (st);
    }

  /* update transfer len */
  if (self->priv->is_chunked)
    {
      if (self->priv->byte_range.end == -1)
        self->priv->byte_range.end =
          filetea_source_get_size (self->priv->source) - 1;
      else
        self->priv->byte_range.end =
          MIN (self->priv->byte_range.end,
               filetea_source_get_size (self->priv->source) - 1);

      self->priv->transfer_len =
        self->priv->byte_range.end - self->priv->byte_range.start + 1;
    }
  else
    {
      self->priv->transfer_len = filetea_source_get_size (self->priv->source);
    }

  /* prepare target response headers */
  soup_message_headers_set_content_length (headers, self->priv->transfer_len);
  soup_message_headers_set_content_type (headers,
                           filetea_source_get_content_type (self->priv->source),
                           NULL);
  soup_message_headers_replace (headers, "Connection", "keep-alive");

  if (self->priv->is_chunked)
    {
      soup_message_headers_set_content_range (headers,
                                              self->priv->byte_range.start,
                                              self->priv->byte_range.end,
                                              filetea_source_get_size (self->priv->source));
      status = SOUP_STATUS_PARTIAL_CONTENT;
    }

  if (! evd_web_service_respond_headers (self->priv->web_service,
                                         self->priv->target_conn,
                                         status,
                                         headers,
                                         &error))
    {
      g_printerr ("Error sending transfer target headers: %s\n", error->message);
      g_error_free (error);

      /* @TODO: abort the transfer */
    }
  else
    {
      if (self->priv->buf == NULL)
        self->priv->buf = g_slice_alloc (BLOCK_SIZE);

      g_signal_connect (self->priv->target_conn,
                        "write",
                        G_CALLBACK (filetea_transfer_on_target_can_write),
                        self);

      self->priv->status = FILETEA_TRANSFER_STATUS_ACTIVE;

      filetea_transfer_read (self);
    }

  soup_message_headers_free (headers);
}

gboolean
filetea_transfer_finish (FileteaTransfer  *self,
                         GAsyncResult     *result,
                         GError          **error)
{
  g_return_val_if_fail (FILETEA_IS_TRANSFER (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        filetea_transfer_new),
                        FALSE);

  return ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                  error);
}

void
filetea_transfer_set_target_peer (FileteaTransfer *self, EvdPeer *peer)
{
  g_return_if_fail (FILETEA_IS_TRANSFER (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  if (self->priv->target_peer != NULL)
    g_object_unref (self->priv->target_peer);

  self->priv->target_peer = peer;

  if (peer != NULL)
    g_object_ref (peer);
}

void
filetea_transfer_get_status (FileteaTransfer *self,
                             guint           *status,
                             gsize           *transferred,
                             gdouble         *bandwidth)
{
  g_return_if_fail (FILETEA_IS_TRANSFER (self));

  if (status != NULL)
    *status = self->priv->status;

  if (transferred != NULL)
    *transferred = self->priv->transferred;

  if (bandwidth != NULL)
    {
      if (self->priv->source_conn != NULL)
        {
          EvdStreamThrottle *throttle;

          throttle =
            evd_io_stream_get_input_throttle (EVD_IO_STREAM (self->priv->source_conn));

          *bandwidth = evd_stream_throttle_get_actual_bandwidth (throttle);
        }
      else
        {
          *bandwidth = 0;
        }
    }
}

void
filetea_transfer_cancel (FileteaTransfer *self)
{
  g_return_if_fail (FILETEA_IS_TRANSFER (self));

  if (self->priv->result == NULL)
    return;

  g_simple_async_result_set_error (self->priv->result,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CANCELLED,
                                   "Transfer cancelled");

  self->priv->status = FILETEA_TRANSFER_STATUS_SOURCE_ABORTED;

  filetea_transfer_complete (self);
}
