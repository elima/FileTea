#include "filetea-node.h"

typedef struct
{
  gchar *test_name;
  gchar *out_msg;
  gint num_sources;
} TestCase;

static TestCase test_cases[] =
  {
    {
      "register/ok",
      "{"
      "  \"method\": \"register\","
      "  \"id\": 5,"
      "  \"params\": [ {"
      "    \"name\": \"Some content\","
      "    \"type\": \"text/plain\","
      "    \"size\": 123,"
      "    \"flags\": 7,"
      "    \"tags\": [\"trip\", \"outer\", \"space\"]"
      "  } ]"
      "}",
      1
    },

    {
      "register/error/not-an-object",
      "{"
      "  \"method\": \"register\","
      "  \"id\": 5,"
      "  \"params\": [0]"
      "}",
      0
    },

    {
      "register/ok/multiple",
      "{"
      "  \"method\": \"register\","
      "  \"id\": 5,"
      "  \"params\": [ {"
      "    \"name\": \"Some content\","
      "    \"type\": \"text/plain\","
      "    \"size\": 123,"
      "    \"flags\": 7,"
      "    \"tags\": [\"trip\", \"outer\", \"space\"]"
      "  },"
      "  {"
      "    \"name\": \"Some content\","
      "    \"type\": \"text/plain\","
      "    \"size\": 123,"
      "    \"flags\": 7,"
      "    \"tags\": [\"trip\", \"outer\", \"space\"]"
      "  } ]"
      "}",
      2
    }
  };

typedef struct
{
  FileteaNode *node;
  EvdPeer *peer1;
  EvdPeer *peer2;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
  EvdWebTransportServer *transport;
  GError *error = NULL;
  GKeyFile *config;

  config = g_key_file_new ();
  g_key_file_load_from_file (config,
                             TESTS_DIR "/test-node-sources.conf",
                             G_KEY_FILE_NONE,
                             &error);
  g_assert_no_error (error);

  f->node = filetea_node_new (config, &error);
  g_assert_no_error (error);

  g_key_file_unref (config);

  transport = evd_web_transport_server_new (NULL);
  f->peer1 = g_object_new (EVD_TYPE_PEER,
                           "transport", transport,
                           NULL);
  f->peer2 = g_object_new (EVD_TYPE_PEER,
                           "transport", transport,
                           NULL);
  g_object_unref (transport);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
  g_assert (G_OBJECT (f->node)->ref_count == 1);
  g_object_unref (f->node);

  g_object_unref (f->peer1);
  g_object_unref (f->peer2);
}

static void
test_func (Fixture       *f,
           gconstpointer  data)
{
  TestCase *test_case = (TestCase *) data;

  FileteaProtocol *protocol;
  EvdJsonrpc *rpc;

  GError *error = NULL;

  GList *sources;

  protocol = filetea_node_get_protocol (f->node);
  rpc = filetea_protocol_get_rpc (protocol);

  evd_jsonrpc_transport_receive (rpc, test_case->out_msg, f->peer1, 1, &error);
  g_assert_no_error (error);

  sources = filetea_node_get_all_sources (f->node);
  g_assert_cmpuint (g_list_length (sources), ==, test_case->num_sources);

  g_list_free (sources);
}

gint
main (gint argc, gchar *argv[])
{
  gint i;

#ifndef GLIB_VERSION_2_36
  g_type_init ();
#endif

  g_test_init (&argc, &argv, NULL);

  for (i=0; i<sizeof (test_cases) / sizeof (TestCase); i++)
    {
      gchar *test_path;

      test_path = g_strdup_printf ("/node/sources/%s", test_cases[i].test_name);

      g_test_add (test_path,
                  Fixture,
                  &test_cases[i],
                  fixture_setup,
                  test_func,
                  fixture_teardown);

      g_free (test_path);
    }

  return g_test_run ();
}
