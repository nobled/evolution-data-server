INCLUDES =						\
	-DG_LOG_DOMAIN=\"libecalbackendcaldav\"		\
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
	$(EVOLUTION_CALENDAR_CFLAGS)			\
	$(SOUP_CFLAGS)

noinst_PROGRAMS = create-account

create_account_SOURCES = create-account.c
create_account_LDADD = 						\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(SOUP_LIBS)						\
	$(E_DATA_SERVER_LIBS)

extension_LTLIBRARIES = libecalbackendcaldav.la

libecalbackendcaldav_la_SOURCES =	\
	e-cal-backend-caldav-factory.c	\
	e-cal-backend-caldav-factory.h	\
	e-cal-backend-caldav.c		\
	e-cal-backend-caldav.h		

libecalbackendcaldav_la_LIBADD =						\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la		\
	$(SOUP_LIBS)

libecalbackendcaldav_la_LDFLAGS =		\
	-module -avoid-version
