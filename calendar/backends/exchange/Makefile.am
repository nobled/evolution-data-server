INCLUDES =                                                      \
	-DG_LOG_DOMAIN=\"libecalbackendexchange\"	\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)/calendar			\
	-I$(top_srcdir)/calendar/backends/file		\
	-I$(top_builddir)/calendar/backends/file	\
	-I$(top_srcdir)/calendar/libical/src		\
	-I$(top_builddir)/calendar/libical/src		\
	-I$(top_srcdir)/calendar/libical/src/libical	\
	-I$(top_builddir)/calendar/libical/src/libical	\
	-I$(top_srcdir)/servers/exchange		\
	-I$(top_builddir)/servers/exchange		\
	-I$(top_builddir)/servers/exchange/lib		\
	-I$(top_builddir)/servers/exchange/xntlm		\
	-I$(top_builddir)/servers/exchange/storage		\
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(LDAP_CFLAGS)					\
	$(E_DATA_SERVER_UI_CFLAGS)			\
	$(SOUP_CFLAGS)					\
	$(DEBUG_CFLAGS)

extension_LTLIBRARIES =						\
        libecalbackendexchange.la

libecalbackendexchange_la_SOURCES = 				\
	e-cal-backend-exchange.c				\
	e-cal-backend-exchange.h				\
	e-cal-backend-exchange-calendar.c			\
	e-cal-backend-exchange-calendar.h			\
	e-cal-backend-exchange-tasks.c				\
	e-cal-backend-exchange-tasks.h				\
	e-cal-backend-exchange-factory.c			\
	e-cal-backend-exchange-factory.h			\
	e2k-cal-query.c						\
	e2k-cal-query.h						\
	e2k-cal-utils.c						\
	e2k-cal-utils.h

libecalbackendexchange_la_LIBADD =				\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la \
	$(top_builddir)/libedataserverui/libedataserverui-1.2.la \
	$(top_builddir)/servers/exchange/storage/libexchange-storage-1.2.la	\
	$(SOUP_LIBS)

libecalbackendexchange_la_LDFLAGS =				\
	$(top_builddir)/libedataserverui/libedataserverui-1.2.la \
	-module -avoid-version
