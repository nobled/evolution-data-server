INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	-I$(top_srcdir)/libdb/dist			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

extension_LTLIBRARIES = libebookbackendfile.la

libebookbackendfile_la_SOURCES =			\
	e-book-backend-file.c				\
	e-book-backend-file.h				\
	e-book-backend-file-factory.c

libebookbackendfile_la_LIBADD =						\
	$(top_builddir)/addressbook/libedata-book/libedata-book-$(API_VERSION).la	\
	$(top_builddir)/libedataserver/libedataserver-$(API_VERSION).la

libebookbackendfile_la_LDFLAGS =	\
	-module -avoid-version
