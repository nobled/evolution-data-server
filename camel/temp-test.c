#include <camel-stream-mem.h>

int main( int argc, char **argv )
{
	CamelStream *mem;

	mem = camel_stream_mem_new();
	printf( "%s\n", camel_object_describe( CAMEL_OBJECT(mem) ) );
	camel_object_unref( CAMEL_OBJECT(mem) );
	printf( "%s\n", camel_object_describe( CAMEL_OBJECT(mem) ) );
	return 0;
}
