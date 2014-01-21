/*
 * filetea-transfer.h
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

#ifndef _FILETEA_TRANSFER_H_
#define _FILETEA_TRANSFER_H_

#include <gio/gio.h>
#include <evd.h>

#include "filetea-source.h"

G_BEGIN_DECLS

typedef struct _FileteaTransfer FileteaTransfer;
typedef struct _FileteaTransferClass FileteaTransferClass;
typedef struct _FileteaTransferPrivate FileteaTransferPrivate;

struct _FileteaTransfer
{
  GObject parent;

  FileteaTransferPrivate *priv;
};

struct _FileteaTransferClass
{
  GObjectClass parent_class;
};

typedef struct _FileteaTransfer FileteaTransfer;

typedef enum
{
  FILETEA_TRANSFER_STATUS_NOT_STARTED,
  FILETEA_TRANSFER_STATUS_ACTIVE,
  FILETEA_TRANSFER_STATUS_PAUSED,
  FILETEA_TRANSFER_STATUS_COMPLETED,
  FILETEA_TRANSFER_STATUS_SOURCE_ABORTED,
  FILETEA_TRANSFER_STATUS_TARGET_ABORTED,
  FILETEA_TRANSFER_STATUS_ERROR,
} FileteaTransferStatus;

#define FILETEA_TYPE_TRANSFER           (filetea_transfer_get_type ())
#define FILETEA_TRANSFER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), FILETEA_TYPE_TRANSFER, FileteaTransfer))
#define FILETEA_TRANSFER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), FILETEA_TYPE_TRANSFER, FileteaTransferClass))
#define FILETEA_IS_TRANSFER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FILETEA_TYPE_TRANSFER))
#define FILETEA_IS_TRANSFER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), FILETEA_TYPE_TRANSFER))
#define FILETEA_TRANSFER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FILETEA_TYPE_TRANSFER, FileteaTransferClass))

GType             filetea_transfer_get_type              (void) G_GNUC_CONST;

FileteaTransfer * filetea_transfer_new                   (FileteaSource       *source,
                                                          EvdWebService       *web_service,
                                                          EvdHttpConnection   *target_conn,
                                                          const gchar         *action,
                                                          gboolean             is_chunked,
                                                          SoupRange           *range,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);

const gchar *     filetea_transfer_get_id                (FileteaTransfer *self);


void              filetea_transfer_start                 (FileteaTransfer *self);
gboolean          filetea_transfer_finish                (FileteaTransfer  *self,
                                                          GAsyncResult     *result,
                                                          GError          **error);

void              filetea_transfer_set_source_conn       (FileteaTransfer   *self,
                                                          EvdHttpConnection *conn);

void              filetea_transfer_set_target_peer       (FileteaTransfer *self,
                                                          EvdPeer         *peer);

void              filetea_transfer_get_status            (FileteaTransfer *self,
                                                          guint           *status,
                                                          gsize           *transferred,
                                                          gdouble         *bandwidth);

void              filetea_transfer_cancel                (FileteaTransfer *self);

#endif /* _FILETEA_TRANSFER_H_ */
