INCLUDES =					\
	-DG_LOG_DOMAIN=\"libecalbackendmapi\"	\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	-I$(top_srcdir)/calendar		\
	-I$(top_builddir)/calendar		\
	-I$(top_srcdir)/libical/src		\
	-I$(top_builddir)/libical/src		\
	-I$(top_srcdir)/libical/src/libical	\
	-I$(top_builddir)/libical/src/libical	\
	-I$(top_srcdir)/servers/mapi		\
	-I$(top_builddir)/servers/mapi		\
	$(E_DATA_SERVER_CFLAGS)			\
	$(LIBEBACKEND_CFLAGS)			\
	$(EVOLUTION_CALENDAR_CFLAGS)		\
	$(LIBECAL_CFLAGS)			\
	$(LIBEDATACAL_CFLAGS)			\
	$(LIBMAPI_CFLAGS)

extension_LTLIBRARIES = libecalbackendmapi.la

libecalbackendmapi_la_SOURCES =			\
	e-cal-backend-mapi-factory.c		\
	e-cal-backend-mapi-factory.h		\
	e-cal-backend-mapi.c			\
	e-cal-backend-mapi.h

libecalbackendmapi_la_LIBADD =					\
	$(top_builddir)/servers/mapi/libexchangemapi-1.0.la	\
	$(E_DATA_SERVER_LIBS)					\
	$(LIBEBACKEND_LIBS)					\
	$(EVOLUTION_CALENDAR_LIBS)				\
	$(LIBECAL_LIBS)						\
	$(LIBEDATACAL_LIBS)					\
	$(LIBMAPI_LIBS)

libecalbackendmapi_la_LDFLAGS =			\
	-module -avoid-version $(NO_UNDEFINED)

