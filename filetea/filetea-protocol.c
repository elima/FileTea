/*
 * filetea-protocol.c
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2012, Igalia S.L.
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

#include "filetea-protocol.h"

G_DEFINE_TYPE (FileteaProtocol, filetea_protocol, G_TYPE_OBJECT)

#define FILETEA_PROTOCOL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           FILETEA_TYPE_PROTOCOL, \
                                           FileteaProtocolPrivate))

#define OP_REGISTER   "register"
#define OP_UNREGISTER "unregister"

typedef enum
{
  OPERATION_INVALID,
  OPERATION_REGISTER,
  OPERATION_UNREGISTER
} FileteaProtocolOperations;

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
  evd_jsonrpc_set_method_call_callback (priv->rpc,
                                        rpc_on_method_called,
                                        self);

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
  FileteaProtocolOperations op;

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
      FileteaSource *source;

      gchar *id = NULL;
      gchar *signature = NULL;

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
                  if (! JSON_NODE_HOLDS_VALUE (node))
                    {
                      /* @TODO */
                    }
                  else
                    {
                      tags[j] = g_strdup (json_node_get_string (node));
                    }
                }
            }
        }

      /* create source */
      source = filetea_source_new (name,
                                   type,
                                   size,
                                   flags,
                                   (const gchar **) tags);
      g_strfreev (tags);

      /* call 'register_source' virtual method */
      self->priv->vtable->register_source (self,
                                           source,
                                           &id,
                                           &signature,
                                           self->priv->user_data);

      /* fill the registration object to respond */
      json_object_set_null_member (reg_node_obj, "error");
      json_object_set_string_member (reg_node_obj, "id", id);
      json_object_set_string_member (reg_node_obj, "signature", signature);

      g_free (id);
      g_free (signature);

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
      const gchar *source_id;
      JsonObject *reg_node_obj;

      reg_node_obj = json_object_new ();

      node = json_array_get_element (items, i);

      if (! JSON_NODE_HOLDS_VALUE (node) ||
          json_node_get_string (node) == NULL ||
          strlen (json_node_get_string (node)) == 0)
        {
          g_set_error (&error,
                       G_IO_ERROR,
                       G_IO_ERROR_INVALID_ARGUMENT,
                       "Unregister expects an array of source id strings");
          goto done;
        }

      source_id = json_node_get_string (node);

      /* call 'unregister_source' virtual method */
      self->priv->vtable->unregister_source (self,
                                             EVD_PEER (context),
                                             source_id,
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
