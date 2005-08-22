## Process this file with automake to produce Makefile.in

camel_provider_LTLIBRARIES = libcamellocal.la
camel_provider_DATA = libcamellocal.urls

INCLUDES = \
	-I$(top_srcdir)/camel			\
	-I$(top_srcdir)/libdb/dist		\
	-I$(top_srcdir)/intl			\
	-I$(top_srcdir)				\
	$(CAMEL_CFLAGS)				\
	-DG_LOG_DOMAIN=\"camel-local-provider\"

libcamellocal_la_SOURCES = 			\
	camel-local-folder.c			\
	camel-local-store.c			\
	camel-local-summary.c			\
	camel-local-provider.c			\
	camel-maildir-folder.c			\
	camel-maildir-store.c			\
	camel-maildir-summary.c			\
	camel-mbox-folder.c			\
	camel-mbox-store.c			\
	camel-mbox-summary.c			\
	camel-mbox-view-summary.c		\
	camel-spool-folder.c			\
	camel-spool-store.c			\
	camel-spool-summary.c

#	camel-mh-folder.c			\
#	camel-mh-store.c			\
#	camel-mh-summary.c

noinst_HEADERS =				\
	camel-local-folder.h			\
	camel-local-store.h			\
	camel-local-summary.h			\
	camel-maildir-folder.h			\
	camel-maildir-store.h			\
	camel-maildir-summary.h			\
	camel-mbox-folder.h			\
	camel-mbox-store.h			\
	camel-mbox-summary.h			\
	camel-mbox-view-summary.h		\
	camel-spool-folder.h			\
	camel-spool-store.h			\
	camel-spool-summary.h			\
	camel-local-private.h

#	camel-mh-folder.h			\
#	camel-mh-store.h			\
#	camel-mh-summary.h

libcamellocal_la_LDFLAGS = -avoid-version -module

libcamellocal_la_LIBADD = \
	$(top_builddir)/libedataserver/libedataserver-${API_VERSION}.la

EXTRA_DIST = libcamellocal.urls
