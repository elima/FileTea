all: filetea

filetea: \
	Makefile \
	main.c \
	file-source.c \
	file-source.h \
	file-transfer.c \
	file-transfer.h
	gcc -ggdb -O0 \
		`pkg-config --libs --cflags evd-0.1 json-glib-1.0` \
		-o filetea \
		main.c \
		file-source.c \
		file-transfer.c

