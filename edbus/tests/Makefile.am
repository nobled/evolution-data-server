include $(srcdir)/Makefile.decl

NULL =

INCLUDES =                      \
        -g                      \
        -I$(top_srcdir)         \
	$(EDBUS_CFLAGS)		\
	$(NULL)

noinst_PROGRAMS = $(TEST_PROGS)
progs_ldadd     =				\
	$(EDBUS_LIBS)				\
        $(top_builddir)/edbus/libedbus.la	\
	$(NULL)

TEST_PROGS += connection
TEST_PROGS += names
TEST_PROGS += proxy
TEST_PROGS += introspection
TEST_PROGS += threading
TEST_PROGS += export
TEST_PROGS += error
TEST_PROGS += peer

connection_SOURCES = connection.c sessionbus.c sessionbus.h tests.h tests.c
connection_LDADD = $(progs_ldadd)

names_SOURCES = names.c sessionbus.c sessionbus.h tests.h tests.c
names_LDADD = $(progs_ldadd)

proxy_SOURCES = proxy.c sessionbus.c sessionbus.h tests.h tests.c
proxy_LDADD = $(progs_ldadd)

introspection_SOURCES = introspection.c sessionbus.c sessionbus.h tests.h tests.c
introspection_LDADD = $(progs_ldadd)

threading_SOURCES = threading.c sessionbus.c sessionbus.h tests.h tests.c
threading_LDADD = $(progs_ldadd)

export_SOURCES = export.c sessionbus.c sessionbus.h tests.h tests.c
export_CFLAGS = $(DBUS1_CFLAGS)
export_LDADD = $(progs_ldadd)

error_SOURCES = error.c sessionbus.c sessionbus.h tests.h tests.c
error_CFLAGS = $(DBUS1_CFLAGS)
error_LDADD = $(progs_ldadd)

peer_SOURCES = peer.c sessionbus.c sessionbus.h tests.h tests.c
peer_CFLAGS = $(DBUS1_CFLAGS)
peer_LDADD = $(progs_ldadd)
