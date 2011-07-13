MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

bin_PROGRAMS = filetea

filetea_CFLAGS = \
	-Wall -Werror -g3 -O0 -ggdb \
	$(EVD_CFLAGS) \
	$(JSON_CFLAGS) \
	-DENABLE_TESTS="\"$(enable_tests)\"" \
	-DHTML_DATA_DIR="\"$(htmldatadir)\""

filetea_LDADD = \
	$(EVD_LIBS) \
	$(JSON_LIBS)

source_c = \
	file-source.c \
	file-transfer.c \
	filetea-node.c

source_h = \
	file-source.h \
	file-transfer.h \
	filetea-node.h

filetea_SOURCES = \
	main.c \
	$(source_c)
