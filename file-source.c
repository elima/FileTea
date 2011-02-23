#include "file-source.h"

FileSource *
file_source_new (EvdPeer     *peer,
                 const gchar *file_name,
                 const gchar *file_type,
                 gsize        file_size)
{
  FileSource *self;
  gchar *st;
  gchar *uuid;

  g_return_val_if_fail (EVD_IS_PEER (peer), NULL);
  g_return_val_if_fail (file_name != NULL, NULL);
  g_return_val_if_fail (file_type != NULL, NULL);
  g_return_val_if_fail (file_size > 0, NULL);

  self = g_slice_new0 (FileSource);
  self->ref_count = 1;

  self->peer = peer;
  g_object_ref (peer);

  self->file_name = g_strdup (file_name);
  self->file_type = g_strdup (file_type);
  self->file_size = file_size;

  uuid = evd_uuid_new ();
  st = g_strdup_printf ("%s%s",
                        file_name,
                        uuid);
  g_free (uuid);

  self->id = g_compute_checksum_for_string (G_CHECKSUM_SHA1, st, -1);
  g_free (st);

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

  g_slice_free (FileSource, self);
}

FileSource *
file_source_ref (FileSource *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_exchange_and_add (&self->ref_count, 1);

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
