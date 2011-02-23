#ifndef _FILE_TRANSFER_H_
#define _FILE_TRANSFER_H_

#include <gio/gio.h>
#include <evd.h>

#include "file-source.h"

typedef struct _FileTransfer FileTransfer;

typedef void (* FileTransferReportCb) (FileTransfer *self,
                                       gdouble       perc_completed,
                                       gdouble       bandwidth,
                                       gpointer      user_data);

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
  GSimpleAsyncResult *result;
  gint ref_count;

  guint report_interval;
  time_t report_last_time;
  FileTransferReportCb report_cb;
  gpointer report_cb_user_data;
};

FileTransfer * file_transfer_new          (FileSource          *source,
                                           EvdHttpConnection   *conn,
                                           GAsyncReadyCallback  callback,
                                           gpointer             user_data);

FileTransfer * file_transfer_ref          (FileTransfer *self);
void           file_transfer_unref        (FileTransfer *self);

void           file_transfer_start        (FileTransfer *self,
                                           gboolean      download);
gboolean       file_transfer_finish       (FileTransfer  *self,
                                           GAsyncResult  *result,
                                           GError       **error);

#endif
