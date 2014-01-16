MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

sbin_PROGRAMS = \
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
	$(UUID_LIBS) \
	-lgcrypt

# FileTea server daemon
filetea_CFLAGS = $(common_CFLAGS) \
	-DHTML_DATA_DIR="\"$(htmldatadir)\""

filetea_LDADD = $(common_LDADD)

source_c = \
	filetea-source.c \
	filetea-protocol.c \
	filetea-web-service.c \
	filetea-transfer.c \
	filetea-node.c

source_h = \
	filetea-source.h \
	filetea-protocol.h \
	filetea-web-service.h \
	filetea-transfer.h \
	filetea-node.h

filetea_SOURCES = \
	main.c \
	$(source_c) $(source_h)
