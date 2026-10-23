/* Minimal 64-bit dedicated server launcher (srcds_portal64).
 *
 * The HL2DM dedicated server (app 232370) ships 64-bit engine
 * libraries in bin/linux64 but only a 32-bit srcds_linux. This does what
 * that launcher does: load dedicated_srv.so and call DedicatedMain.
 *
 * Build: cc -O2 -o srcds_portal64 srcds_portal64.c -ldl
 * Run from the server root: LD_LIBRARY_PATH=bin/linux64 ./srcds_portal64 -game ... */
#include <dlfcn.h>
#include <stdio.h>

typedef int (*DedicatedMain_t)( int argc, char **argv );

int main( int argc, char **argv )
{
	void *pDedicated = dlopen( "bin/linux64/dedicated_srv.so", RTLD_NOW );
	if ( !pDedicated )
	{
		fprintf( stderr, "srcds_portal64: %s\n", dlerror() );
		return 1;
	}

	DedicatedMain_t pMain = (DedicatedMain_t)dlsym( pDedicated, "DedicatedMain" );
	if ( !pMain )
	{
		fprintf( stderr, "srcds_portal64: no DedicatedMain: %s\n", dlerror() );
		return 1;
	}

	return pMain( argc, argv );
}
