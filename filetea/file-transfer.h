/*
 * file-transfer.h
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

#ifndef _FILE_TRANSFER_H_
#define _FILE_TRANSFER_H_

#include <gio/gio.h>
#include <evd.h>

#include "file-source.h"

typedef struct _FileTransfer FileTransfer;

struct _FileTransfer
{
  gchar *id;
  FileSource *source;
  EvdHttpConnection *source_conn;
  EvdHttpConnection *target_conn;
  EvdPeer *target_peer;
  guint status;
  gchar *buf;
  gsize transferred;
  gdouble bandwidth;
  GSimpleAsyncResult *result;
  gint ref_count;
  gboolean download;
};

typedef enum
{
  FILE_TRANSFER_STATUS_NOT_STARTED,
  FILE_TRANSFER_STATUS_ACTIVE,
  FILE_TRANSFER_STATUS_PAUSED,
  FILE_TRANSFER_STATUS_COMPLETED,
  FILE_TRANSFER_STATUS_SOURCE_ABORTED,
  FILE_TRANSFER_STATUS_TARGET_ABORTED
} FileTransferStatus;

FileTransfer * file_transfer_new          (const gchar         *id,
                                           FileSource          *source,
                                           EvdHttpConnection   *conn,
                                           gboolean             download,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);

FileTransfer * file_transfer_ref          (FileTransfer *self);
void           file_transfer_unref        (FileTransfer *self);

void           file_transfer_start        (FileTransfer *self);
gboolean       file_transfer_finish       (FileTransfer  *self,
                                           GAsyncResult  *result,
                                           GError       **error);

void           file_transfer_set_source_conn (FileTransfer      *self,
                                              EvdHttpConnection *conn);

void           file_transfer_set_target_peer (FileTransfer *self,
                                              EvdPeer      *peer);

void           file_transfer_get_status      (FileTransfer *self,
                                              guint        *status,
                                              gsize        *transferred,
                                              gdouble      *bandwidth);

#endif
