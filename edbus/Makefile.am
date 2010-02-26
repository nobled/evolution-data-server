NULL =

SUBDIRS=. tests

AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"GLib-DBus-Standalone\"			\
	-I$(top_builddir)					\
	-I$(top_srcdir)						\
	-DG_DISABLE_DEPRECATED					\
	-DE_DBUS_COMPILATION					\
	-DE_DBUS_I_UNDERSTAND_THAT_ABI_AND_API_IS_UNSTABLE	\
	$(NULL)

lib_LTLIBRARIES = libedbus.la

marshal_sources = \
        edbus-marshal.h	\
        edbus-marshal.c	\
	$(NULL)

glib_genmarshal=glib-genmarshal

edbus-marshal.h: edbus-marshal.list
	$(glib_genmarshal) --prefix=_edbus_marshal $(srcdir)/edbus-marshal.list --header --internal > $@.tmp && \
	  mv $@.tmp $@

edbus-marshal.c: edbus-marshal.h edbus-marshal.list
	(echo "#include \"edbus-marshal.h\""; \
	$(glib_genmarshal) --prefix=_edbus_marshal $(srcdir)/edbus-marshal.list --body --internal) > $@.tmp && \
	  mv $@.tmp $@

evariant_sources =			\
	ebitlock.c			\
	evarianttype.c			\
	evariant-core.c			\
	evariant-serialiser.c		\
	evariant-util.c			\
	evariant-printer.c		\
	evariant-valist.c		\
	evarianttypeinfo.c		\
	$(NULL)

evariant_headers =			\
	evariant.h			\
	ebitlock.h			\
	evariant-serialiser.h		\
	evarianttype.h			\
	evarianttypeinfo.h		\
	$(NULL)

edbus_headers =				\
	$(evariant_headers)		\
	edbus.h				\
	edbusenums.h			\
	edbustypes.h			\
	edbuserror.h			\
	edbusconnection.h		\
	edbusnameowning.h		\
	edbusnamewatching.h		\
	edbusproxywatching.h		\
	edbusproxy.h			\
	edbusintrospection.h		\
	edbusmethodinvocation.h		\
	edbusserver.h			\
	$(NULL)

libedbus_la_SOURCES =					\
	$(evariant_sources)						\
	edbus.h								\
	edbustypes.h							\
	edbusenumtypes.h						\
	edbus-marshal.h 		edbus-marshal.c 		\
	edbusenumtypes.h		edbusenumtypes.c		\
	edbuserror.h			edbuserror.c			\
	edbusconnection.h		edbusconnection.c		\
	edbusnameowning.h		edbusnameowning.c		\
	edbusnamewatching.h		edbusnamewatching.c		\
	edbusproxywatching.h		edbusproxywatching.c		\
	edbusproxy.h			edbusproxy.c			\
	edbusprivate.h			edbusprivate.c			\
	edbusintrospection.h		edbusintrospection.c		\
	edbusconversion.h		edbusconversion.c		\
	edbusmethodinvocation.h		edbusmethodinvocation.c		\
	edbusserver.h			edbusserver.c			\
	$(NULL)

$(libedbus_la_OBJECTS): $(marshal_sources)

libedbus_la_CFLAGS =					\
	$(EDBUS_CFLAGS)					\
	$(NULL)

libedbus_la_LIBADD =					\
	$(EDBUS_LIBS)					\
	$(NULL)
install-def-file:
uninstall-def-file:

export_symbols = -export-symbols-regex '^e_.*'

LT_CURRENT=0
LT_REVISION=0
LT_AGE=0

libedbus_la_LDFLAGS = \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-export-dynamic $(no_undefined) $(export_symbols)

libedbus_la_DEPENDENCIES = $(edbus_def)

edbusincludedir=$(includedir)/edbus/edbus
edbusinclude_HEADERS = 			\
	$(edbus_headers)		\
	edbusenumtypes.h		\
	$(NULL)

# these sources (also mentioned above) are generated.
BUILT_SOURCES = 			\
	edbus-marshal.h 		\
	edbus-marshal.c 		\
	edbusenumtypes.h		\
	edbusenumtypes.c		\
	$(NULL)

EXTRA_DIST=
EXTRA_DIST += 				\
	edbus-marshal.list		\
	edbusenumtypes.h.template	\
	edbusenumtypes.c.template	\
	$(NULL)

CLEANFILES = 			\
	$(marshal_sources)	\
	$(NULL)

edbusenumtypes.h: $(edbus_headers) edbusenumtypes.h.template Makefile.am
	( top_builddir=`cd $(top_builddir) && pwd`; \
	  cd $(srcdir) && glib-mkenums --template edbusenumtypes.h.template $(edbus_headers) ) > \
	    edbusenumtypes.h.tmp && \
	    mv edbusenumtypes.h.tmp edbusenumtypes.h

edbusenumtypes.c: $(edbus_headers) edbusenumtypes.c.template Makefile.am
	( top_builddir=`cd $(top_builddir) && pwd`; \
	  cd $(srcdir) && glib-mkenums --template edbusenumtypes.c.template $(edbus_headers) ) > \
	    edbusenumtypes.c.tmp && mv edbusenumtypes.c.tmp edbusenumtypes.c

noinst_PROGRAMS =
noinst_PROGRAMS += example-own-name
noinst_PROGRAMS += example-watch-name
noinst_PROGRAMS += example-watch-proxy
noinst_PROGRAMS += example-server
noinst_PROGRAMS += example-subtree

example_watch_name_SOURCES = example-watch-name.c
example_watch_name_CFLAGS  = $(EDBUS_CFLAGS)
example_watch_name_LDADD   = libedbus.la $(EDBUS_LIBS)

example_watch_proxy_SOURCES = example-watch-proxy.c
example_watch_proxy_CFLAGS  = $(EDBUS_CFLAGS)
example_watch_proxy_LDADD   = libedbus.la $(EDBUS_LIBS)

example_own_name_SOURCES = example-own-name.c
example_own_name_CFLAGS  = $(EDBUS_CFLAGS)
example_own_name_LDADD   = libedbus.la $(EDBUS_LIBS)

example_server_SOURCES = example-server.c
example_server_CFLAGS  = $(EDBUS_CFLAGS)
example_server_LDADD   = libedbus.la $(EDBUS_LIBS)

example_subtree_SOURCES = example-subtree.c
example_subtree_CFLAGS  = $(EDBUS_CFLAGS)
example_subtree_LDADD   = libedbus.la $(EDBUS_LIBS)


