MAINTAINERCLEANFILES = \
	Makefile.in

tests_dir = `pwd`/
src_dir = $(top_srcdir)/filetea
htmldatadir = $(datadir)/filetea

AM_CFLAGS = \
	-Wall \
	$(EVD_CFLAGS) \
	$(JSON_CFLAGS) \
	-DENABLE_TESTS="\"$(enable_tests)\"" \
	-DTESTS_DIR="\"$(tests_dir)\"" \
	-DHTML_DATA_DIR="\"$(htmldatadir)\"" \
	-I$(top_srcdir)/filetea

if ENABLE_DEBUG
AM_CFLAGS += -Werror -g3 -O0 -ggdb
else
AM_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

AM_LIBS = \
	$(EVD_LIBS) \
	$(JSON_LIBS)

if ENABLE_TESTS

noinst_PROGRAMS = \
	test-protocol \
	test-node-sources

TESTS = \
	test-protocol \
	test-node-sources

# test-protocol
test_protocol_CFLAGS = $(AM_CFLAGS)
test_protocol_LDADD = $(AM_LIBS)
test_protocol_SOURCES = \
	../filetea/filetea-source.c \
	../filetea/filetea-transfer.c \
	../filetea/filetea-protocol.c \
	test-protocol.c

# test-node-sources
test_node_sources_CFLAGS = $(AM_CFLAGS)
test_node_sources_LDADD = $(AM_LIBS)
test_node_sources_SOURCES = \
	$(src_dir)/filetea-source.c \
	$(src_dir)/filetea-protocol.c \
	$(src_dir)/filetea-web-service.c \
	$(src_dir)/filetea-transfer.c \
	$(src_dir)/filetea-node.c \
	test-node-sources.c

endif # ENABLE_TESTS

EXTRA_DIST =
