MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

sbin_PROGRAMS = \
	filetead

bin_PROGRAMS = \
	filetea

common_CFLAGS = \
	-Wall \
	$(EVD_CFLAGS) \
	$(JSON_CFLAGS) \
	-DENABLE_TESTS="\"$(enable_tests)\""

if ENABLE_DEBUG
common_CFLAGS += -Werror -g3 -O0 -ggdb
else
common_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

common_LDADD = \
	$(EVD_LIBS) \
	$(JSON_LIBS) \
	-lgcrypt

common_source_c = \
	filetea-protocol.c \
	filetea-source.c \
	filetea-transfer.c

common_source_h = \
	filetea-protocol.h \
	filetea-source.h \
	filetea-transfer.h

# FileTea server daemon
filetead_CFLAGS = $(common_CFLAGS) \
	-DHTML_DATA_DIR="\"$(htmldatadir)\""

filetead_LDADD = $(common_LDADD)

filetead_SOURCES = \
	filetead-main.c \
	$(common_source_c) \
	filetea-web-service.c \
	filetea-node.c \
	$(common_source_h) \
	filetea-web-service.h \
	filetea-node.h

# FileTea client
filetea_CFLAGS = $(common_CFLAGS)

filetea_LDADD = $(common_LDADD)

filetea_SOURCES = \
	filetea-main.c \
	$(common_source_c) \
	$(common_source_h)
