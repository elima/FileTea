/*
 * file-source.h
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

#ifndef _FILE_SOURCE_H_
#define _FILE_SOURCE_H_

#include <evd.h>

typedef struct
{
  EvdPeer *peer;
  gchar *file_name;
  gchar *file_type;
  gsize file_size;
  gchar *id;
  gint ref_count;
} FileSource;

FileSource * file_source_new  (EvdPeer     *peer,
                               const gchar *id,
                               const gchar *file_name,
                               const gchar *file_type,
                               gsize        file_size);

FileSource * file_source_ref   (FileSource *self);
void         file_source_unref (FileSource *self);

#endif
