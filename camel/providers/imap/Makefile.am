camel_provider_LTLIBRARIES = libcamelimap.la
camel_provider_DATA = libcamelimap.urls

libcamelimap_la_CPPFLAGS = \
	$(AM_CPPFLAGS)				\
	-I.. 					\
	-I$(srcdir)/..				\
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	$(GNOME_INCLUDEDIR)			\
	$(GTK_INCLUDEDIR)			\
	-DG_LOG_DOMAIN=\"camel-imap-provider\"

libcamelimap_la_SOURCES = 			\
	camel-imap-command.c			\
	camel-imap-folder.c			\
	camel-imap-message-cache.c		\
	camel-imap-provider.c			\
	camel-imap-search.c			\
	camel-imap-store.c			\
	camel-imap-store-summary.c		\
	camel-imap-summary.c			\
	camel-imap-journal.c			\
	camel-imap-utils.c			\
	camel-imap-wrapper.c

noinst_HEADERS =			\
	camel-imap-command.h			\
	camel-imap-folder.h			\
	camel-imap-message-cache.h		\
	camel-imap-search.h			\
	camel-imap-store.h			\
	camel-imap-store-summary.h		\
	camel-imap-summary.h			\
	camel-imap-journal.h			\
	camel-imap-utils.h			\
	camel-imap-wrapper.h                    \
	camel-imap-private.h

libcamelimap_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED)

libcamelimap_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la \
	$(top_builddir)/camel/libcamel-provider-1.2.la			\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(CAMEL_LIBS)

EXTRA_DIST = libcamelimap.urls

-include $(top_srcdir)/git.mk
