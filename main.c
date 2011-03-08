#include <string.h>
#include <evd.h>

#include "file-source.h"
#include "file-transfer.h"

#define LISTEN_PORT 8080

#define SOURCE_ID_START_DEPTH 8

#define PATH_REALM_FILE "file"

#define PATH_ACTION_DOWNLOAD "download"

static gint exit_status = 0;

static const gchar *instance_id = "1a0";

static GMainLoop *main_loop;

static EvdPeerManager *peer_manager;
static EvdWebSelector *selector;
static EvdJsonrpc *jsonrpc;

static GHashTable *sources_by_id;
static GHashTable *sources_by_peer;
static GHashTable *transfers;

static void
quit (gint _exit_status)
{
  g_main_loop_quit (main_loop);
  exit_status = _exit_status;
}

static void
on_user_interrupt (gint sig)
{
  signal (SIGINT, NULL);
  quit (-1);
}

static void
web_selector_on_listen (GObject      *service,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  GError *error = NULL;

  if (evd_service_listen_finish (EVD_SERVICE (service), result, &error))
    {
      g_debug ("Listening on port %d", LISTEN_PORT);
    }
  else
    {
      g_debug ("%s", error->message);
      g_error_free (error);
      quit (-1);
    }
}

static void
on_new_transfer_response (GObject      *obj,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  FileTransfer *transfer = user_data;
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

  g_hash_table_remove (transfers, transfer->id);
}

static void
setup_new_transfer (FileSource        *source,
                    EvdHttpConnection *conn,
                    gboolean           download)
{
  FileTransfer *transfer;
  JsonNode *params;
  JsonArray *arr;

  transfer = file_transfer_new (source,
                                conn,
                                download,
                                on_transfer_finished,
                                NULL);

  params = json_node_new (JSON_NODE_ARRAY);
  arr = json_array_new ();
  json_node_set_array (params, arr);

  json_array_add_string_element (arr, source->id);
  json_array_add_string_element (arr, transfer->id);

  evd_jsonrpc_call_method (jsonrpc,
                           "fileTransferNew",
                           params,
                           source->peer,
                           NULL,
                           on_new_transfer_response,
                           transfer);
  json_array_unref (arr);
  json_node_free (params);

  g_hash_table_insert (transfers, transfer->id, transfer);
}

static gboolean
handle_special_request (EvdWebService       *web_streamer,
                        EvdHttpConnection   *conn,
                        EvdHttpRequest      *request,
                        SoupURI             *uri,
                        gchar             **tokens)
{
  const gchar *realm;
  const gchar *id;
  const gchar *action;
  gboolean download;

  realm = tokens[1];
  id = tokens[2];
  action = tokens[3];

  download = g_strcmp0 (action, PATH_ACTION_DOWNLOAD) == 0;

  if (g_strcmp0 (evd_http_request_get_method (request), "PUT") == 0)
    {
      FileTransfer *transfer;

      transfer = g_hash_table_lookup (transfers, id);
      if (transfer != NULL)
        {
          EvdStreamThrottle *throttle;

          g_object_get (conn,
                        "input-throttle", &throttle,
                        NULL);
          g_object_set (throttle,
                        "bandwidth", 2048.0,
                        NULL);
          g_object_unref (throttle);

          file_transfer_set_source_conn (transfer, conn);
          file_transfer_start (transfer);

          return TRUE;
        }
    }
  else
    {
      FileSource *source;

      source = g_hash_table_lookup (sources_by_id, id);
      if (source != NULL)
        {
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
            {
              setup_new_transfer (source, conn, download);
              return TRUE;
            }
        }
    }

  return FALSE;
}

static void
web_streamer_on_request (EvdWebService     *web_streamer,
                         EvdHttpConnection *conn,
                         EvdHttpRequest    *request,
                         gpointer           user_data)
{
  SoupMessageHeaders *headers;
  GError *error = NULL;

  gchar **tokens;
  SoupURI *uri;
  guint tokens_len;

  uri = evd_http_request_get_uri (request);

  tokens = g_strsplit (uri->path, "/", 16);
  tokens_len = g_strv_length (tokens);

  //  g_debug ("%s: %s", evd_http_request_get_method (request), uri->path);

  if (tokens_len < 3 ||
      g_strcmp0 (tokens[1], PATH_REALM_FILE) != 0 ||
      ! handle_special_request (web_streamer,
                                conn,
                                request,
                                uri,
                                tokens))
    {
      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (selector),
                                                   conn,
                                                   request,
                                                   NULL);
    }

  g_strfreev (tokens);
}

static void
remove_file_source (FileSource *source, gboolean abort_transfers)
{
  g_hash_table_remove (sources_by_id, source->id);

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
on_new_peer (EvdPeerManager *self,
             EvdPeer        *peer,
             gpointer        user_data)
{
  g_debug ("New peer %s", evd_peer_get_id (peer));
}

static void
on_peer_closed (EvdPeerManager *self,
                EvdPeer        *peer,
                gpointer        user_data)
{
  GHashTable *sources_of_peer;

  sources_of_peer = g_hash_table_lookup (sources_by_peer, peer);
  if (sources_of_peer != NULL)
    {
      gboolean abort_transfers = TRUE;

      g_hash_table_foreach_remove (sources_of_peer,
                                   remove_file_source_foreach,
                                   &abort_transfers);

      g_hash_table_remove (sources_by_peer, peer);
    }

  g_debug ("Closed peer %s", evd_peer_get_id (peer));
}

static gchar *
generate_source_id (const gchar *instance_id)
{
  gchar *uuid;
  gchar *id;
  static gint depth = SOURCE_ID_START_DEPTH;
  gint _depth;
  gint fails;

  _depth = depth - strlen (instance_id);
  g_assert (_depth > 0 && _depth <= 40);

  fails = 0;

  uuid = evd_uuid_new ();
  uuid[_depth] = '\0';
  while (g_hash_table_lookup (sources_by_id, uuid) != NULL)
    {
      g_free (uuid);
      fails++;
      if (fails > 2)
        {
          depth++;
          _depth = depth - strlen (instance_id);
          fails = 0;
        }

      uuid = evd_uuid_new ();
      uuid[_depth] = '\0';
    }

  id = g_strconcat (instance_id, uuid, NULL);
  g_free (uuid);

  return id;
}

static FileSource *
add_file_source (JsonNode *item, EvdPeer *peer)
{
  gchar *source_id = NULL;
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

  id = generate_source_id (instance_id);
  source = file_source_new (peer, id, name, type, size);
  g_free (id);

  g_hash_table_insert (sources_by_id, source->id, source);

  sources_of_peer = g_hash_table_lookup (sources_by_peer, peer);
  if (sources_of_peer == NULL)
    {
      sources_of_peer = g_hash_table_new_full (g_str_hash,
                                               g_str_equal,
                                               NULL,
                                               (GDestroyNotify) file_source_unref);
      g_hash_table_insert (sources_by_peer, peer, sources_of_peer);
    }
  g_hash_table_insert (sources_of_peer, source->id, file_source_ref (source));

  g_debug ("Added new source '%s'", name);

  return source;
}

static void
remove_file_sources (JsonNode *ids, gboolean abort_transfers)
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

      source = g_hash_table_lookup (sources_by_id, id);
      if (source != NULL)
        {
          GHashTable *sources_of_peer;

          g_debug ("Removed file source '%s'", source->file_name);

          remove_file_source (source, abort_transfers);

          sources_of_peer = g_hash_table_lookup (sources_by_peer, source->peer);
          if (sources_of_peer != NULL)
            {
              g_hash_table_remove (sources_of_peer, source->id);
            }
        }
    }
}

static void
jsonrpc_on_method_called (EvdJsonrpc  *jsonrpc,
                          const gchar *method_name,
                          JsonNode    *params,
                          guint        invocation_id,
                          gpointer     context,
                          gpointer     user_data)
{
  EvdPeer *peer = EVD_PEER (context);
  GError *error = NULL;
  JsonArray *a;
  gint i;
  JsonNode *item;

  a = json_node_get_array (params);

  if (g_strcmp0 (method_name, "addFileSources") == 0)
    {
      FileSource *source;
      JsonNode *result;
      JsonArray *result_arr;

      result = json_node_new (JSON_NODE_ARRAY);
      result_arr = json_array_new ();
      json_node_set_array (result, result_arr);

      for (i = 0; i < json_array_get_length (a); i++)
        {
          item = json_array_get_element (a, i);

          source = add_file_source (item, peer);
          if (source != NULL)
            json_array_add_string_element (result_arr, source->id);
          else
            json_array_add_null_element (result_arr);
        }

      /* respond JSON-RPC method call */
      if (! evd_jsonrpc_respond (jsonrpc,
                                 invocation_id,
                                 result,
                                 peer,
                                 &error))
        {
          g_debug ("error responding JSON-RPC: %s", error->message);
          g_error_free (error);
        }

      json_node_free (result);
      json_array_unref (result_arr);
    }
  else if (g_strcmp0 (method_name, "removeFileSources") == 0)
    {
      gboolean abort_transfers;
      JsonArray *ids;

      item = json_array_get_element (a, 0);
      abort_transfers = json_node_get_boolean (item);

      item = json_array_get_element (a, 1);

      remove_file_sources (item, abort_transfers);

      if (! evd_jsonrpc_respond (jsonrpc,
                                 invocation_id,
                                 NULL,
                                 peer,
                                 &error))
        {
          g_debug ("error responding JSON-RPC: %s", error->message);
          g_error_free (error);
        }
    }
  else
    {
      g_debug ("ERROR: unhandled method: %s", method_name);
    }
}

gint
main (gint argc, gchar *argv[])
{
  EvdSocket *stream_listener;
  EvdWebTransport *transport;
  EvdWebDir *web_dir;
  EvdWebService *web_streamer;
  EvdTlsCredentials *cred;
  gchar *addr;

  g_type_init ();
  evd_tls_init (NULL);

  /* hash tables for indexing file sources */
  sources_by_id = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         NULL,
                                         (GDestroyNotify) file_source_unref);
  sources_by_peer = g_hash_table_new_full (g_direct_hash,
                                           g_direct_equal,
                                           NULL,
                                           (GDestroyNotify) g_hash_table_unref);

  /* hash tables for indexing file transfers */
  transfers = g_hash_table_new_full (g_str_hash,
                                     g_str_equal,
                                     NULL,
                                     (GDestroyNotify) file_transfer_unref);

  /* web streamer */
  web_streamer = evd_web_service_new ();

  g_signal_connect (web_streamer,
                    "request-headers",
                    G_CALLBACK (web_streamer_on_request),
                    NULL);

  addr = g_strdup_printf ("0.0.0.0:%d", LISTEN_PORT);
  evd_service_listen_async (EVD_SERVICE (web_streamer),
                            addr,
                            NULL,
                            web_selector_on_listen,
                            NULL);
  g_free (addr);

  /* web transport */
  transport = evd_web_transport_new ();

  /* JSON-RPC */
  jsonrpc = evd_jsonrpc_new ();
  evd_jsonrpc_set_method_call_callback (jsonrpc,
                                        jsonrpc_on_method_called,
                                        NULL);
  evd_jsonrpc_use_transport (jsonrpc, EVD_TRANSPORT (transport));

  /* peer manager */
  peer_manager = evd_peer_manager_get_default ();
  g_signal_connect (peer_manager,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    NULL);
  g_signal_connect (peer_manager,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    NULL);

  /* web dir */
  web_dir = evd_web_dir_new ();
  evd_web_dir_set_root (web_dir, "./html");

  /* web selector */
  selector = evd_web_selector_new ();

  evd_web_selector_set_default_service (selector, EVD_SERVICE (web_dir));
  evd_web_transport_set_selector (transport, selector);

  /*  evd_service_set_tls_autostart (EVD_SERVICE (selector), TRUE);*/
  cred = evd_service_get_tls_credentials (EVD_SERVICE (selector));

  /* start the show */
  main_loop = g_main_loop_new (NULL, FALSE);

  signal (SIGINT, on_user_interrupt);
  g_main_loop_run (main_loop);

  /* free stuff */
  g_main_loop_unref (main_loop);

  g_object_unref (selector);
  g_object_unref (web_dir);
  g_object_unref (peer_manager);
  g_object_unref (jsonrpc);
  g_object_unref (transport);
  g_object_unref (web_streamer);

  g_hash_table_unref (transfers);
  g_hash_table_unref (sources_by_peer);
  g_hash_table_unref (sources_by_id);

  evd_tls_deinit ();

  return exit_status;
}
