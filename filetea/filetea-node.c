/*
 * filetea-node.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#include <uuid/uuid.h>

#include "filetea-node.h"
#include "file-source.h"
#include "file-transfer.h"

#include <string.h>

G_DEFINE_TYPE (FileteaNode, filetea_node, EVD_TYPE_WEB_SERVICE)

#define FILETEA_NODE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                       FILETEA_NODE_TYPE, \
                                       FileteaNodePrivate))

#define DEFAULT_SOURCE_ID_START_DEPTH 8

#define PATH_ACTION_VIEW "view"

#define FILETEA_ERROR_DOMAIN_STR "me.filetea.ErrorDomain"
#define FILETEA_ERROR            g_quark_from_string (FILETEA_ERROR_DOMAIN_STR)

typedef enum {
  FILETEA_ERROR_SUCCESS,
  FILETEA_ERROR_FILE_NOT_FOUND,
  FILETEA_ERROR_RPC_UNKNOWN_METHOD
} FileteaErrorEnum;

/* private data */
struct _FileteaNodePrivate
{
  gchar *id;
  guint8 source_id_start_depth;

  EvdPeerManager *peer_manager;
  EvdJsonrpc *rpc;
  EvdWebTransportServer *transport;
  EvdWebSelector *selector;
  EvdWebDir *webdir;

  GHashTable *sources_by_id;
  GHashTable *sources_by_peer;
  GHashTable *transfers_by_id;
  GHashTable *transfers_by_peer;

  guint report_transfers_src_id;

  gboolean force_https;
  guint https_port;

  gchar *log_filename;
  GFileOutputStream *log_output_stream;
  GQueue *log_queue;
};

static void     filetea_node_class_init         (FileteaNodeClass *class);
static void     filetea_node_init               (FileteaNode *self);

static void     filetea_node_finalize           (GObject *obj);
static void     filetea_node_dispose            (GObject *obj);

static void     on_new_peer                     (EvdPeerManager *self,
                                                 EvdPeer        *peer,
                                                 gpointer        user_data);
static void     on_peer_closed                  (EvdPeerManager *self,
                                                 EvdPeer        *peer,
                                                 gboolean        gracefully,
                                                 gpointer        user_data);

static void     request_handler                 (EvdWebService     *self,
                                                 EvdHttpConnection *conn,
                                                 EvdHttpRequest    *request);

static void     rpc_on_method_called            (EvdJsonrpc  *jsonrpc,
                                                 const gchar *method_name,
                                                 JsonNode    *params,
                                                 guint        invocation_id,
                                                 gpointer     context,
                                                 gpointer     user_data);

static void     web_dir_on_log_entry            (EvdWebService *service,
                                                 const gchar   *entry,
                                                 gpointer       user_data);

static void
filetea_node_class_init (FileteaNodeClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *ws_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = filetea_node_dispose;
  obj_class->finalize = filetea_node_finalize;

  ws_class->request_handler = request_handler;

  g_type_class_add_private (obj_class, sizeof (FileteaNodePrivate));
}

static void
filetea_node_init (FileteaNode *self)
{
  FileteaNodePrivate *priv;

  priv = FILETEA_NODE_GET_PRIVATE (self);
  self->priv = priv;

  /* web transport */
  priv->transport = evd_web_transport_server_new ("/transport/");

  /* JSON-RPC */
  priv->rpc = evd_jsonrpc_new ();
  evd_jsonrpc_set_callbacks (priv->rpc,
                             rpc_on_method_called,
                             NULL,
                             self,
                             NULL);
  evd_ipc_mechanism_use_transport (EVD_IPC_MECHANISM (priv->rpc),
                                   EVD_TRANSPORT (priv->transport));

  /* web dir */
  priv->webdir = evd_web_dir_new ();
  evd_web_dir_set_root (priv->webdir, HTML_DATA_DIR);

  /* web selector */
  priv->selector = evd_web_selector_new ();

  evd_web_transport_server_use_selector (priv->transport, priv->selector);
  evd_web_selector_set_default_service (priv->selector,
                                        EVD_SERVICE (priv->webdir));

  /* track peers */
  priv->peer_manager = evd_peer_manager_get_default ();
  g_signal_connect (priv->peer_manager,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    self);
  g_signal_connect (priv->peer_manager,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    self);

  /* hash tables for indexing file sources */
  self->priv->sources_by_id =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL,
                           (GDestroyNotify) file_source_unref);
  self->priv->sources_by_peer =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) g_hash_table_unref);

  /* hash tables for indexing file transfers */
  self->priv->transfers_by_id =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL,
                           (GDestroyNotify) file_transfer_unref);

  self->priv->transfers_by_peer =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) g_queue_free);

  priv->report_transfers_src_id = 0;

  priv->force_https = FALSE;
  priv->https_port = 0;

  priv->log_filename = NULL;
  priv->log_output_stream = NULL;
  priv->log_queue = NULL;
}

static void
filetea_node_dispose (GObject *obj)
{
  FileteaNode *self = FILETEA_NODE (obj);

  if (self->priv->peer_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->peer_manager,
                                            on_new_peer,
                                            self);
      g_signal_handlers_disconnect_by_func (self->priv->peer_manager,
                                            on_peer_closed,
                                            self);
      g_object_unref (self->priv->peer_manager);
      self->priv->peer_manager = NULL;
    }

  if (self->priv->sources_by_id != NULL)
    {
      g_hash_table_unref (self->priv->sources_by_id);
      self->priv->sources_by_id = NULL;
    }

  if (self->priv->sources_by_peer != NULL)
    {
      g_hash_table_unref (self->priv->sources_by_peer);
      self->priv->sources_by_peer = NULL;
    }

  if (self->priv->transfers_by_id != NULL)
    {
      g_hash_table_unref (self->priv->transfers_by_id);
      self->priv->transfers_by_id = NULL;
    }

  if (self->priv->transfers_by_peer != NULL)
    {
      g_hash_table_unref (self->priv->transfers_by_peer);
      self->priv->transfers_by_peer = NULL;
    }

  G_OBJECT_CLASS (filetea_node_parent_class)->dispose (obj);
}

static void
filetea_node_finalize (GObject *obj)
{
  FileteaNode *self = FILETEA_NODE (obj);

  g_free (self->priv->id);

  g_object_unref (self->priv->rpc);
  g_object_unref (self->priv->transport);
  g_object_unref (self->priv->webdir);
  g_object_unref (self->priv->selector);

  if (self->priv->log_queue != NULL)
    {
      while (g_queue_get_length (self->priv->log_queue) > 0)
        {
          gchar *entry;
          entry = g_queue_pop_head (self->priv->log_queue);
          g_free (entry);
        }
      g_queue_free (self->priv->log_queue);
    }

  if (self->priv->log_output_stream != NULL)
    g_object_unref (self->priv->log_output_stream);

  if (self->priv->log_filename != NULL)
    g_free (self->priv->log_filename);

  G_OBJECT_CLASS (filetea_node_parent_class)->finalize (obj);
}

static gchar *
generate_random_source_id (const gchar *prefix, gsize len)
{
  gchar *id;
  uuid_t uuid;
  gchar *id_rnd;
  gint i;

  uuid_generate (uuid);
  id_rnd = g_base64_encode (uuid, len);
  id = g_strconcat (prefix, id_rnd, NULL);
  g_free (id_rnd);

  for (i=0; id[i] != '\0'; i++)
    if (id[i] == '/' || id[i] == '+')
      id[i] = 'x';
    else if (id[i] == '=')
      id[i] = '\0';

  return id;
}

static gchar *
generate_source_id (FileteaNode *self, const gchar *instance_id)
{
  gchar *id;
  static gint depth;
  gint _depth;
  gint fails;

  depth = self->priv->source_id_start_depth;

  _depth = depth - strlen (instance_id);

  fails = 0;

  id = generate_random_source_id (instance_id, _depth);

  while (g_hash_table_lookup (self->priv->sources_by_id, id) != NULL)
    {
      g_free (id);

      fails++;
      if (fails > 2)
        {
          depth++;
          _depth = depth - strlen (instance_id);
          fails = 0;
        }

      id = generate_random_source_id (instance_id, _depth);
    }

  return id;
}

static FileSource *
add_file_source (FileteaNode *self, JsonNode *item, EvdPeer *peer)
{
  JsonArray *a;
  JsonNode *node;
  gchar *id;
  const gchar *name;
  const gchar *type;
  gssize size;
  FileSource *source;
  GHashTable *sources_of_peer;

  a = json_node_get_array (item);

  node = json_array_get_element (a, 0);
  name = json_node_get_string (node);

  node = json_array_get_element (a, 1);
  type = json_node_get_string (node);

  node = json_array_get_element (a, 2);
  size = (gssize) json_node_get_int (node);

  if (name == NULL || type == NULL || size <= 0)
    return NULL;

  id = generate_source_id (self, self->priv->id);
  source = file_source_new (peer, id, name, type, size, G_OBJECT (self));
  g_free (id);

  g_hash_table_insert (self->priv->sources_by_id, source->id, source);

  sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
  if (sources_of_peer == NULL)
    {
      sources_of_peer =
        g_hash_table_new_full (g_str_hash,
                               g_str_equal,
                               NULL,
                               (GDestroyNotify) file_source_unref);
      g_hash_table_insert (self->priv->sources_by_peer, peer, sources_of_peer);
    }
  g_hash_table_insert (sources_of_peer, source->id, file_source_ref (source));

  g_debug ("Added new source '%s'", name);

  return source;
}

static void
remove_file_source (FileSource  *source, gboolean abort_transfers)
{
  FileteaNode *self = FILETEA_NODE (source->node);

  g_hash_table_remove (self->priv->sources_by_id, source->id);

  if (abort_transfers)
    {
      /* @TODO */
    }
}

static gboolean
remove_file_source_foreach (gpointer key,
                            gpointer value,
                            gpointer user_data)
{
  FileSource *source = value;
  gboolean abort_transfers = * (gboolean *) user_data;

  remove_file_source (source, abort_transfers);

  return TRUE;
}

static void
remove_file_sources (FileteaNode *self, JsonNode *ids, gboolean abort_transfers)
{
  JsonArray *a;
  gint i;
  JsonNode *item;

  a = json_node_get_array (ids);

  for (i=0; i < json_array_get_length (a); i++)
    {
      const gchar *id;
      FileSource *source;

      item = json_array_get_element (a, i);
      id = json_node_get_string (item);

      if (id != NULL &&
          (source = g_hash_table_lookup (self->priv->sources_by_id, id)) != NULL)
        {
          GHashTable *sources_of_peer;

          g_debug ("Removed file source '%s'", source->file_name);

          remove_file_source (source, abort_transfers);

          sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer,
                                                 source->peer);
          if (sources_of_peer != NULL)
            g_hash_table_remove (sources_of_peer, source->id);
        }
    }
}

static void
on_new_peer (EvdPeerManager *peer_manager,
             EvdPeer        *peer,
             gpointer        user_data)
{
  g_debug ("New peer %s", evd_peer_get_id (peer));
}

static void
on_peer_closed (EvdPeerManager *peer_manager,
                EvdPeer        *peer,
                gboolean        gracefully,
                gpointer        user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  GHashTable *sources_of_peer;

  sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
  if (sources_of_peer != NULL)
    {
      gboolean abort_transfers = TRUE;

      g_hash_table_foreach_remove (sources_of_peer,
                                   remove_file_source_foreach,
                                   &abort_transfers);

      g_hash_table_remove (self->priv->sources_by_peer, peer);
    }

  g_debug ("Closed peer %s", evd_peer_get_id (peer));
}

static void
on_new_transfer_response (GObject      *obj,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  JsonNode *error_json;
  JsonNode *result_json;
  GError *error = NULL;

  if (! evd_jsonrpc_call_method_finish (EVD_JSONRPC (obj),
                                        res,
                                        &result_json,
                                        &error_json,
                                        &error))
    {
      /* @TODO: source error, cancel transfer */
      g_debug ("Error calling 'fileTransferNew' over JSON-RPC: %s", error->message);
      g_error_free (error);
      return;
    }

  if (error_json != NULL)
    {
      g_debug ("Transfer not accepted by peer");
    }
  else
    {
      json_node_free (result_json);
    }
}

static void
bind_transfer_to_peer (FileteaNode *self, EvdPeer *peer, FileTransfer *transfer)
{
  GQueue *peer_transfers;

  peer_transfers = g_hash_table_lookup (self->priv->transfers_by_peer, peer);
  if (peer_transfers == NULL)
    {
      peer_transfers = g_queue_new ();
      g_hash_table_insert (self->priv->transfers_by_peer, peer, peer_transfers);
    }

  g_queue_push_head (peer_transfers, transfer);
}

static void
unbind_transfer_from_peer (FileteaNode *self, EvdPeer *peer, FileTransfer *transfer)
{
  GQueue *peer_transfers;

  peer_transfers = g_hash_table_lookup (self->priv->transfers_by_peer, peer);
  if (peer_transfers == NULL)
    return;

  g_queue_remove (peer_transfers, transfer);

  if (g_queue_get_length (peer_transfers) == 0)
    g_hash_table_remove (self->priv->transfers_by_peer, peer);
}

static void
report_transfer_finished (FileteaNode *self, FileTransfer *transfer)
{
  JsonNode *node;
  JsonArray *args;
  guint status;

  file_transfer_get_status (transfer, &status, NULL, NULL);

  node = json_node_new (JSON_NODE_ARRAY);
  args = json_array_new ();
  json_node_set_array (node, args);

  json_array_add_string_element (args, transfer->id);
  json_array_add_int_element (args, status);

  if (! evd_jsonrpc_send_notification (self->priv->rpc,
                                       "transfer-finished",
                                       node,
                                       transfer->source->peer,
                                       NULL))
    {
      g_warning ("Failed to send 'transfer-finished' notification to peer");
    }

  if (transfer->target_peer != NULL)
    {
      if (! evd_jsonrpc_send_notification (self->priv->rpc,
                                           "transfer-finished",
                                           node,
                                           transfer->target_peer,
                                           NULL))
        {
          g_warning ("Failed to send 'transfer-finished' notification to peer");
        }
    }

  json_array_unref (args);
  json_node_free (node);
}

static void
on_transfer_finished (GObject      *obj,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  FileTransfer *transfer = user_data;
  GError *error = NULL;
  FileteaNode *self;

  self = FILETEA_NODE (transfer->source->node);

  if (! file_transfer_finish (transfer, res, &error))
    {
      g_debug ("transfer failed: %s", error->message);
      g_error_free (error);
    }
  else
    {
      g_debug ("Transfer completed successfully: %s",
               transfer->source->file_name);
    }

  report_transfer_finished (self, transfer);

  unbind_transfer_from_peer (self, transfer->source->peer, transfer);
  if (transfer->target_peer != NULL)
    unbind_transfer_from_peer (self, transfer->target_peer, transfer);

  if (self->priv->report_transfers_src_id != 0 &&
      g_hash_table_size (self->priv->transfers_by_peer) == 0)
    {
      g_source_remove (self->priv->report_transfers_src_id);
      self->priv->report_transfers_src_id = 0;
    }

  g_hash_table_remove (self->priv->transfers_by_id, transfer->id);
}

static void
report_transfer_status (gpointer data, gpointer user_data)
{
  FileTransfer *transfer = data;
  JsonArray *args = user_data;
  guint status;
  gsize transferred;
  gdouble bandwidth;

  file_transfer_get_status (transfer, &status, &transferred, &bandwidth);

  if (status == FILE_TRANSFER_STATUS_ACTIVE)
    {
      JsonObject *obj;

      obj = json_object_new ();

      json_object_set_string_member (obj, "id", transfer->id);
      json_object_set_int_member (obj, "status", status);
      json_object_set_int_member (obj, "transferred", transferred);
      json_object_set_int_member (obj, "bandwidth", bandwidth);

      json_array_add_object_element (args, obj);
    }
}

static gboolean
report_transfers_status (gpointer user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  GHashTableIter iter;
  gpointer key, value;

  self->priv->report_transfers_src_id = 0;

  g_hash_table_iter_init (&iter, self->priv->transfers_by_peer);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      EvdPeer *peer = EVD_PEER (key);
      GQueue *transfers = value;
      JsonNode *node;
      JsonArray *args;

      g_assert (g_queue_get_length (transfers) > 0);

      node = json_node_new (JSON_NODE_ARRAY);
      args = json_array_new ();
      json_node_set_array (node, args);

      g_queue_foreach (transfers, report_transfer_status, args);

      if (json_array_get_length (args) > 0)
        {
          if (! evd_jsonrpc_send_notification (self->priv->rpc,
                                               "transfer-status",
                                               node,
                                               peer,
                                               NULL))
            {
              g_warning ("Failed to send 'transfer-status' notification to peer");
            }
        }

      json_array_unref (args);
      json_node_free (node);
    }

  return g_hash_table_size (self->priv->transfers_by_peer) > 0;
}

static FileTransfer *
setup_new_transfer (FileteaNode       *self,
                    FileSource        *source,
                    EvdHttpConnection *conn,
                    gboolean           download)
{
  FileTransfer *transfer;
  JsonNode *params;
  JsonArray *arr;
  gchar *uuid;
  gchar *transfer_id;

  uuid = evd_uuid_new ();
  transfer_id = g_strconcat (self->priv->id, uuid, NULL);
  transfer = file_transfer_new (transfer_id,
                                source,
                                conn,
                                download,
                                on_transfer_finished,
                                NULL);
  g_free (transfer_id);
  g_free (uuid);

  params = json_node_new (JSON_NODE_ARRAY);
  arr = json_array_new ();
  json_node_set_array (params, arr);

  json_array_add_string_element (arr, source->id);
  json_array_add_string_element (arr, transfer->id);

  evd_jsonrpc_call_method (self->priv->rpc,
                           "fileTransferNew",
                           params,
                           source->peer,
                           NULL,
                           on_new_transfer_response,
                           transfer);
  json_array_unref (arr);
  json_node_free (params);

  g_hash_table_insert (self->priv->transfers_by_id, transfer->id, transfer);

  bind_transfer_to_peer (self, transfer->source->peer, transfer);

  if (self->priv->report_transfers_src_id == 0)
    self->priv->report_transfers_src_id = evd_timeout_add (NULL,
                                                           1000,
                                                           G_PRIORITY_DEFAULT,
                                                           report_transfers_status,
                                                           self);

  return transfer;
}

static gboolean
user_agent_is_browser (const gchar *user_agent)
{
  const gchar *mozilla = "Mozilla";
  const gchar *opera = "Opera";

  /* @TODO: improves this detection in the future;
     it is currently very naive. */

  return
    (g_strstr_len (user_agent, strlen (mozilla), mozilla) == user_agent) ||
    (g_strstr_len (user_agent, strlen (opera), opera) == user_agent);
}

static void
check_file_size_changed (FileteaNode    *self,
                         FileTransfer   *transfer,
                         EvdHttpRequest *request)
{
  SoupMessageHeaders *headers;

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
  if (soup_message_headers_get_encoding (headers) ==
      SOUP_ENCODING_CONTENT_LENGTH)
    {
      gsize reported_content_len;

      reported_content_len = soup_message_headers_get_content_length (headers);
      if (reported_content_len != transfer->source->file_size)
        {
          JsonNode *node;
          JsonArray *args;

          transfer->source->file_size = reported_content_len;

          node = json_node_new (JSON_NODE_ARRAY);
          args = json_array_new ();
          json_node_set_array (node, args);

          json_array_add_string_element (args, transfer->source->id);
          json_array_add_int_element (args, transfer->source->file_size);

          if (! evd_jsonrpc_send_notification (self->priv->rpc,
                                               "update-file-size",
                                               node,
                                               transfer->source->peer,
                                               NULL))
            {
              g_warning ("Failed to send 'transfer-started' notification to peer");
            }

          json_array_unref (args);
          json_node_free (node);
        }
    }
}

static gboolean
handle_special_request (FileteaNode         *self,
                        EvdHttpConnection   *conn,
                        EvdHttpRequest      *request,
                        SoupURI             *uri,
                        gchar             **tokens)
{
  const gchar *id;
  const gchar *action;

  id = tokens[1];
  action = tokens[2];

  if (g_strcmp0 (evd_http_request_get_method (request), "PUT") == 0)
    {
      FileTransfer *transfer;

      transfer = g_hash_table_lookup (self->priv->transfers_by_id, id);
      if (transfer != NULL)
        {
          JsonNode *node;
          JsonArray *args;

          /* check if file size has changed */
          check_file_size_changed (self, transfer, request);

          file_transfer_set_source_conn (transfer, conn);
          file_transfer_start (transfer);

          if (transfer->target_peer &&
              ! evd_peer_is_closed (transfer->target_peer))
            {
              bind_transfer_to_peer (self, transfer->target_peer, transfer);

              node = json_node_new (JSON_NODE_ARRAY);
              args = json_array_new ();
              json_node_set_array (node, args);

              json_array_add_string_element (args, transfer->id);
              json_array_add_string_element (args, transfer->source->file_name);
              json_array_add_int_element (args, transfer->source->file_size);
              json_array_add_boolean_element (args, TRUE);

              /* notify target */
              if (! evd_jsonrpc_send_notification (self->priv->rpc,
                                                   "transfer-started",
                                                   node,
                                                   transfer->target_peer,
                                                   NULL))
                {
                  g_warning ("Failed to send 'transfer-started' notification to peer");
                }

              json_array_unref (args);
              json_node_free (node);
            }

          return TRUE;
        }
    }
  else
    {
      FileSource *source;

      source = g_hash_table_lookup (self->priv->sources_by_id, id);
      if (source != NULL)
        {
          SoupMessageHeaders *headers;
          const gchar *user_agent;

          headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
          user_agent = soup_message_headers_get_one (headers, "user-agent");

          if ((action == NULL || (strlen (action) == 0)) &&
              user_agent_is_browser (user_agent))
            {
              gchar *new_path;
              GError *error = NULL;

              new_path = g_strdup_printf ("/#%s", id);

              if (! evd_http_connection_redirect (conn, new_path, FALSE, &error))
                {
                  /* @TODO: do proper error logging */
                  g_debug ("ERROR sending response to source: %s", error->message);
                  g_error_free (error);
                }

              g_free (new_path);

              return TRUE;
            }
          else
            {
              gboolean download;
              FileTransfer *transfer;

              download = g_strcmp0 (action, PATH_ACTION_VIEW) != 0;

              transfer = setup_new_transfer (self, source, conn, download);

              if (uri->query != NULL)
                {
                  EvdPeer *peer;

                  peer = evd_transport_lookup_peer (EVD_TRANSPORT (self->priv->transport),
                                                    uri->query);
                  if (peer != NULL)
                    file_transfer_set_target_peer (transfer, peer);
                }

              return TRUE;
            }
        }
    }

  return FALSE;
}

static void
request_handler (EvdWebService     *web_service,
                 EvdHttpConnection *conn,
                 EvdHttpRequest    *request)
{
  FileteaNode *self = FILETEA_NODE (web_service);

  gchar **tokens;
  SoupURI *uri;
  guint tokens_len;

  uri = evd_http_request_get_uri (request);

  /* redirect to HTTPS service if forced in config */
  if (self->priv->force_https &&
      ! evd_connection_get_tls_active (EVD_CONNECTION (conn)))
    {
      gchar *new_uri;

      soup_uri_set_scheme (uri, "https");
      soup_uri_set_port (uri, self->priv->https_port);

      new_uri = soup_uri_to_string (uri, FALSE);

      evd_http_connection_redirect (conn, new_uri, FALSE, NULL);

      g_free (new_uri);

      return;
    }

  tokens = g_strsplit (uri->path, "/", 16);
  tokens_len = g_strv_length (tokens);

  if (tokens_len >= 2 &&
      handle_special_request (self, conn, request, uri, tokens))
    {
      /* @TODO: possibly, a request from a transfer endpoint */
      g_debug ("transfer request");
    }
  else
    {
      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (self->priv->selector),
                                                   conn,
                                                   request,
                                                   EVD_SERVICE (self));
    }

  g_strfreev (tokens);
}

static void
rpc_on_method_called (EvdJsonrpc  *jsonrpc,
                      const gchar *method_name,
                      JsonNode    *params,
                      guint        invocation_id,
                      gpointer     context,
                      gpointer     user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  EvdPeer *peer = EVD_PEER (context);
  GError *error = NULL;
  JsonArray *a;
  gint i;
  JsonNode *item;
  JsonNode *result = NULL;
  JsonArray *result_arr = NULL;

  a = json_node_get_array (params);

  if (g_strcmp0 (method_name, "addFileSources") == 0)
    {
      FileSource *source;

      result = json_node_new (JSON_NODE_ARRAY);
      result_arr = json_array_new ();
      json_node_set_array (result, result_arr);

      for (i = 0; i < json_array_get_length (a); i++)
        {
          item = json_array_get_element (a, i);

          source = add_file_source (self, item, peer);
          if (source != NULL)
            {
              JsonArray *sources_arr;

              sources_arr = json_array_new ();
              json_array_add_string_element (sources_arr, source->id);

              json_array_add_array_element (result_arr, sources_arr);
            }
          else
            json_array_add_null_element (result_arr);
        }
    }
  else if (g_strcmp0 (method_name, "removeFileSources") == 0)
    {
      gboolean abort_transfers;

      item = json_array_get_element (a, 0);
      abort_transfers = json_node_get_boolean (item);

      item = json_array_get_element (a, 1);

      remove_file_sources (self, item, abort_transfers);

      result = json_node_new (JSON_NODE_VALUE);
      json_node_set_boolean (result, TRUE);
    }
  else if (g_strcmp0 (method_name, "getFileSourceInfo") == 0)
    {
      const gchar *id;
      FileSource *source;

      id = json_array_get_string_element (a, 0);
      if (id != NULL &&
          (source = g_hash_table_lookup (self->priv->sources_by_id, id)) != NULL)
        {
          result = json_node_new (JSON_NODE_ARRAY);
          result_arr = json_array_new ();
          json_node_set_array (result, result_arr);

          json_array_add_string_element (result_arr, source->file_name);
          json_array_add_string_element (result_arr, source->file_type);
          json_array_add_int_element (result_arr, source->file_size);
        }
      else
        {
          g_set_error (&error,
                       FILETEA_ERROR,
                       FILETEA_ERROR_FILE_NOT_FOUND,
                       "No source file with id '%s'", id);
        }
    }
  else if (g_strcmp0 (method_name, "cancelTransfer") == 0)
    {
      const gchar *id;
      FileTransfer *transfer;
      gint i;

      for (i=0; i<json_array_get_length (a); i++)
        {
          id = json_array_get_string_element (a, i);
          if (id != NULL &&
              (transfer = g_hash_table_lookup (self->priv->transfers_by_id,
                                               id)) != NULL)
            {
              file_transfer_cancel (transfer);
            }
        }

      result = json_node_new (JSON_NODE_VALUE);
      json_node_set_boolean (result, TRUE);
    }
  else
    {
      /* error, method not known/handled */
      g_set_error (&error,
                   FILETEA_ERROR,
                   FILETEA_ERROR_RPC_UNKNOWN_METHOD,
                   "Unknown or unhandled JSON-RPC method '%s'", method_name);
    }

  if (error == NULL)
    {
      /* respond method call */
      evd_jsonrpc_respond (jsonrpc,
                           invocation_id,
                           result,
                           peer,
                           &error);

      if (result != NULL)
        json_node_free (result);
      if (result_arr != NULL)
        json_array_unref (result_arr);
    }
  else
    {
      JsonObject *obj;

      /* build error object and respond with error */
      result = json_node_new (JSON_NODE_OBJECT);
      obj = json_object_new ();
      json_node_set_object (result, obj);

      json_object_set_int_member (obj, "code", error->code);
      json_object_set_string_member (obj, "message", error->message);

      g_clear_error (&error);

      evd_jsonrpc_respond_error (jsonrpc,
                                 invocation_id,
                                 result,
                                 peer,
                                 &error);

      json_object_unref (obj);
      json_node_free (result);
    }


  if (error != NULL)
    {
      /* failed to respond method call */

      /* @TODO: do proper logging */
      g_debug ("ERROR responding JSON-RPC method call: %s", error->message);
      g_error_free (error);
    }
}

static void
set_max_node_bandwidth (FileteaNode *self,
                        gdouble      max_bw_in,
                        gdouble      max_bw_out)
{
  EvdStreamThrottle *throttle;

  g_object_get (self,
                "input-throttle", &throttle,
                NULL);
  g_object_set (throttle,
                "bandwidth", max_bw_in,
                NULL);
  g_object_unref (throttle);

  g_object_get (self,
                "output-throttle", &throttle,
                NULL);
  g_object_set (throttle,
                "bandwidth", max_bw_out,
                NULL);
  g_object_unref (throttle);
}

static void
web_dir_on_log_entry_written (GObject      *obj,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  gchar *entry = user_data;
  GError *error = NULL;

  g_output_stream_write_finish (G_OUTPUT_STREAM (obj), res, &error);
  if (error != NULL)
    {
      g_warning ("Failed to write to log file: %s", error->message);
      g_error_free (error);
    }

  entry = g_queue_pop_head (self->priv->log_queue);
  g_free (entry);

  if (g_queue_get_length (self->priv->log_queue) > 0)
    {
      entry = g_queue_peek_head (self->priv->log_queue);
      g_output_stream_write_async (G_OUTPUT_STREAM (self->priv->log_output_stream),
                                   entry,
                                   strlen (entry),
                                   G_PRIORITY_DEFAULT,
                                   NULL,
                                   web_dir_on_log_entry_written,
                                   self);
    }
}

static void
web_dir_on_log_entry (EvdWebService *service,
                      const gchar   *entry,
                      gpointer       user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  gchar *copy;

  copy = g_strdup_printf ("%s\n", entry);

  g_queue_push_tail (self->priv->log_queue, copy);

  if (! g_output_stream_has_pending (G_OUTPUT_STREAM (self->priv->log_output_stream)))
    g_output_stream_write_async (G_OUTPUT_STREAM (self->priv->log_output_stream),
                                 copy,
                                 strlen (copy),
                                 G_PRIORITY_DEFAULT,
                                 NULL,
                                 web_dir_on_log_entry_written,
                                 self);
}

static void
setup_web_dir_logging (FileteaNode *self)
{
  GError *error = NULL;
  GFile *log_file;

  log_file = g_file_new_for_path (self->priv->log_filename);

  self->priv->log_output_stream = g_file_append_to (log_file,
                                                    G_FILE_CREATE_NONE,
                                                    NULL,
                                                    &error);

  if (self->priv->log_output_stream == NULL)
    {
      g_warning ("Failed opening log file: %s. (HTTP logs disabled)",
                 error->message);
      g_error_free (error);

      return;
    }

  self->priv->log_queue = g_queue_new ();

  g_signal_connect (self->priv->webdir,
                    "log-entry",
                    G_CALLBACK (web_dir_on_log_entry),
                    self);

  g_object_unref (log_file);
}

static void
load_config (FileteaNode *self, GKeyFile *config)
{
  /* source id start depth */
  self->priv->source_id_start_depth =
    g_key_file_get_integer (config,
                            "node",
                            "source-id-start-depth",
                            NULL);
  if (self->priv->source_id_start_depth == 0)
    self->priv->source_id_start_depth = DEFAULT_SOURCE_ID_START_DEPTH;
  else
    self->priv->source_id_start_depth =
      MIN (self->priv->source_id_start_depth, 16 + strlen (self->priv->id));

  /* global bandwidth limites */
  set_max_node_bandwidth (self,
                          g_key_file_get_double (config,
                                                 "node",
                                                 "max-bandwidth-in",
                                                 NULL),
                          g_key_file_get_double (config,
                                                 "node",
                                                 "max-bandwidth-out",
                                                 NULL));

  /* force https? */
  self->priv->force_https = g_key_file_get_boolean (config,
                                                    "http",
                                                    "force-https",
                                                    NULL) == TRUE;
  if (self->priv->force_https)
    {
      self->priv->https_port = g_key_file_get_integer (config,
                                                       "https",
                                                       "port",
                                                       NULL);
    }

  /* log file */
  self->priv->log_filename = g_key_file_get_string (config,
                                                    "log",
                                                    "http-log-file",
                                                    NULL);
  if (self->priv->log_filename != NULL && self->priv->log_filename[0] != '\0')
    setup_web_dir_logging (self);

  /* external base URL */
  const char *external_base_url = g_key_file_get_string (config,
                                                         "http",
                                                         "external-base-url",
                                                         NULL);
  if (external_base_url != NULL && external_base_url[0] != '\0')
    {
      evd_web_transport_server_set_external_base_url (self->priv->transport,
                                                      external_base_url);
    }
}

/* public methods */

FileteaNode *
filetea_node_new (GKeyFile *config, GError **error)
{
  FileteaNode *self;
  gchar *id;

  g_return_val_if_fail (config != NULL, NULL);

  id = g_key_file_get_string (config, "node", "id", error);
  if (id == NULL)
    return NULL;

  self = g_object_new (FILETEA_NODE_TYPE, NULL);

  self->priv->id = id;

  load_config (self, config);

  return self;
}

const gchar *
filetea_node_get_id (FileteaNode *self)
{
  g_return_val_if_fail (IS_FILETEA_NODE (self), NULL);

  return self->priv->id;
}
