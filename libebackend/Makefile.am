lib_LTLIBRARIES = libebackend-1.2.la

libebackend_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-DG_LOG_DOMAIN=\"e-data-server\"			\
	-DE_DATA_SERVER_EXTENSIONDIR=\"$(extensiondir)\"	\
	$(DB_CFLAGS)						\
	$(E_BACKEND_CFLAGS)

libebackend_1_2_la_SOURCES =		\
	e-data-server-module.c		\
	e-data.c			\
	e-data-view.c			\
	e-backend.c			\
	e-backend-factory.c		\
	e-backend-sexp.c		\
	e-data-factory.c		\
	e-offline-listener.c		\
	e-dbhash.c			\
	e-db3-utils.c			\
	e-file-cache.c

libebackend_1_2_la_LIBADD = 				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS)				\
	$(DB_LIBS)

libebackend_1_2_la_LDFLAGS = \
	-version-info $(LIBEBACKEND_CURRENT):$(LIBEBACKEND_REVISION):$(LIBEBACKEND_AGE) $(NO_UNDEFINED) \
	$(NULL)

libebackendincludedir = $(privincludedir)/libebackend

# FIXME: don't install these new generic headers, if possible

libebackendinclude_HEADERS =		\
	e-data-server-module.h		\
	e-data-types.h			\
	e-data.h			\
	e-data-view.h			\
	e-backend.h			\
	e-backend-factory.h		\
	e-backend-sexp.h		\
	e-data-factory.h		\
	e-offline-listener.h		\
	e-db3-utils.h			\
	e-dbhash.h			\
	e-file-cache.h

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libebackend-$(API_VERSION).pc

EXTRA_DIST = 						\
	$(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk
