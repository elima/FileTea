MAINTAINERCLEANFILES = \
	Makefile.in

EXTRA_DIST = default common

htmldatadir = $(datadir)/filetea

install: default
	$(INSTALL) -d $(DESTDIR)/$(htmldatadir)
	cp -r ./default/ $(DESTDIR)/$(htmldatadir)
	cp -r ./common/ $(DESTDIR)/$(htmldatadir)
