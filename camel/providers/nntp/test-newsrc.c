#include <stdio.h>
#include <glib.h>
#include "camel-nntp-newsrc.h"

gint
main (gint argc, gchar *argv[])
{
  CamelNNTPNewsrc *newsrc = camel_nntp_newsrc_read_for_server (argv[1]);
  camel_nntp_newsrc_write_to_file (newsrc, stdout);
}
