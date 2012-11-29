/*
 * filetea-source.c
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

#include "filetea-source.h"

G_DEFINE_TYPE (FileteaSource, filetea_source, G_TYPE_OBJECT)

#define FILETEA_SOURCE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                         FILETEA_SOURCE_TYPE, \
                                         FileteaSourcePrivate))

/* private data */
struct _FileteaSourcePrivate
{
  gchar *id;
  guint flags;
  gchar *name;
  gchar *type;
  gsize size;
  gchar **tags;

  GList *peers;
};

static void     filetea_source_class_init         (FileteaSourceClass *class);
static void     filetea_source_init               (FileteaSource *self);

static void     filetea_source_finalize           (GObject *obj);
static void     filetea_source_dispose            (GObject *obj);

static void
filetea_source_class_init (FileteaSourceClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = filetea_source_dispose;
  obj_class->finalize = filetea_source_finalize;

  g_type_class_add_private (obj_class, sizeof (FileteaSourcePrivate));
}

static void
filetea_source_init (FileteaSource *self)
{
  FileteaSourcePrivate *priv;

  priv = FILETEA_SOURCE_GET_PRIVATE (self);
  self->priv = priv;

  priv->tags = NULL;
  priv->peers = NULL;
}

static void
filetea_source_dispose (GObject *obj)
{
  FileteaSource *self = FILETEA_SOURCE (obj);

  if (self->priv->peers != NULL)
    {
      g_list_free_full (self->priv->peers, g_object_unref);
      self->priv->peers = NULL;
    }

  G_OBJECT_CLASS (filetea_source_parent_class)->dispose (obj);
}

static void
filetea_source_finalize (GObject *obj)
{
  FileteaSource *self = FILETEA_SOURCE (obj);

  g_free (self->priv->id);
  g_free (self->priv->name);
  g_free (self->priv->type);
  g_strfreev (self->priv->tags);

  G_OBJECT_CLASS (filetea_source_parent_class)->finalize (obj);
}

/* public methods */

FileteaSource *
filetea_source_new (const gchar  *name,
                    const gchar  *type,
                    gsize         size,
                    guint         flags,
                    const gchar **tags)
{
  FileteaSource *self;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);

  self = g_object_new (FILETEA_SOURCE_TYPE, NULL);

  self->priv->name = g_strdup (name);
  self->priv->type = g_strdup (type);
  self->priv->size = size;
  self->priv->flags = flags;
  if (tags != NULL)
    self->priv->tags = g_strdupv ((gchar **) tags);

  return self;
}

const gchar *
filetea_source_get_name (FileteaSource *self)
{
  g_return_val_if_fail (FILETEA_IS_SOURCE (self), NULL);

  return self->priv->name;
}

const gchar *
filetea_source_get_content_type (FileteaSource *self)
{
  g_return_val_if_fail (FILETEA_IS_SOURCE (self), NULL);

  return self->priv->type;
}

gsize
filetea_source_get_size (FileteaSource *self)
{
  g_return_val_if_fail (FILETEA_IS_SOURCE (self), 0);

  return self->priv->size;
}

guint
filetea_source_get_flags (FileteaSource *self)
{
  g_return_val_if_fail (FILETEA_IS_SOURCE (self), 0);

  return self->priv->flags;
}

const gchar **
filetea_source_get_tags (FileteaSource *self)
{
  g_return_val_if_fail (FILETEA_IS_SOURCE (self), 0);

  return (const gchar **) self->priv->tags;
}

void
filetea_source_add_peer (FileteaSource *self, EvdPeer *peer)
{
  g_return_if_fail (FILETEA_IS_SOURCE (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  if (g_list_find (self->priv->peers, peer) == NULL)
    {
      self->priv->peers = g_list_append (self->priv->peers, peer);
      g_object_ref (peer);
    }
}

void
filetea_source_remove_peer (FileteaSource *self, EvdPeer *peer)
{
  g_return_if_fail (FILETEA_IS_SOURCE (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  if (g_list_find (self->priv->peers, peer) != NULL)
    {
      self->priv->peers = g_list_remove (self->priv->peers, peer);
      g_object_unref (peer);
    }
}
