MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

sbin_PROGRAMS = filetea

filetea_CFLAGS = \
	-Wall \
	$(EVD_CFLAGS) \
	$(JSON_CFLAGS) \
	$(UUID_CFLAGS) \
	-DENABLE_TESTS="\"$(enable_tests)\"" \
	-DHTML_DATA_DIR="\"$(htmldatadir)\""

if ENABLE_DEBUG
filetea_CFLAGS += -Werror -g3 -O0 -ggdb
else
filetea_CFLAGS += -DG_DISABLE_ASSERT -DG_DISABLE_CHECKS
endif

filetea_LDADD = \
	$(EVD_LIBS) \
	$(JSON_LIBS) \
	$(UUID_LIBS) \
	-lgcrypt

source_c = \
	file-source.c \
	file-transfer.c \
	filetea-source.c \
	filetea-protocol.c \
	filetea-web-service.c \
	filetea-transfer.c \
	filetea-node.c

source_h = \
	file-source.h \
	file-transfer.h \
	filetea-source.h \
	filetea-protocol.h \
	filetea-web-service.h \
	filetea-transfer.h \
	filetea-node.h

filetea_SOURCES = \
	main.c \
	$(source_c) $(source_h)
