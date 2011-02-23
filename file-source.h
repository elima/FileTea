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
                               const gchar *file_name,
                               const gchar *file_type,
                               gsize        file_size);

FileSource * file_source_ref   (FileSource *self);
void         file_source_unref (FileSource *self);

#endif
