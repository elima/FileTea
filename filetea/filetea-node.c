/*
 * filetea-node.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2011, Igalia S.L.
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

#include "filetea-node.h"
#include "file-source.h"
#include "file-transfer.h"

G_DEFINE_TYPE (FileteaNode, filetea_node, EVD_TYPE_WEB_SERVICE)

#define FILETEA_NODE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                       FILETEA_NODE_TYPE, \
                                       FileteaNodePrivate))

#define DEFAULT_SOURCE_ID_START_DEPTH 8

/* private data */
struct _FileteaNodePrivate
{
  gchar *id;
  guint8 source_id_start_depth;

  EvdPeerManager *peer_manager;
  EvdJsonrpc *rpc;
  EvdWebTransport *transport;
  EvdWebSelector *selector;
  EvdWebDir *webdir;

  GHashTable *sources_by_id;
  GHashTable *sources_by_peer;
  GHashTable *transfers;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint filetea_node_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_ID
};

static void     filetea_node_class_init         (FileteaNodeClass *class);
static void     filetea_node_init               (FileteaNode *self);

static void     filetea_node_finalize           (GObject *obj);
static void     filetea_node_dispose            (GObject *obj);

static void     set_property                    (GObject      *obj,
                                                 guint         prop_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec);
static void     get_property                    (GObject    *obj,
                                                 guint       prop_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec);

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

static void
filetea_node_class_init (FileteaNodeClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *ws_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = filetea_node_dispose;
  obj_class->finalize = filetea_node_finalize;
  obj_class->get_property = get_property;
  obj_class->set_property = set_property;

  ws_class->request_handler = request_handler;

  g_object_class_install_property (obj_class, PROP_ID,
                                   g_param_spec_string ("id",
                                                        "Peer's UUID",
                                                        "A string representing the UUID of the peer",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (FileteaNodePrivate));
}

static void
filetea_node_init (FileteaNode *self)
{
  FileteaNodePrivate *priv;

  priv = FILETEA_NODE_GET_PRIVATE (self);
  self->priv = priv;

  /* web transport */
  priv->transport = evd_web_transport_new (NULL);

  /* JSON-RPC */
  priv->rpc = evd_jsonrpc_new ();
  evd_jsonrpc_set_method_call_callback (priv->rpc,
                                        rpc_on_method_called,
                                        self);
  evd_jsonrpc_use_transport (priv->rpc, EVD_TRANSPORT (priv->transport));

  /* web dir */
  priv->webdir = evd_web_dir_new ();
  evd_web_dir_set_root (priv->webdir, HTML_DATA_DIR);

  /* web selector */
  priv->selector = evd_web_selector_new ();

  evd_web_transport_set_selector (priv->transport, priv->selector);
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
  self->priv->transfers =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL,
                           (GDestroyNotify) file_transfer_unref);
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

  if (self->priv->transfers != NULL)
    {
      g_hash_table_unref (self->priv->transfers);
      self->priv->transfers = NULL;
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

  G_OBJECT_CLASS (filetea_node_parent_class)->finalize (obj);

  g_debug ("*** Filetea Node finalized");
}

static void
set_property (GObject      *obj,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
  //  FileteaNode *self;

  //  self = FILETEA_NODE (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
get_property (GObject    *obj,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
  FileteaNode *self;

  self = FILETEA_NODE (obj);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, filetea_node_get_id (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gchar *
generate_source_id (FileteaNode *self, const gchar *instance_id)
{
  gchar *uuid;
  gchar *id;
  static gint depth;
  gint _depth;
  gint fails;

  depth = self->priv->source_id_start_depth;

  _depth = depth - strlen (instance_id);

  fails = 0;

  uuid = evd_uuid_new ();
  uuid[_depth] = '\0';
  id = g_strconcat (instance_id, uuid, NULL);

  while (g_hash_table_lookup (self->priv->sources_by_id, id) != NULL)
    {
      g_free (uuid);
      g_free (id);

      fails++;
      if (fails > 2)
        {
          depth++;
          _depth = depth - strlen (instance_id);
          fails = 0;
        }

      uuid = evd_uuid_new ();
      uuid[_depth] = '\0';
      id = g_strconcat (instance_id, uuid, NULL);
    }

  g_free (uuid);

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

      source = g_hash_table_lookup (self->priv->sources_by_id, id);
      if (source != NULL)
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

  g_hash_table_remove (self->priv->transfers, transfer->id);
}

static void
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

  g_hash_table_insert (self->priv->transfers, transfer->id, transfer);
}

static gboolean
handle_special_request (FileteaNode         *self,
                        EvdHttpConnection   *conn,
                        EvdHttpRequest      *request,
                        SoupURI             *uri,
                        gchar             **tokens)
{
  //  const gchar *realm;
  const gchar *id;
  //  const gchar *action;
  //  gboolean download;

  //  realm = tokens[1];
  id = tokens[1];
  //  action = tokens[2];

  //  download = g_strcmp0 (action, PATH_ACTION_DOWNLOAD) == 0;

  if (g_strcmp0 (evd_http_request_get_method (request), "PUT") == 0)
    {
      FileTransfer *transfer;

      transfer = g_hash_table_lookup (self->priv->transfers, id);
      if (transfer != NULL)
        {
          file_transfer_set_source_conn (transfer, conn);
          file_transfer_start (transfer);

          return TRUE;
        }
    }
  else
    {
      FileSource *source;

      source = g_hash_table_lookup (self->priv->sources_by_id, id);
      if (source != NULL)
        {
          /*
          if (action == NULL || strlen (action) == 0)
            {
              gchar *new_path;

              g_free (tokens[1]);
              tokens[1] = NULL;

              g_free (tokens[2]);
              tokens[2] = NULL;

              new_path = g_strjoinv ("/", tokens);
              soup_uri_set_path (uri, new_path);
              g_free (new_path);
            }
          else
          */
            {
              setup_new_transfer (self, source, conn, /*download*/ TRUE);
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

  tokens = g_strsplit (uri->path, "/", 16);
  tokens_len = g_strv_length (tokens);

  if (tokens_len == 2 &&
      handle_special_request (self, conn, request, uri, tokens))
    {
      /* @TODO: possibly, a request from a transfer endpoint */
      g_debug ("transfer request");
    }
  else if (tokens_len == 2 &&
           g_strcmp0 (tokens[1], "default") != 0 &&
           g_strcmp0 (tokens[1], "common") != 0)
    {
      gchar *new_path;
      GError *error = NULL;

      /* @TODO: detect type of user-agent (mobile vs. desktop), and locale,
         then redirect to correponding html root. By now, only default. */

      new_path = g_strdup_printf ("/default%s", uri->path);

      /* redirect */
      if (! evd_http_connection_redirect (conn, new_path, FALSE, &error))
        {
          g_debug ("ERROR sending response to source: %s", error->message);
          g_error_free (error);
        }

      g_free (new_path);
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
          /* @TODO: respond error, file not found */
        }
    }
  else
    {
      /* @TODO: error, method not handled */
      g_debug ("ERROR: unhandled method: %s", method_name);
    }

  if (error == NULL)
    {
      if (! evd_jsonrpc_respond (jsonrpc,
                                 invocation_id,
                                 result,
                                 peer,
                                 &error))
        {
          g_debug ("error responding JSON-RPC: %s", error->message);
          g_error_free (error);
        }

      if (result != NULL)
        json_node_free (result);
      if (result_arr != NULL)
        json_array_unref (result_arr);
    }
  else
    {
      /* @TODO: respond with error */
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
      MAX (self->priv->source_id_start_depth, strlen (self->priv->id) + 1);

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
