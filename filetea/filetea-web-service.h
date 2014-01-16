/*
 * filetea-web-service.h
 *
 * FileTea, low-friction file sharing <http://filetea.net>
 *
 * Copyright (C) 2013-2014, Igalia S.L.
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

#ifndef __FILETEA_WEB_SERVICE_H__
#define __FILETEA_WEB_SERVICE_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _FileteaWebService FileteaWebService;
typedef struct _FileteaWebServiceClass FileteaWebServiceClass;
typedef struct _FileteaWebServicePrivate FileteaWebServicePrivate;

struct _FileteaWebService
{
  EvdWebService parent;

  FileteaWebServicePrivate *priv;
};

struct _FileteaWebServiceClass
{
  EvdWebServiceClass parent_class;
};

typedef void (* FileteaWebServiceContentRequestCb) (FileteaWebService *self,
                                                    const gchar       *content_id,
                                                    EvdHttpConnection *conn,
                                                    EvdHttpRequest    *request,
                                                    gpointer           user_data);

#define FILETEA_TYPE_WEB_SERVICE           (filetea_web_service_get_type ())
#define FILETEA_WEB_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), FILETEA_TYPE_WEB_SERVICE, FileteaWebService))
#define FILETEA_WEB_SERVICE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), FILETEA_TYPE_WEB_SERVICE, FileteaWebServiceClass))
#define FILETEA_IS_WEB_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), FILETEA_TYPE_WEB_SERVICE))
#define FILETEA_IS_WEB_SERVICE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), FILETEA_TYPE_WEB_SERVICE))
#define FILETEA_WEB_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), FILETEA_TYPE_WEB_SERVICE, FileteaWebServiceClass))


GType               filetea_web_service_get_type                (void) G_GNUC_CONST;

FileteaWebService * filetea_web_service_new                     (GKeyFile                           *config,
                                                                 FileteaWebServiceContentRequestCb   content_req_cb,
                                                                 gpointer                            user_data,
                                                                 GError                            **error);

EvdTransport *      filetea_web_service_get_transport           (FileteaWebService *self);

#ifdef ENABLE_TESTS

#endif /* ENABLE_TESTS */

G_END_DECLS

#endif /* __FILETEA_WEB_SERVICE_H__ */
