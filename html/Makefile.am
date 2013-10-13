MAINTAINERCLEANFILES = \
	Makefile.in

EXTRA_DIST = default common

htmldatadir = $(datadir)/filetea

install-data-local:
	$(INSTALL) -d "$(DESTDIR)/$(htmldatadir)"
	cp -r "$(srcdir)/default" "$(DESTDIR)/$(htmldatadir)"
	cp -r "$(srcdir)/common" "$(DESTDIR)/$(htmldatadir)"
	find "$(DESTDIR)/$(htmldatadir)" -type d -exec chmod 755 {} \;
	find "$(DESTDIR)/$(htmldatadir)" -type f -exec chmod 644 {} \;

uninstall-local:
	rm -rf "$(DESTDIR)/$(htmldatadir)"
