/*
 * filetea-source.h
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

#ifndef __FILETEA_SOURCE_H__
#define __FILETEA_SOURCE_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _FileteaSource FileteaSource;
typedef struct _FileteaSourceClass FileteaSourceClass;
typedef struct _FileteaSourcePrivate FileteaSourcePrivate;

typedef enum {
  FILETEA_SOURCE_FLAGS_NONE          = 0,
  FILETEA_SOURCE_FLAGS_PUBLIC        = 1 << 0,
  FILETEA_SOURCE_FLAGS_LIVE          = 1 << 1,
  FILETEA_SOURCE_FLAGS_REAL_TIME     = 1 << 2,
  FILETEA_SOURCE_FLAGS_CHUNKABLE     = 1 << 3,
  FILETEA_SOURCE_FLAGS_BIDIRECTIONAL = 1 << 4
} FileteaSourceFlags;

struct _FileteaSource
{
  EvdWebService parent;

  FileteaSourcePrivate *priv;
};

struct _FileteaSourceClass
{
  EvdWebServiceClass parent_class;
};

#define FILETEA_SOURCE_TYPE           (filetea_source_get_type ())
#define FILETEA_SOURCE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), FILETEA_SOURCE_TYPE, FileteaSource))
#define FILETEA_SOURCE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), FILETEA_SOURCE_TYPE, FileteaSourceClass))
#define FILETEA_IS_SOURCE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FILETEA_SOURCE_TYPE))
#define FILETEA_IS_SOURCE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), FILETEA_SOURCE_TYPE))
#define FILETEA_SOURCE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FILETEA_SOURCE_TYPE, FileteaSourceClass))


GType             filetea_source_get_type                (void) G_GNUC_CONST;

FileteaSource *   filetea_source_new                     (EvdPeer      *peer,
                                                          const gchar  *name,
                                                          const gchar  *type,
                                                          gsize         size,
                                                          guint         flags,
                                                          const gchar **tags);

void              filetea_source_set_peer                (FileteaSource *self,
                                                          EvdPeer       *peer);
EvdPeer *         filetea_source_get_peer                (FileteaSource *self);

const gchar *     filetea_source_get_name                (FileteaSource  *self);
const gchar *     filetea_source_get_content_type        (FileteaSource  *self);
void              filetea_source_set_size                (FileteaSource *self,
                                                          gsize          size);
gsize             filetea_source_get_size                (FileteaSource  *self);
guint             filetea_source_get_flags               (FileteaSource  *self);
const gchar **    filetea_source_get_tags                (FileteaSource  *self);

void              filetea_source_set_id                  (FileteaSource *self,
                                                          const gchar   *id);
const gchar *     filetea_source_get_id                  (FileteaSource *self);

void              filetea_source_set_signature           (FileteaSource *self,
                                                          const gchar   *signature);
const gchar *     filetea_source_get_signature           (FileteaSource *self);

gboolean          filetea_source_is_chunkable            (FileteaSource *self);

GCancellable *    filetea_source_get_cancellable         (FileteaSource *self);

void              filetea_source_take_error              (FileteaSource *self,
                                                          GError        *error);
GError *          filetea_source_get_error               (FileteaSource *self);

G_END_DECLS

#endif /* __FILETEA_SOURCE_H__ */
