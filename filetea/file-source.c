/*
 * file-source.c
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

#include "file-source.h"

FileSource *
file_source_new (EvdPeer     *peer,
                 const gchar *id,
                 const gchar *file_name,
                 const gchar *file_type,
                 gsize        file_size,
                 GObject     *node)
{
  FileSource *self;

  g_return_val_if_fail (EVD_IS_PEER (peer), NULL);
  g_return_val_if_fail (file_name != NULL, NULL);
  g_return_val_if_fail (file_type != NULL, NULL);
  g_return_val_if_fail (file_size > 0, NULL);

  self = g_slice_new0 (FileSource);
  self->ref_count = 1;

  self->peer = peer;
  g_object_ref (peer);

  self->id = g_strdup (id);
  self->file_name = g_strdup (file_name);
  self->file_type = g_strdup (file_type);
  self->file_size = file_size;

  if (node != NULL)
    {
      self->node = node;
      g_object_ref (node);
    }

  return self;
}

static void
file_source_free (FileSource *self)
{
  g_return_if_fail (self != NULL);

  g_free (self->id);
  g_free (self->file_name);
  g_free (self->file_type);

  g_object_unref (self->peer);

  if (self->node != NULL)
    g_object_unref (self->node);

  g_slice_free (FileSource, self);
}

FileSource *
file_source_ref (FileSource *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_add (&self->ref_count, 1);

  return self;
}

void
file_source_unref (FileSource *self)
{
  gint old_ref;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  old_ref = g_atomic_int_get (&self->ref_count);
  if (old_ref > 1)
    g_atomic_int_compare_and_exchange (&self->ref_count, old_ref, old_ref - 1);
  else
    file_source_free (self);
}
