INCLUDES =					\
	-DG_LOG_DOMAIN=\"libecalbackendmapi\"	\
	$(E_DATA_SERVER_CFLAGS)			\
	$(EVOLUTION_CALENDAR_CFLAGS)		\
	$(LIBECAL_CFLAGS)			\
	$(LIBEDATACAL_CFLAGS)			\
	-I$(top_srcdir)/calendar		\
	-I$(top_srcdir)/libical/src		\
	-I$(top_srcdir)/servers/mapi		\
	$(LIBMAPI_CFLAGS)

extension_LTLIBRARIES = libecalbackendmapi.la

libecalbackendmapi_la_SOURCES =			\
	e-cal-backend-mapi-factory.c		\
	e-cal-backend-mapi-factory.h		\
	e-cal-backend-mapi.c			\
	e-cal-backend-mapi.h

libecalbackendmapi_la_LIBADD =			\
	$(E_DATA_SERVER_LIBS)			\
	$(EVOLUTION_CALENDAR_LIBS)		\
	$(LIBECAL_LIBS)				\
	$(LIBEDATACAL_LIBS)			\
	$(top_builddir)/servers/mapi/libexchangemapi-1.0.la	\
	$(LIBMAPI_LIBS)

libecalbackendmapi_la_LDFLAGS =			\
	-module -avoid-version $(NO_UNDEFINED)

