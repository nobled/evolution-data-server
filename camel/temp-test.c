#include <camel-stream-mem.h>

static void on_finalize (CamelObject *obj, gpointer event_data, gpointer user_data)
{
	printf ("%s is being finalized! %s\n",
		camel_object_describe (obj), (gchar *) user_data);
}

int main( int argc, char **argv )
{
	CamelStream *mem;

	mem = camel_stream_mem_new();
	camel_object_hook_event (CAMEL_OBJECT(mem), "finalize", on_finalize, "Run for your life!");
	printf( "%s\n", camel_object_describe( CAMEL_OBJECT(mem) ) );
	camel_object_unref( CAMEL_OBJECT(mem) );
	printf( "%s\n", camel_object_describe( CAMEL_OBJECT(mem) ) );
	return 0;
}
