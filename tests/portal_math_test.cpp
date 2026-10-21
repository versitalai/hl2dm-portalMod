// Checks the portal transform conventions used by CHL2MPPortal::MatrixThisToLinked
// and the virtual camera in CViewRender::DrawHL2MPPortalViews, using the SDK's mathlib.
// Run: tests/run_math_test.sh [sdk-dir]

#include "mathlib/mathlib.h"
#include "mathlib/vmatrix.h"
#include <stdio.h>

struct Portal_t
{
	Vector	origin;
	QAngle	angles;
};

// Same frame as CHL2MPPortal::PlaceOnSurface: forward = normal.
static Portal_t MakePortal( const Vector &origin, const Vector &normal, const Vector &up )
{
	Portal_t p;
	p.origin = origin;
	VectorAngles( normal, up, p.angles );
	return p;
}

// Same math as CHL2MPPortal::MatrixThisToLinked.
static VMatrix ThisToLinked( const Portal_t &a, const Portal_t &b )
{
	VMatrix matA, matB, matAInv, matRotate;
	matA.SetupMatrixOrgAngles( a.origin, a.angles );
	matB.SetupMatrixOrgAngles( b.origin, b.angles );
	MatrixInverseTR( matA, matAInv );
	matRotate.Identity();
	matRotate.m[0][0] = -1.0f;
	matRotate.m[1][1] = -1.0f;
	return matB * matRotate * matAInv;
}

static int s_nFailures = 0;

static void Check( const char *pszName, const Vector &got, const Vector &want )
{
	bool bOk = ( got - want ).Length() < 0.01f;
	printf( "%s %-40s got (%8.2f %8.2f %8.2f) want (%8.2f %8.2f %8.2f)\n", bOk ? "PASS" : "FAIL", pszName,
		got.x, got.y, got.z, want.x, want.y, want.z );
	if ( !bOk )
		s_nFailures++;
}

int main()
{
	// Wall portal at x=0 facing +x; floor portal at (500,0,0) facing up; opposite wall at x=1000 facing -x.
	Portal_t wall = MakePortal( Vector( 0, 0, 64 ), Vector( 1, 0, 0 ), Vector( 0, 0, 1 ) );
	Portal_t floor = MakePortal( Vector( 500, 0, 0 ), Vector( 0, 0, 1 ), Vector( 1, 0, 0 ) );
	Portal_t farWall = MakePortal( Vector( 1000, 0, 64 ), Vector( -1, 0, 0 ), Vector( 0, 0, 1 ) );

	Vector vForward;
	AngleVectors( wall.angles, &vForward );
	Check( "wall portal forward is its normal", vForward, Vector( 1, 0, 0 ) );
	AngleVectors( floor.angles, &vForward );
	Check( "floor portal forward is its normal", vForward, Vector( 0, 0, 1 ) );

	VMatrix wallToFloor = ThisToLinked( wall, floor );
	Check( "center maps to center", wallToFloor.VMul4x3( wall.origin ), floor.origin );
	Check( "in front of entry -> behind exit", wallToFloor.VMul4x3( Vector( 10, 0, 64 ) ), Vector( 500, 0, -10 ) );
	Check( "behind entry -> in front of exit", wallToFloor.VMul4x3( Vector( -10, 0, 64 ) ), Vector( 500, 0, 10 ) );
	Check( "velocity into wall -> up out of floor", wallToFloor.VMul3x3( Vector( -100, 0, 0 ) ), Vector( 0, 0, 100 ) );

	VMatrix wallToFarWall = ThisToLinked( wall, farWall );
	Check( "facing walls: keep moving the same way", wallToFarWall.VMul3x3( Vector( -100, 0, 0 ) ), Vector( -100, 0, 0 ) );
	Check( "facing walls: no mirroring", wallToFarWall.VMul4x3( Vector( 0, 10, 64 ) ), Vector( 1000, 10, 64 ) );

	// Virtual camera: viewer 200 in front of the wall portal looking into it.
	VMatrix matCamera, matVirtual;
	matCamera.SetupMatrixOrgAngles( Vector( 200, 0, 64 ), QAngle( 0, 180, 0 ) );
	matVirtual = wallToFarWall * matCamera;
	QAngle angVirtual;
	MatrixAngles( matVirtual.As3x4(), angVirtual );
	AngleVectors( angVirtual, &vForward );
	Check( "virtual camera origin behind exit", wallToFarWall.VMul4x3( Vector( 200, 0, 64 ) ), Vector( 1200, 0, 64 ) );
	Check( "virtual camera looks out of exit", vForward, Vector( -1, 0, 0 ) );

	VMatrix roundTrip = ThisToLinked( farWall, wall ) * wallToFarWall;
	Check( "A->B then B->A is identity", roundTrip.VMul4x3( Vector( 3, 4, 5 ) ), Vector( 3, 4, 5 ) );

	printf( "%s (%d failures)\n", s_nFailures ? "FAILED" : "ALL PASSED", s_nFailures );
	return s_nFailures ? 1 : 0;
}
