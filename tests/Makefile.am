MAINTAINERCLEANFILES = \
	Makefile.in

tests_dir = `pwd`/

AM_CFLAGS = \
	-Wall \
	$(EVD_CFLAGS) \
	$(JSON_CFLAGS) \
	-DTESTS_DIR="\"$(tests_dir)\"" \
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
	test-protocol

TESTS = \
	test-protocol

# test-protocol
test_protocol_CFLAGS = $(AM_CFLAGS)
test_protocol_LDADD = $(AM_LIBS)
test_protocol_SOURCES = \
	../filetea/filetea-source.c \
	../filetea/filetea-protocol.c \
	test-protocol.c

endif # ENABLE_TESTS

EXTRA_DIST =
