SUBDIRS = \
	filetea \
	html

DIST_SUBDIRS = \
	filetea \
	html

EXTRA_DIST = \
	autogen.sh \
	filetea.8 \
	filetea.conf

man8_MANS = filetea.8

configdir = $(sysconfdir)/filetea
config_DATA = filetea.conf

CLEANFILES = *~

MAINTAINERCLEANFILES = \
	Makefile.in aclocal.m4 configure config.guess config.sub \
	depcomp install-sh ltmain.sh missing mkinstalldirs config.h.in \
	stamp-h.in compile

DISTCLEANFILES = \
	cscope.files cscope.out

cscope.files:
	find src -name '*.[ch]' > $@

cscope.out: cscope.files
	cscope -b

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		 $(top_srcdir)/missing --run git log --stat ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		    echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi
