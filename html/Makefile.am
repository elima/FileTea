MAINTAINERCLEANFILES = \
	Makefile.in

htmldatadir = $(datadir)/filetea

html_sources_common =				\
	common/fileTea.js 			\
	common/contentManager.js		\
	common/require.js			\
	common/utils.js				\
	common/main.js                          \
	common/jquery.js                        \
	common/privacy-policy.html		\
	common/bitcoin.png			\
	common/delete.png			\
	common/download.png			\
	common/gradient-top.png			\
	common/html5_logo_32.png		\
	common/loading.gif			\
	common/loading_big.gif			\
	common/mime-type-icon-default.png	\
	common/warning.png			\
	$(NULL)

htmlcommondir = $(htmldatadir)/common
htmlcommon_DATA = $(html_sources_common)

html_sources_default =			\
	default/download-view.css	\
	default/download-view.html	\
	default/downloadView.js		\
	default/index.html		\
	default/main.css		\
	default/not-found-view.html	\
	default/shared-files-view.css	\
	default/sharedFilesView.js	\
	default/transfers-view.css	\
	default/transfersView.js	\
	default/ux.js			\
	$(NULL)

htmldefaultdir = $(htmldatadir)/default
htmldefault_DATA = $(html_sources_default)

EXTRA_DIST = 							\
	$(addprefix $(srcdir)/, $(html_sources_common))		\
	$(addprefix $(srcdir)/, $(html_sources_default))	\
	$(srcdir)/default/jquery-ui				\
	$(NULL)

install-data-local:
	$(INSTALL) -d "$(DESTDIR)/$(htmldefaultdir)"
	cp -r "$(srcdir)/default/jquery-ui" "$(DESTDIR)/$(htmldefaultdir)"

uninstall-local:
	find "$(DESTDIR)/$(htmldefaultdir)" -type d -exec chmod 755 {} \;
	rm -rf "$(DESTDIR)/$(htmldefaultdir)/jquery-ui"
