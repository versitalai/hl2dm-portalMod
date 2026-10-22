/* Minimal 64-bit dedicated server launcher for the self-test.
 *
 * The Source SDK Base 2013 Dedicated Server (app 244310) ships 64-bit engine
 * libraries in bin/linux64 but only a 32-bit srcds_linux. This does what
 * that launcher does: load dedicated_srv.so and call DedicatedMain.
 *
 * Build: cc -O2 -o srcds64 srcds64.c -ldl
 * Run from the server root: LD_LIBRARY_PATH=bin/linux64 ./srcds64 -game ... */
#include <dlfcn.h>
#include <stdio.h>

typedef int (*DedicatedMain_t)( int argc, char **argv );

int main( int argc, char **argv )
{
	void *pDedicated = dlopen( "bin/linux64/dedicated_srv.so", RTLD_NOW );
	if ( !pDedicated )
	{
		fprintf( stderr, "srcds64: %s\n", dlerror() );
		return 1;
	}

	DedicatedMain_t pMain = (DedicatedMain_t)dlsym( pDedicated, "DedicatedMain" );
	if ( !pMain )
	{
		fprintf( stderr, "srcds64: no DedicatedMain: %s\n", dlerror() );
		return 1;
	}

	return pMain( argc, argv );
}
