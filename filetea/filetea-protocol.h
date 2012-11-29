/*
 * filetea-protocol.h
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

#ifndef __FILETEA_PROTOCOL_H__
#define __FILETEA_PROTOCOL_H__

#include <evd.h>

#include <filetea-source.h>

G_BEGIN_DECLS

typedef struct _FileteaProtocol FileteaProtocol;
typedef struct _FileteaProtocolClass FileteaProtocolClass;
typedef struct _FileteaProtocolPrivate FileteaProtocolPrivate;

typedef struct
{
  void     (* register_source)   (FileteaProtocol  *self,
                                  FileteaSource   *source,
                                  gchar           **id,
                                  gchar           **signature,
                                  gpointer          user_data);
  gboolean (* unregister_source) (FileteaProtocol  *self,
                                  EvdPeer          *peer,
                                  const gchar      *id,
                                  gpointer          user_data);

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

G_END_DECLS

#endif /* __FILETEA_PROTOCOL_H__ */
