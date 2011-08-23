/*
 * main.c
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

#include <evd.h>

#include "filetea-node.h"

#define DEFAULT_HTTP_LISTEN_PORT 8080
#define DEFAULT_CONFIG_FILENAME "/etc/filetea/filetea.conf"

static EvdDaemon *evd_daemon;

static gchar *config_file = NULL;
static gboolean daemonize = FALSE;
static guint http_port = 0;
static GKeyFile *config = NULL;

static GOptionEntry entries[] =
{
  { "conf", 'c', 0, G_OPTION_ARG_STRING, &config_file, "Absolute path for the configuration file, default is '" DEFAULT_CONFIG_FILENAME "'", "filename" },
  { "daemonize", 'D', 0, G_OPTION_ARG_NONE, &daemonize, "Run service in the background", NULL },
  { "http-port", 'p', 0, G_OPTION_ARG_INT, &http_port, "Override the HTTP listening port specified in configuration file", "port" },
  { NULL }
};

static void
web_selector_on_listen (GObject      *service,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  GError *error = NULL;
  gchar *user = NULL;

  if (! evd_service_listen_finish (EVD_SERVICE (service), result, &error))
    goto quit;

  /* set 'user' as process owner, if specified */
  user = g_key_file_get_string (config, "node", "user", NULL);
  if (user != NULL)
    {
      if (! evd_daemon_set_user (evd_daemon, user, &error))
        goto quit;

      g_free (user);
    }

  if (daemonize)
    evd_daemon_daemonize (evd_daemon, NULL);

  g_debug ("Listening on port %d", http_port);

  return;

 quit:
  g_debug ("%s", error->message);
  g_error_free (error);

  evd_daemon_quit (evd_daemon, -1);
}

gint
main (gint argc, gchar *argv[])
{
  gint exit_status = 0;

  GError *error = NULL;
  GOptionContext *context = NULL;

  FileteaNode *node;
  gchar *addr;

  g_type_init ();
  evd_tls_init (NULL);

  /* main daemon */
  evd_daemon = evd_daemon_get_default (&argc, &argv);

  /* parse command line args */
  context = g_option_context_new ("- low friction file sharing service daemon");
  g_option_context_add_main_entries (context, entries, NULL);
  if (! g_option_context_parse (context, &argc, &argv, &error))
    {
      g_debug ("ERROR parsing commandline options: %s", error->message);
      g_error_free (error);

      exit_status = -1;
      goto out;
    }

  if (config_file == NULL)
    config_file = g_strdup (DEFAULT_CONFIG_FILENAME);

  /* load and parse configuration file */
  config = g_key_file_new ();
  if (! g_key_file_load_from_file (config,
                                   config_file,
                                   G_KEY_FILE_NONE,
                                   &error))
    {
      g_debug ("ERROR loading configuration: %s", error->message);
      g_error_free (error);

      exit_status = -1;
      goto out;
    }

  /* obtain HTTP listening port from config */
  if (http_port == 0)
    {
      http_port = g_key_file_get_integer (config, "http", "port", &error);
      if (http_port == 0)
        {
          if (error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND)
            {
              g_debug ("ERROR, invalid HTTP port: %s", error->message);
              g_error_free (error);

              exit_status = -1;
              goto out;
            }

          g_error_free (error);
        }
    }

  if (http_port == 0)
    http_port = DEFAULT_HTTP_LISTEN_PORT;

  /* Filetea node */
  node = filetea_node_new (config, &error);
  if (node == NULL)
    {
      g_debug ("ERROR, failed setting up service: %s", error->message);
      g_error_free (error);

      exit_status = -1;
      goto out;
    }

  addr = g_strdup_printf ("0.0.0.0:%d", http_port);
  evd_service_listen (EVD_SERVICE (node),
                      addr,
                      NULL,
                      web_selector_on_listen,
                      NULL);
  g_free (addr);

  /* start the show */
  exit_status = evd_daemon_run (evd_daemon);

  /* free stuff */
  g_object_unref (node);
  g_object_unref (evd_daemon);

 out:
  g_option_context_free (context);
  g_free (config_file);
  if (config != NULL)
    g_key_file_free (config);

  evd_tls_deinit ();

  g_print ("FileTea node terminated\n");

  return exit_status;
}
