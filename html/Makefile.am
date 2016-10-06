MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

html_sources =				\
	fileTea.js	 		\
	contentManager.js		\
	require.js			\
	utils.js			\
	main.js				\
	jquery.js			\
	privacy-policy.html		\
	delete.png			\
	download.png			\
	gradient-top.png		\
	loading.gif			\
	loading_big.gif			\
	mime-type-icon-default.png	\
	warning.png			\
					\
	download-view.css		\
	download-view.html		\
	downloadView.js			\
	index.html			\
	main.css			\
	not-found-view.html		\
	shared-files-view.css		\
	sharedFilesView.js		\
	transfers-view.css		\
	transfersView.js		\
	ux.js				\
	$(NULL)

htmldir = $(htmldatadir)/
html_DATA = $(html_sources)

EXTRA_DIST = 						\
	$(addprefix $(srcdir)/, $(html_sources))	\
	$(srcdir)/jquery-ui				\
	$(NULL)

install-data-local:
	$(INSTALL) -d "$(DESTDIR)/$(htmldir)"
	cp -r "$(srcdir)/jquery-ui" "$(DESTDIR)/$(htmldir)"

uninstall-local:
	find "$(DESTDIR)/$(htmldir)" -type d -exec chmod 755 {} \;
	rm -rf "$(DESTDIR)/$(htmldir)/jquery-ui"
