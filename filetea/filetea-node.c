/*
 * filetea-node.c
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

#include <uuid/uuid.h>

#include "filetea-node.h"

#include "filetea-source.h"
#include "filetea-transfer.h"

G_DEFINE_TYPE (FileteaNode, filetea_node, G_TYPE_OBJECT)

#define FILETEA_NODE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                       FILETEA_TYPE_NODE, \
                                       FileteaNodePrivate))

#define DEFAULT_SOURCE_ID_START_DEPTH 8

/*
#define FILETEA_ERROR_DOMAIN_STR "me.filetea.ErrorDomain"
#define FILETEA_ERROR            g_quark_from_string (FILETEA_ERROR_DOMAIN_STR)

typedef enum {
  FILETEA_ERROR_NONE,
  FILETEA_ERROR_SOURCE_NOT_FOUND,
} FileteaErrorEnum;
*/

/* private data */
struct _FileteaNodePrivate
{
  gchar *id;
  gchar *key;
  guint8 source_id_start_depth;

  FileteaProtocolVTable protocol_vtable;
  FileteaProtocol *protocol;

  FileteaWebService *web_service;

  GHashTable *sources_by_id;
  GHashTable *sources_by_peer;
  GHashTable *transfers_by_id;
  GHashTable *transfers_by_peer;

  guint report_transfers_src_id;
};

static void     filetea_node_class_init         (FileteaNodeClass *class);
static void     filetea_node_init               (FileteaNode *self);

static void     filetea_node_finalize           (GObject *obj);
static void     filetea_node_dispose            (GObject *obj);

static gboolean register_source                 (FileteaProtocol  *protocol,
                                                 EvdPeer          *peer,
                                                 FileteaSource    *source,
                                                 GError          **error,
                                                 gpointer          user_data);
static gboolean unregister_source               (FileteaProtocol *protocol,
                                                 EvdPeer         *peer,
                                                 const gchar     *id,
                                                 gboolean         gracefully,
                                                 gpointer         user_data);
static void     content_request                 (FileteaProtocol    *protocol,
                                                 FileteaSource      *source,
                                                 EvdHttpConnection  *conn,
                                                 const gchar        *action,
                                                 const gchar        *peer_id,
                                                 gboolean            is_chunked,
                                                 SoupRange          *byte_range,
                                                 gpointer            user_data);
static void     content_push                    (FileteaProtocol    *protocol,
                                                 FileteaTransfer    *transfer,
                                                 EvdHttpConnection  *conn,
                                                 gpointer            user_data);

static void     on_new_peer                     (EvdTransport *transport,
                                                 EvdPeer      *peer,
                                                 gpointer      user_data);
static void     on_peer_closed                  (EvdTransport *transport,
                                                 EvdPeer      *peer,
                                                 gboolean      gracefully,
                                                 gpointer      user_data);

static void
filetea_node_class_init (FileteaNodeClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = filetea_node_dispose;
  obj_class->finalize = filetea_node_finalize;

  g_type_class_add_private (obj_class, sizeof (FileteaNodePrivate));
}

static void
filetea_node_init (FileteaNode *self)
{
  FileteaNodePrivate *priv;

  priv = FILETEA_NODE_GET_PRIVATE (self);
  self->priv = priv;

  /* protocol */
  priv->protocol = filetea_protocol_new (&self->priv->protocol_vtable,
                                         self,
                                         NULL);

  /* fill protocol's virtual table */
  priv->protocol_vtable.register_source = register_source;
  priv->protocol_vtable.unregister_source = unregister_source;
  priv->protocol_vtable.content_request = content_request;
  priv->protocol_vtable.content_push = content_push;

  /* hash tables for indexing sources */
  self->priv->sources_by_id =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free,
                           g_object_unref);
  self->priv->sources_by_peer =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) g_hash_table_unref);

  /* hash tables for indexing file transfers */
  self->priv->transfers_by_id =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free,
                           g_object_unref);

  /*
  self->priv->transfers_by_peer =
    g_hash_table_new_full (g_direct_hash,
                           g_direct_equal,
                           NULL,
                           (GDestroyNotify) g_queue_free);
  */

  priv->report_transfers_src_id = 0;
}

static void
filetea_node_dispose (GObject *obj)
{
  FileteaNode *self = FILETEA_NODE (obj);

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
  EvdTransport *transport;
  EvdTransport *ws_transport;

  g_free (self->priv->id);
  g_free (self->priv->key);

  g_object_unref (self->priv->protocol);

  transport = filetea_web_service_get_transport (self->priv->web_service);
  g_signal_handlers_disconnect_by_func (transport,
                                        on_new_peer,
                                        self);
  g_signal_handlers_disconnect_by_func (transport,
                                        on_peer_closed,
                                        self);

  g_object_get (transport, "websocket-service", &ws_transport, NULL);
  g_signal_handlers_disconnect_by_func (ws_transport,
                                        on_new_peer,
                                        self);
  g_signal_handlers_disconnect_by_func (ws_transport,
                                        on_peer_closed,
                                        self);
  g_object_unref (ws_transport);

  g_object_unref (self->priv->web_service);

  G_OBJECT_CLASS (filetea_node_parent_class)->finalize (obj);
}

static gboolean
load_config (FileteaNode *self, GKeyFile *config, GError **error)
{
  /* node id */
  self->priv->id = g_key_file_get_string (config, "node", "id", error);
  if (self->priv->id == NULL)
    return FALSE;

  /* node key */
  self->priv->key = g_key_file_get_string (config, "node", "key", NULL);
  if (self->priv->key == NULL)
    {
      /* generate a random key */
      self->priv->key = evd_uuid_new ();
    }

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

  return TRUE;
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

static gchar *
source_get_signature (FileteaNode *self, FileteaSource *source)
{
  GHmac *hmac;
  gchar *signature = NULL;
  gchar *data;

  hmac = g_hmac_new (G_CHECKSUM_SHA256,
                     (const guchar *) self->priv->key,
                     strlen (self->priv->key));

  /* data to be signed is: <id>:<content-type>:<flags> */
  data = g_strdup_printf ("%s:%s:%u",
                          filetea_source_get_id (source),
                          filetea_source_get_content_type (source),
                          filetea_source_get_flags (source));

  g_hmac_update (hmac, (const guchar *) data, -1);

  signature = g_strdup (g_hmac_get_string (hmac));

  g_free (data);
  g_hmac_unref (hmac);

  return signature;
}

static gboolean
source_check_signature (FileteaNode   *self,
                        FileteaSource *source,
                        const gchar   *signature)
{
  gboolean result;
  gchar *actual_signature;

  actual_signature = source_get_signature (self, source);
  result = g_strcmp0 (signature, actual_signature) == 0;

  g_free (actual_signature);

  return result;
}

static gboolean
register_source (FileteaProtocol  *protocol,
                 EvdPeer          *peer,
                 FileteaSource    *source,
                 GError          **error,
                 gpointer          user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  GHashTable *sources_of_peer;

  /* check if an existing id is being claimed */
  if (filetea_source_get_id (source) != NULL)
    {
      const gchar *source_id;
      const gchar *source_sig;

      source_id = filetea_source_get_id (source);
      source_sig = filetea_source_get_signature (source);

      /* validate the signature */
      if (source_check_signature (self, source, source_sig))
        {
          FileteaSource *current_source;

          /* registration seems authentic */

          /* check if the source is already registered */
          current_source = g_hash_table_lookup (self->priv->sources_by_id,
                                                source_id);
          if (current_source != NULL)
            {
              sources_of_peer =
                g_hash_table_lookup (self->priv->sources_by_peer,
                                     filetea_source_get_peer (current_source));
              g_assert (sources_of_peer != NULL);
              g_hash_table_remove (sources_of_peer, source_id);

              /* already registered, just update the fields and return success */
              filetea_source_set_peer (current_source, peer);

              /* fill 'sources_by_peer' table */
              sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
              if (sources_of_peer == NULL)
                {
                  sources_of_peer =
                    g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           g_object_unref);
                  g_hash_table_insert (self->priv->sources_by_peer, peer, sources_of_peer);
                }
              g_hash_table_insert (sources_of_peer,
                                   g_strdup (filetea_source_get_id (source)),
                                   g_object_ref (current_source));

              /* @TODO: update the rest of the fields */

              return TRUE;
            }
        }
      else
        {
          /* invalid signature, return error */
          g_set_error (error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Invalid source signature");
          return FALSE;
        }
    }
  else
    {
      gchar *source_id;
      gchar *source_sig;

      /* create a fresh source id */
      source_id = generate_source_id (self, self->priv->id);

      /* store id in source object */
      filetea_source_set_id (source, source_id);

      /* create a signature for this source */
      /* @TODO: do this asynchonously to avoid blocking */
      source_sig = source_get_signature (self, source);
      filetea_source_set_signature (source, source_sig);

      g_free (source_id);
      g_free (source_sig);
    }

  /* fill 'sources_by_id' table */
  g_hash_table_insert (self->priv->sources_by_id,
                       g_strdup (filetea_source_get_id (source)),
                       g_object_ref (source));

  /* fill 'sources_by_peer' table */
  sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
  if (sources_of_peer == NULL)
    {
      sources_of_peer =
        g_hash_table_new_full (g_str_hash,
                               g_str_equal,
                               g_free,
                               g_object_unref);
      g_hash_table_insert (self->priv->sources_by_peer, peer, sources_of_peer);
    }
  g_hash_table_insert (sources_of_peer,
                       g_strdup (filetea_source_get_id (source)),
                       g_object_ref (source));

  /* @TODO: if source is public, index it */

  /* @TODO: write corresponding entry in filetea log file */

  return TRUE;
}

static void
remove_source (FileteaNode *self, FileteaSource *source, gboolean graceful)
{
  /* @TODO: if source was public, un-index it */

  /* @TODO: if removal is not 'graceful', abort related transfers */

  /* @TODO: notify subscribers that source is gone */

  /* @TODO: write corresponding entry in filetea log file */

  /* finally, remove source */
  g_hash_table_remove (self->priv->sources_by_id,
                       filetea_source_get_id (source));
}

static gboolean
unregister_source (FileteaProtocol *protocol,
                   EvdPeer         *peer,
                   const gchar     *id,
                   gboolean         gracefully,
                   gpointer         user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  FileteaSource *source;
  GHashTable *sources_of_peer;

  /* find source to unregister */
  source = g_hash_table_lookup (self->priv->sources_by_id, id);
  if (source == NULL)
    return FALSE;

  /* check that the peer unregistering is the owner of the source */
  if (peer != filetea_source_get_peer (source))
    {
      /* @TODO: buggy client or possible attack, log acordingly */
      return FALSE;
    }

  /* remove source from peer's own table */
  sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
  g_assert (sources_of_peer != NULL);
  g_hash_table_remove (sources_of_peer, id);

  /* remove source from main table */
  remove_source (self, source, gracefully);

  return TRUE;
}

static void
transfer_on_completed (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  FileteaTransfer *transfer = FILETEA_TRANSFER (obj);
  FileteaNode *self = FILETEA_NODE (user_data);
  GError *error = NULL;

  if (! filetea_transfer_finish (transfer, result, &error))
    {
      g_print ("Transfer failed: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      /* @TODO */
      /*      g_print ("Transfer completed\n"); */
    }

  /* remove transfer */
  g_hash_table_remove (self->priv->transfers_by_id,
                       filetea_transfer_get_id (transfer));
}

static void
content_request (FileteaProtocol    *protocol,
                 FileteaSource      *source,
                 EvdHttpConnection  *conn,
                 const gchar        *action,
                 const gchar        *peer_id,
                 gboolean            is_chunked,
                 SoupRange          *byte_range,
                 gpointer            user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  FileteaTransfer *transfer;
  GError *error = NULL;

  if (is_chunked)
    {
      guint flags;

      flags = filetea_source_get_flags (source);
      if ((flags & FILETEA_SOURCE_FLAGS_CHUNKABLE) == 0)
        {
          evd_web_service_respond (EVD_WEB_SERVICE (self->priv->web_service),
                                   conn,
                                   SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE,
                                   NULL,
                                   NULL,
                                   0,
                                   NULL);
          return;
        }
    }

  /* create new transfer */
  transfer = filetea_transfer_new (source,
                                   EVD_WEB_SERVICE (self->priv->web_service),
                                   conn,
                                   action,
                                   is_chunked,
                                   byte_range,
                                   filetea_source_get_cancellable (source),
                                   transfer_on_completed,
                                   self);

  /* fill 'transfers-by-id' table */
  g_hash_table_insert (self->priv->transfers_by_id,
                       g_strdup (filetea_transfer_get_id (transfer)),
                       transfer);

  /* associate target peer with transfer */
  if (peer_id != NULL)
    {
      EvdTransport *transport;
      EvdPeer *peer = NULL;

      transport = filetea_web_service_get_transport (self->priv->web_service);
      peer = evd_transport_lookup_peer (transport, peer_id);

      if (peer != NULL)
        filetea_transfer_set_target_peer (transfer, peer);
    }

  /* notify source peer */
  filetea_protocol_request_content (self->priv->protocol,
                                    filetea_source_get_peer (source),
                                    filetea_source_get_id (source),
                                    filetea_transfer_get_id (transfer),
                                    is_chunked,
                                    byte_range,
                                    &error);

  if (error != NULL)
    {
      g_print ("Failed to notify source peer of new transfer: %s\n", error->message);
      g_error_free (error);

      /* @TODO: abort transfer */
      return;
    }

  /* @TODO: set transfer bandwidth limits */

  g_print ("requested '%s'\n", filetea_source_get_name (source));
}

static void
content_push (FileteaProtocol    *protocol,
              FileteaTransfer    *transfer,
              EvdHttpConnection  *conn,
              gpointer            user_data)
{
  /* set source connection of transfer */
  filetea_transfer_set_source_conn (transfer, conn);

  /* start the transfer */
  filetea_transfer_start (transfer);
}

static gboolean
remove_source_foreach (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  FileteaSource *source = FILETEA_SOURCE (value);

  remove_source (self, source, FALSE);

  return TRUE;
}

static void
on_new_peer (EvdTransport *transport,
             EvdPeer      *peer,
             gpointer      user_data)
{
  g_print ("New peer %s\n", evd_peer_get_id (peer));
}

static void
on_peer_closed (EvdTransport *transport,
                EvdPeer      *peer,
                gboolean      gracefully,
                gpointer      user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  GHashTable *sources_of_peer;

  sources_of_peer = g_hash_table_lookup (self->priv->sources_by_peer, peer);
  if (sources_of_peer != NULL)
    {
      g_hash_table_foreach_remove (sources_of_peer,
                                   remove_source_foreach,
                                   self);

      g_hash_table_remove (self->priv->sources_by_peer, peer);
    }

  g_print ("Closed peer %s\n", evd_peer_get_id (peer));
}

static void
web_service_on_content_request (FileteaWebService *web_service,
                                const gchar       *content_id,
                                EvdHttpConnection *conn,
                                EvdHttpRequest    *request,
                                gpointer           user_data)
{
  FileteaNode *self = FILETEA_NODE (user_data);
  GError *error = NULL;

  /* validate content id */
  if (content_id == NULL || content_id[0] == '\0')
    {
      evd_web_service_respond (EVD_WEB_SERVICE (web_service),
                               conn,
                               SOUP_STATUS_FORBIDDEN,
                               NULL,
                               NULL,
                               0,
                               NULL);
      return;
    }

  const gchar *method = evd_http_request_get_method (request);

  if (g_strcmp0 (method, SOUP_METHOD_GET) == 0)
    {
      FileteaSource *source;

      /* lookup corresponding source */
      source = g_hash_table_lookup (self->priv->sources_by_id, content_id);
      if (source == NULL)
        goto not_found;

      /* let protocol handle the HTTP request */
      if (! filetea_protocol_handle_content_request (self->priv->protocol,
                                      source,
                                      EVD_WEB_SERVICE (self->priv->web_service),
                                      conn,
                                      request,
                                      &error))
        {
          g_print ("Failed: %s\n", error->message);
          g_error_free (error);
        }
    }
  else if (g_strcmp0 (method, SOUP_METHOD_POST) == 0)
    {
      FileteaTransfer *transfer;

      /* lookup corresponding transfer */
      transfer = g_hash_table_lookup (self->priv->transfers_by_id, content_id);
      if (transfer == NULL)
        goto not_found;

      /* let protocol handle the HTTP push */
      if (! filetea_protocol_handle_content_push (self->priv->protocol,
                                      transfer,
                                      EVD_WEB_SERVICE (self->priv->web_service),
                                      conn,
                                      request,
                                      &error))
        {
          g_print ("Failed: %s\n", error->message);
          g_error_free (error);
        }
    }

  return;

 not_found:
  evd_web_service_respond (EVD_WEB_SERVICE (web_service),
                           conn,
                           SOUP_STATUS_NOT_FOUND,
                           NULL,
                           NULL,
                           0,
                           NULL);
}

/* public methods */

FileteaNode *
filetea_node_new (GKeyFile *config, GError **error)
{
  FileteaNode *self;
  EvdTransport *transport;
  EvdTransport *ws_transport;
  EvdJsonrpc *rpc;

  g_return_val_if_fail (config != NULL, NULL);

  self = g_object_new (FILETEA_TYPE_NODE, NULL);

  /* load config */
  if (! load_config (self, config, error))
    goto err;

  /* create web service */
  self->priv->web_service =
    filetea_web_service_new (config,
                             web_service_on_content_request,
                             self,
                             error);
  if (self->priv->web_service == NULL)
    goto err;

  /* associate web service transport with protocol's RPC object */
  rpc = filetea_protocol_get_rpc (self->priv->protocol);

  transport = filetea_web_service_get_transport (self->priv->web_service);
  evd_ipc_mechanism_use_transport (EVD_IPC_MECHANISM (rpc), transport);

  g_object_get (transport, "websocket-service", &ws_transport, NULL);
  evd_ipc_mechanism_use_transport (EVD_IPC_MECHANISM (rpc), ws_transport);

  /* track peers coming from transport */
  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    self);
  g_signal_connect (transport,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    self);

  g_signal_connect (ws_transport,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    self);
  g_signal_connect (ws_transport,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    self);
  g_object_unref (ws_transport);

  return self;

 err:
  g_object_unref (self);
  return NULL;
}

const gchar *
filetea_node_get_id (FileteaNode *self)
{
  g_return_val_if_fail (FILETEA_IS_NODE (self), NULL);

  return self->priv->id;
}

FileteaWebService *
filetea_node_get_web_service (FileteaNode *self)
{
  g_return_val_if_fail (FILETEA_IS_NODE (self), NULL);

  return self->priv->web_service;
}

#ifdef ENABLE_TESTS

FileteaProtocol *
filetea_node_get_protocol (FileteaNode *self)
{
  g_return_val_if_fail (FILETEA_IS_NODE (self), NULL);

  return self->priv->protocol;
}

GList *
filetea_node_get_all_sources (FileteaNode *self)
{
  g_return_val_if_fail (FILETEA_IS_NODE (self), NULL);

  return g_hash_table_get_values (self->priv->sources_by_id);
}

#endif /* ENABLE_TESTS */
