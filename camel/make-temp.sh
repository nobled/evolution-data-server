#!/bin/sh

gcc -o temp-test -g -I. -I.. -I/usr/lib/glib/include temp-test.c camel-object.c camel-stream.c camel-seekable-stream.c camel-stream-mem.c -lglib
