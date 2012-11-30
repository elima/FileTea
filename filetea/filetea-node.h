/*
 * filetea-node.h
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2011-2012, Igalia S.L.
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

#ifndef __FILETEA_NODE_H__
#define __FILETEA_NODE_H__

#include <evd.h>

#include <filetea-protocol.h>

G_BEGIN_DECLS

typedef struct _FileteaNode FileteaNode;
typedef struct _FileteaNodeClass FileteaNodeClass;
typedef struct _FileteaNodePrivate FileteaNodePrivate;

struct _FileteaNode
{
  EvdWebService parent;

  FileteaNodePrivate *priv;
};

struct _FileteaNodeClass
{
  EvdWebServiceClass parent_class;

  /* virtual methods */

  /* signal prototypes */
};

#define FILETEA_NODE_TYPE           (filetea_node_get_type ())
#define FILETEA_NODE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), FILETEA_NODE_TYPE, FileteaNode))
#define FILETEA_NODE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), FILETEA_NODE_TYPE, FileteaNodeClass))
#define FILETEA_IS_NODE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FILETEA_NODE_TYPE))
#define FILETEA_IS_NODE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), FILETEA_NODE_TYPE))
#define FILETEA_NODE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FILETEA_NODE_TYPE, FileteaNodeClass))


GType             filetea_node_get_type                (void) G_GNUC_CONST;

FileteaNode *     filetea_node_new                     (GKeyFile  *config,
                                                        GError   **error);

const gchar *     filetea_node_get_id                  (FileteaNode  *self);

#ifdef ENABLE_TESTS

FileteaProtocol * filetea_node_get_protocol            (FileteaNode *self);

#endif /* ENABLE_TESTS */

G_END_DECLS

#endif /* __FILETEA_NODE_H__ */
