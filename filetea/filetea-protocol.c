/*
 * filetea-protocol.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2012-2014, Igalia S.L.
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

#include <libsoup/soup.h>

#include "filetea-protocol.h"

G_DEFINE_TYPE (FileteaProtocol, filetea_protocol, G_TYPE_OBJECT)

#define FILETEA_PROTOCOL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           FILETEA_TYPE_PROTOCOL, \
                                           FileteaProtocolPrivate))

#define OP_REGISTER            "register"
#define OP_UNREGISTER          "unregister"
#define OP_SEEDER_PUSH_REQUEST "push-request"

#define DEFAULT_ACTION "download"

#define URI_QUERY_ACTION_KEY "action"
#define URI_QUERY_PEER_KEY   "peer"

/* private data */
struct _FileteaProtocolPrivate
{
  EvdJsonrpc *rpc;

  FileteaProtocolVTable *vtable;

  gpointer user_data;
  GDestroyNotify user_data_free_func;
};

static void     filetea_protocol_class_init         (FileteaProtocolClass *class);
static void     filetea_protocol_init               (FileteaProtocol *self);

static void     filetea_protocol_finalize           (GObject *obj);
static void     filetea_protocol_dispose            (GObject *obj);

static void     rpc_on_method_called                (EvdJsonrpc  *jsonrpc,
                                                     const gchar *method_name,
                                                     JsonNode    *params,
                                                     guint        invocation_id,
                                                     gpointer     context,
                                                     gpointer     user_data);
static void     rpc_on_notification                 (EvdJsonrpc  *jsonrpc,
                                                     const gchar *method_name,
                                                     JsonNode    *params,
                                                     gpointer     context,
                                                     gpointer     user_data);

static void
filetea_protocol_class_init (FileteaProtocolClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = filetea_protocol_dispose;
  obj_class->finalize = filetea_protocol_finalize;

  g_type_class_add_private (obj_class, sizeof (FileteaProtocolPrivate));
}

static void
filetea_protocol_init (FileteaProtocol *self)
{
  FileteaProtocolPrivate *priv;

  priv = FILETEA_PROTOCOL_GET_PRIVATE (self);
  self->priv = priv;

  /* JSON-RPC */
  priv->rpc = evd_jsonrpc_new ();
  evd_jsonrpc_set_callbacks (priv->rpc,
                             rpc_on_method_called,
                             rpc_on_notification,
                             self,
                             NULL);

  priv->vtable = NULL;
}

static void
filetea_protocol_dispose (GObject *obj)
{
  FileteaProtocol *self = FILETEA_PROTOCOL (obj);

  if (self->priv->user_data != NULL && self->priv->user_data_free_func != NULL)
    {
      self->priv->user_data_free_func (self->priv->user_data);
      self->priv->user_data = NULL;
    }

  G_OBJECT_CLASS (filetea_protocol_parent_class)->dispose (obj);
}

static void
filetea_protocol_finalize (GObject *obj)
{
  FileteaProtocol *self = FILETEA_PROTOCOL (obj);

  g_object_unref (self->priv->rpc);

  G_OBJECT_CLASS (filetea_protocol_parent_class)->finalize (obj);
}

typedef struct
{
  guint invocation_id;
  gpointer context;

  JsonNode *result;
  GError *error;
  JsonNode **result_elements;

  guint num_ops;
  guint num_completed_ops;
} AsyncOpData;

typedef struct
{
  AsyncOpData *data;
  guint op_index;
} FileteaProtocolOperation;

static void
op_register_content (FileteaProtocol *self,
                     JsonNode        *params,
                     guint            invocation_id,
                     gpointer         context)
{
  JsonArray *a;
  gint i;
  GError *error = NULL;

  JsonArray *result_arr = NULL;

  if (self->priv->vtable->register_source == NULL)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "'register' operation not implemented");
      goto out;
    }

  result_arr = json_array_new ();

  a = json_node_get_array (params);
  for (i=0; i<json_array_get_length (a); i++)
    {
      JsonNode *node;
      JsonObject *obj;

      JsonObject *reg_node_obj;

      const gchar *name;
      const gchar *type;
      gint64 num;
      gsize size = 0;
      guint flags = FILETEA_SOURCE_FLAGS_NONE;
      gchar **tags = NULL;
      FileteaSource *source = NULL;

      reg_node_obj = json_object_new ();

      node = json_array_get_element (a, i);
      if (! JSON_NODE_HOLDS_OBJECT (node))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Method register expects an array of objects");
          goto done;
        }

      obj = json_node_get_object (node);

      /* name */
      if (! json_object_has_member (obj, "name") ||
          ! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "name")) ||
          strlen (json_object_get_string_member (obj, "name")) == 0)
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Source object expects a 'name' member to be a string");
          goto done;
        }
      else
        {
          name = json_object_get_string_member (obj, "name");
        }

      /* type */
      if (! json_object_has_member (obj, "type") ||
          ! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "type")) ||
          strlen (json_object_get_string_member (obj, "type")) == 0)
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Source object expects a 'type' member to be a string");
          goto done;
        }
      else
        {
          type = json_object_get_string_member (obj, "type");
        }

      /* size */
      if (json_object_has_member (obj, "size"))
        {
          if (! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "size")))
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Source object expects a 'size' member to be a number");
              goto done;
            }
          else
            {
              num = json_object_get_int_member (obj, "size");
              if (num < 0)
                {
                  g_set_error (&error,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_ARGUMENT,
                               "Source size must be equal or greater than zero");
                  goto done;
                }
              else
                {
                  size = (gsize) num;
                }
            }
        }

      /* flags */
      if (! json_object_has_member (obj, "flags") ||
          ! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "flags")))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Source object expects a 'flags' member to be a number");
          goto done;
        }
      else
        {
          num = json_object_get_int_member (obj, "flags");
          if (num < 0)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Source flags must be equal or greater than zero");
              goto done;
            }
          else
            {
              flags = (gsize) num;
            }
        }

      /* tags */
      if (json_object_has_member (obj, "tags"))
        {
          if (! JSON_NODE_HOLDS_ARRAY (json_object_get_member (obj, "tags")))
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Source tags must be an array");
              goto done;
            }
          else
            {
              JsonArray *json_tags;
              guint len;
              gint j;

              json_tags = json_object_get_array_member (obj, "tags");
              len = json_array_get_length (json_tags);

              tags = g_new0 (gchar *, len + 1);
              tags[len] = NULL;

              for (j=0; j<len; j++)
                {
                  node = json_array_get_element (json_tags, j);
                  if (JSON_NODE_HOLDS_VALUE (node) &&
                      json_node_get_string (node) != NULL &&
                      strlen (json_node_get_string (node)) > 0)
                    {
                      tags[j] = g_strdup (json_node_get_string (node));
                    }
                  else
                    {
                      /* @TODO: invalid tag, not a valid string */
                    }
                }
            }
        }

      /* create source */
      source = filetea_source_new (EVD_PEER (context),
                                   name,
                                   type,
                                   size,
                                   flags,
                                   (const gchar **) tags);
      g_strfreev (tags);

      /* check object has 'id' and 'signature', in which case is a registration
         claiming for a previously assigned id. */
      if (json_object_has_member (obj, "id") &&
          json_object_has_member (obj, "signature"))
        {
          const gchar *_id;
          const gchar *_signature;

          _id = json_object_get_string_member (obj, "id");
          _signature = json_object_get_string_member (obj, "signature");

          if (_id != NULL && _signature != NULL)
            {
              filetea_source_set_id (source, _id);
              filetea_source_set_signature (source, _signature);
            }
        }

      /* call 'register_source' virtual method */
      if (! self->priv->vtable->register_source (self,
                                                 EVD_PEER (context),
                                                 source,
                                                 &error,
                                                 self->priv->user_data))
        {
          goto done;
        }

      /* fill the registration object to respond */
      json_object_set_null_member (reg_node_obj, "error");
      json_object_set_string_member (reg_node_obj,
                                     "id",
                                     filetea_source_get_id (source));
      json_object_set_string_member (reg_node_obj,
                                     "signature",
                                     filetea_source_get_signature (source));

    done:
      if (source != NULL)
        g_object_unref (source);

      if (error != NULL)
        {
          json_object_set_string_member (reg_node_obj, "error", error->message);
          g_clear_error (&error);
        }

      /* add the registration object to the response list */
      json_array_add_object_element (result_arr, reg_node_obj);
    }

 out:
  if (error != NULL)
    {
      evd_jsonrpc_respond_from_error (self->priv->rpc,
                                      invocation_id,
                                      error,
                                      context,
                                      NULL);
      g_error_free (error);
    }
  else
    {
      JsonNode *result;

      result = json_node_new (JSON_NODE_ARRAY);
      json_node_set_array (result, result_arr);

      evd_jsonrpc_respond (self->priv->rpc,
                           invocation_id,
                           result,
                           context,
                           NULL);

      json_node_free (result);
      if (result_arr != NULL)
        json_array_unref (result_arr);
    }
}

static void
op_unregister_content (FileteaProtocol *self,
                       JsonNode        *params,
                       guint            invocation_id,
                       gpointer         context)
{
  JsonArray *items;
  gint i;
  GError *error = NULL;

  JsonArray *result_arr = NULL;

  if (self->priv->vtable->unregister_source == NULL)
    {
      g_set_error (&error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_SUPPORTED,
                   "'unregister' operation not implemented");
      goto out;
    }

  result_arr = json_array_new ();

  items = json_node_get_array (params);
  for (i=0; i<json_array_get_length (items); i++)
    {
      JsonNode *node;
      JsonObject *obj;

      const gchar *source_id;
      JsonObject *reg_node_obj;
      gboolean gracefully = FALSE;

      reg_node_obj = json_object_new ();

      node = json_array_get_element (items, i);

      if (! JSON_NODE_HOLDS_OBJECT (node))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Method unregister expects an array of objects");
          goto done;
        }

      obj = json_node_get_object (node);

      /* source id */
      if (! json_object_has_member (obj, "id") ||
          ! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "id")))
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Unregister expects an array of source id strings");
          goto done;
        }
      else
        {
          source_id = json_object_get_string_member (obj, "id");
          if (source_id == NULL || strlen (source_id) == 0)
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Source id must be a valid string");
              goto done;
            }
        }

      /* force */
      if (json_object_has_member (obj, "force"))
        {
          if (! JSON_NODE_HOLDS_VALUE (json_object_get_member (obj, "force")))
            {
              g_set_error (&error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "Argument 'force' must be boolean");
              goto done;
            }
          else
            {
              gracefully = ! json_object_get_boolean_member (obj, "force");
            }
        }

      /* call 'unregister_source' virtual method */
      self->priv->vtable->unregister_source (self,
                                             EVD_PEER (context),
                                             source_id,
                                             gracefully,
                                             self->priv->user_data);

      /* fill the registration object to respond */
      /* for security reasons, we always return TRUE even if the unregistration
         returned FALSE */
      json_object_set_boolean_member (reg_node_obj, "result", TRUE);

    done:
      if (error != NULL)
        {
          json_object_set_string_member (reg_node_obj, "error", error->message);
          g_clear_error (&error);
        }

      /* add the registration object to the response list */
      json_array_add_object_element (result_arr, reg_node_obj);
    }

 out:
  if (error != NULL)
    {
      evd_jsonrpc_respond_from_error (self->priv->rpc,
                                      invocation_id,
                                      error,
                                      context,
                                      NULL);
      g_error_free (error);
    }
  else
    {
      JsonNode *result;

      result = json_node_new (JSON_NODE_ARRAY);
      json_node_set_array (result, result_arr);

      evd_jsonrpc_respond (self->priv->rpc,
                           invocation_id,
                           result,
                           context,
                           NULL);

      json_node_free (result);
      if (result_arr != NULL)
        json_array_unref (result_arr);
    }
}

static void
rpc_on_method_called (EvdJsonrpc  *jsonrpc,
                      const gchar *method_name,
                      JsonNode    *params,
                      guint        invocation_id,
                      gpointer     context,
                      gpointer     user_data)
{
  FileteaProtocol *self = FILETEA_PROTOCOL (user_data);

  if (g_strcmp0 (method_name, OP_REGISTER) == 0)
    {
      op_register_content (self, params, invocation_id, context);
    }
  else if (g_strcmp0 (method_name, OP_UNREGISTER) == 0)
    {
      op_unregister_content (self, params, invocation_id, context);
    }
}

static void
op_seeder_push_request (FileteaProtocol *self,
                        JsonNode        *params,
                        guint            invocation_id,
                        gpointer         context)
{
  GError *error = NULL;

  GSimpleAsyncResult *async_result = NULL;
  const gchar *source_id;
  const gchar *transfer_id;
  gboolean is_chunked = FALSE;
  SoupRange byte_range;

  JsonArray *args;
  guint args_len;

#define SET_ERROR_AND_OUT(err_code,err_msg) g_set_error (&error,      \
                                                         G_IO_ERROR,  \
                                                         err_code,    \
                                                         err_msg);

  /* if method is not implemented, don't even bother */
  if (self->priv->vtable->seeder_push_request == NULL)
    {
      SET_ERROR_AND_OUT (G_IO_ERROR_NOT_SUPPORTED,
                         "Operation not implemented");
      goto out;
    }

  /* validate params */
  if (! JSON_NODE_HOLDS_ARRAY (params))
    {
      SET_ERROR_AND_OUT (G_IO_ERROR_INVALID_ARGUMENT,
                         "Push Request operation expects an array argument");
      goto out;
    }

  args = json_node_get_array (params);
  args_len = json_array_get_length (args);

  if (args_len < 2)
    {
      SET_ERROR_AND_OUT (G_IO_ERROR_INVALID_ARGUMENT,
                         "Push Request operation expects an array of at least 2 elements");
      goto out;
    }

  /* source id */
  source_id = json_array_get_string_element (args, 0);
  if (source_id == NULL)
    {
      SET_ERROR_AND_OUT (G_IO_ERROR_INVALID_ARGUMENT,
                         "First argument of Push Request operation must be a source id string");
      goto out;
    }

  /* transfer id */
  transfer_id = json_array_get_string_element (args, 1);
  if (transfer_id == NULL)
    {
      SET_ERROR_AND_OUT (G_IO_ERROR_INVALID_ARGUMENT,
                         "First argument of Push Request operation must be a source id string");
      goto out;
    }

  /* byte range */
  if (args_len > 2)
    {
      is_chunked = TRUE;

      byte_range.start = json_array_get_int_element (args, 2);
      if (args_len > 3)
        byte_range.end = json_array_get_int_element (args, 3);
      else
        byte_range.end = -1;
    }

  /* @TODO: create an async result and attach it the invocation id */

  /* call virtual method */
  self->priv->vtable->seeder_push_request (self,
                                           G_ASYNC_RESULT (async_result),
                                           source_id,
                                           transfer_id,
                                           is_chunked,
                                           &byte_range,
                                           self->priv->user_data);

 out:
  if (error != NULL)
    {
      g_print ("Push request error: %s\n", error->message);
      evd_jsonrpc_respond_from_error (self->priv->rpc,
                                      invocation_id,
                                      error,
                                      context,
                                      NULL);
      g_error_free (error);
    }

#undef SET_ERROR_AND_OUT
}

static void
rpc_on_notification (EvdJsonrpc  *jsonrpc,
                     const gchar *method_name,
                     JsonNode    *params,
                     gpointer     context,
                     gpointer     user_data)
{
  FileteaProtocol *self = FILETEA_PROTOCOL (user_data);

  if (g_strcmp0 (method_name, OP_SEEDER_PUSH_REQUEST) == 0)
    {
      op_seeder_push_request (self, params, 0, context);
    }
}

static JsonNode *
filetea_source_to_json (FileteaSource *source)
{
  JsonNode *node;
  JsonObject *obj;
  const gchar **tags;

  node = json_node_new (JSON_NODE_OBJECT);
  obj = json_object_new ();
  json_node_take_object (node, obj);

  json_object_set_string_member (obj,
                                 "name",
                                 filetea_source_get_name (source));
  json_object_set_string_member (obj,
                                 "type",
                                 filetea_source_get_content_type (source));
  json_object_set_int_member (obj, "size", filetea_source_get_size (source));
  json_object_set_int_member (obj, "flags", filetea_source_get_flags (source));

  if (filetea_source_get_id (source) != NULL)
    json_object_set_string_member (obj, "id", filetea_source_get_id (source));

  if (filetea_source_get_signature (source) != NULL)
    json_object_set_string_member (obj,
                                   "signature",
                                   filetea_source_get_signature (source));

  tags = filetea_source_get_tags (source);
  if (tags != NULL)
    {
      JsonArray *tags_arr;
      gint i;

      tags_arr = json_array_new ();
      for (i=0; tags[i] != NULL; i++)
        json_array_add_string_element (tags_arr, tags[i]);

      json_object_set_array_member (obj, "tags", tags_arr);
    }

  return node;
}

static void
rpc_on_register_sources (GObject      *obj,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  JsonNode *result_json;
  JsonNode *err_json;
  GError *error = NULL;

  if (! evd_jsonrpc_call_method_finish (EVD_JSONRPC (obj),
                                        res,
                                        &result_json,
                                        &err_json,
                                        &error))
    {
      g_print ("Error registering sources: %s\n", error->message);
      g_simple_async_result_take_error (result, error);
    }
  else if (err_json != NULL)
    {
      /* @TODO */
      json_node_free (err_json);
    }
  else
    {
      JsonArray *arr;
      guint len;
      gint i;
      GList *sources;
      GList *node;

      sources = g_simple_async_result_get_op_res_gpointer (result);
      g_assert (sources != NULL);

      arr = json_node_get_array (result_json);
      len = json_array_get_length (arr);

      if (len != g_list_length (sources))
        g_error ("Protocol error, number of registered sources differ\n");

      node = sources;
      for (i=0; i<json_array_get_length (arr); i++)
        {
          JsonObject *obj;
          JsonNode *err_member;

          obj = json_array_get_object_element (arr, i);
          err_member = json_object_get_member (obj, "error");

          if (json_node_is_null (err_member))
            {
              FileteaSource *source;

              source = FILETEA_SOURCE (node->data);
              filetea_source_set_id (source,
                                     json_object_get_string_member (obj, "id"));
              filetea_source_set_signature (source,
                              json_object_get_string_member (obj, "signature"));

            }
          else
            {
              g_print ("Error registering source: %s\n",
                       json_node_get_string (err_member));
            }

          node = g_list_next (node);
        }

      json_node_free (result_json);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/* public methods */

FileteaProtocol *
filetea_protocol_new (FileteaProtocolVTable *vtable,
                      gpointer               user_data,
                      GDestroyNotify         user_data_free_func)
{
  FileteaProtocol *self;

  g_return_val_if_fail (vtable != NULL, NULL);

  self = g_object_new (FILETEA_TYPE_PROTOCOL, NULL);

  self->priv->vtable = vtable;

  self->priv->user_data = user_data;
  self->priv->user_data_free_func = user_data_free_func;

  return self;
}

EvdJsonrpc *
filetea_protocol_get_rpc (FileteaProtocol *self)
{
  g_return_val_if_fail (FILETEA_IS_PROTOCOL (self), NULL);

  return self->priv->rpc;
}

gboolean
filetea_protocol_handle_content_request (FileteaProtocol    *self,
                                         FileteaSource      *source,
                                         EvdWebService      *web_service,
                                         EvdHttpConnection  *conn,
                                         EvdHttpRequest     *request,
                                         GError            **error)
{
  SoupMessageHeaders *headers;
  SoupURI *uri;
  const gchar *uri_query;
  GHashTable *uri_query_items;

  SoupRange *ranges = NULL;
  gint ranges_len;
  gboolean is_chunked = FALSE;

  gchar *action = NULL;
  gchar *peer_id = NULL;

  g_return_val_if_fail (FILETEA_IS_PROTOCOL (self), FALSE);
  g_return_val_if_fail (FILETEA_IS_SOURCE (source), FALSE);
  g_return_val_if_fail (EVD_IS_WEB_SERVICE (web_service), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), FALSE);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  /* check if it is a chunked request */
  if (soup_message_headers_get_ranges (headers, 0, &ranges, &ranges_len))
    {
      /* currently only one range is supported */
      if (ranges_len > 1)
        {
          evd_web_service_respond (EVD_WEB_SERVICE (web_service),
                                   conn,
                                   SOUP_STATUS_INVALID_RANGE,
                                   NULL,
                                   NULL,
                                   0,
                                   NULL);
          goto out;
        }
      else
        {
          /* @TODO: validate range */
          is_chunked = TRUE;
        }
    }

  /* determine the action and leecher id from query arguments */
  uri = evd_http_request_get_uri (request);
  if ( (uri_query = soup_uri_get_query (uri)) != NULL)
    {
      const gchar *_value;

      uri_query_items = soup_form_decode (uri_query);

      _value = g_hash_table_lookup (uri_query_items, URI_QUERY_ACTION_KEY);
      if (_value != NULL)
        action = g_strdup (_value);

      _value = g_hash_table_lookup (uri_query_items, URI_QUERY_PEER_KEY);
      if (_value != NULL)
        peer_id = g_strdup (_value);

      g_hash_table_unref (uri_query_items);
    }

  if (action == NULL)
    action = g_strdup (DEFAULT_ACTION);

  /* call 'content_request' virtual method */
  g_assert (self->priv->vtable->content_request != NULL);
  self->priv->vtable->content_request (self,
                                       source,
                                       conn,
                                       action,
                                       peer_id,
                                       is_chunked,
                                       is_chunked ? &ranges[0] : NULL,
                                       self->priv->user_data);

  g_free (action);
  g_free (peer_id);

 out:
  soup_message_headers_free_ranges (headers, ranges);

  return TRUE;
}

gboolean
filetea_protocol_handle_content_push (FileteaProtocol    *self,
                                      FileteaTransfer    *transfer,
                                      EvdWebService      *web_service,
                                      EvdHttpConnection  *conn,
                                      EvdHttpRequest     *request,
                                      GError            **error)
{
  g_return_val_if_fail (FILETEA_IS_PROTOCOL (self), FALSE);
  g_return_val_if_fail (FILETEA_IS_TRANSFER (transfer), FALSE);
  g_return_val_if_fail (EVD_IS_WEB_SERVICE (web_service), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), FALSE);

  /* call 'content_push' virtual method */
  g_assert (self->priv->vtable->content_push != NULL);
  self->priv->vtable->content_push (self,
                                    transfer,
                                    conn,
                                    self->priv->user_data);

  return TRUE;
}

gboolean
filetea_protocol_request_content (FileteaProtocol  *self,
                                  EvdPeer          *peer,
                                  const gchar      *source_id,
                                  const gchar      *transfer_id,
                                  gboolean          is_chunked,
                                  SoupRange        *byte_range,
                                  GError          **error)
{
  gboolean result;
  JsonNode *params;
  JsonArray *arr;

  g_return_val_if_fail (FILETEA_IS_PROTOCOL (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  params = json_node_new (JSON_NODE_ARRAY);
  arr = json_array_new ();
  json_node_take_array (params, arr);

  json_array_add_string_element (arr, source_id);
  json_array_add_string_element (arr, transfer_id);
  if (is_chunked)
    {
      json_array_add_int_element (arr, byte_range->start);
      json_array_add_int_element (arr, byte_range->end);
    }

  result = evd_jsonrpc_send_notification (self->priv->rpc,
                                          OP_SEEDER_PUSH_REQUEST,
                                          params,
                                          peer,
                                          error);
  json_node_free (params);

  return result;
}
