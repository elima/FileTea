/*
 * filetea-protocol.h
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

#ifndef __FILETEA_PROTOCOL_H__
#define __FILETEA_PROTOCOL_H__

#include <evd.h>

#include "filetea-source.h"
#include "filetea-transfer.h"

G_BEGIN_DECLS

typedef struct _FileteaProtocol FileteaProtocol;
typedef struct _FileteaProtocolClass FileteaProtocolClass;
typedef struct _FileteaProtocolPrivate FileteaProtocolPrivate;

typedef struct
{
  gboolean (* register_source)   (FileteaProtocol  *self,
                                  EvdPeer          *peer,
                                  FileteaSource    *source,
                                  GError          **error,
                                  gpointer          user_data);
  gboolean (* unregister_source) (FileteaProtocol  *self,
                                  EvdPeer          *peer,
                                  const gchar      *id,
                                  gboolean          gracefully,
                                  gpointer          user_data);

  void     (* content_request)   (FileteaProtocol    *self,
                                  FileteaSource      *source,
                                  EvdHttpConnection  *conn,
                                  const gchar        *action,
                                  const gchar        *peer_id,
                                  gboolean            is_chunked,
                                  SoupRange          *byte_range,
                                  gpointer            user_data);
  void     (* content_push)      (FileteaProtocol    *self,
                                  FileteaTransfer    *transfer,
                                  EvdHttpConnection  *conn,
                                  gpointer            user_data);

  void     (* seeder_push_request) (FileteaProtocol *self,
                                    GAsyncResult    *result,
                                    const gchar     *source_id,
                                    const gchar     *transfer_id,
                                    gboolean         is_chunked,
                                    SoupRange       *byte_range,
                                    gpointer         user_data);

} FileteaProtocolVTable;

struct _FileteaProtocol
{
  GObject parent;

  FileteaProtocolPrivate *priv;
};

struct _FileteaProtocolClass
{
  GObjectClass parent_class;
};

#define FILETEA_TYPE_PROTOCOL           (filetea_protocol_get_type ())
#define FILETEA_PROTOCOL(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), FILETEA_TYPE_PROTOCOL, FileteaProtocol))
#define FILETEA_PROTOCOL_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), FILETEA_TYPE_PROTOCOL, FileteaProtocolClass))
#define FILETEA_IS_PROTOCOL(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FILETEA_TYPE_PROTOCOL))
#define FILETEA_IS_PROTOCOL_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), FILETEA_TYPE_PROTOCOL))
#define FILETEA_PROTOCOL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FILETEA_TYPE_PROTOCOL, FileteaProtocolClass))


GType             filetea_protocol_get_type                (void) G_GNUC_CONST;

FileteaProtocol * filetea_protocol_new                     (FileteaProtocolVTable *vtable,
                                                            gpointer               user_data,
                                                            GDestroyNotify         user_data_free_func);

EvdJsonrpc *      filetea_protocol_get_rpc                 (FileteaProtocol *self);

gboolean          filetea_protocol_handle_content_request  (FileteaProtocol    *self,
                                                            FileteaSource      *source,
                                                            EvdWebService      *web_service,
                                                            EvdHttpConnection  *conn,
                                                            EvdHttpRequest     *request,
                                                            GError            **error);
gboolean          filetea_protocol_handle_content_push     (FileteaProtocol    *self,
                                                            FileteaTransfer    *transfer,
                                                            EvdWebService      *web_service,
                                                            EvdHttpConnection  *conn,
                                                            EvdHttpRequest     *request,
                                                            GError            **error);

gboolean          filetea_protocol_request_content         (FileteaProtocol  *self,
                                                            EvdPeer          *peer,
                                                            const gchar      *source_id,
                                                            const gchar      *transfer_id,
                                                            gboolean          is_chunked,
                                                            SoupRange        *byte_range,
                                                            GError          **error);

G_END_DECLS

#endif /* __FILETEA_PROTOCOL_H__ */
