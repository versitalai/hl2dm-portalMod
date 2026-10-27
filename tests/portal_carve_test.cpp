// Checks the convex solid / prism subtraction in hl2mp_portal_carve.h (used by the
// portal's local collision world), using the SDK's mathlib.
// Run: tests/run_carve_test.sh [sdk-dir]

#include "mathlib/mathlib.h"
#include "hl2mp_portal_carve.h"
#include <stdio.h>
#include <math.h>
#include <float.h>

static int s_nFailures = 0;

static void Report( const char *pszName, bool bOk, const char *pszDetail )
{
	printf( "%s %-48s %s\n", bOk ? "PASS" : "FAIL", pszName, pszDetail );
	if ( !bOk )
		s_nFailures++;
}

static void CheckBool( const char *pszName, bool got, bool want )
{
	char sz[64];
	snprintf( sz, sizeof( sz ), "got %d want %d", got, want );
	Report( pszName, got == want, sz );
}

static void CheckInt( const char *pszName, int got, int want )
{
	char sz[64];
	snprintf( sz, sizeof( sz ), "got %d want %d", got, want );
	Report( pszName, got == want, sz );
}

static void CheckVol( const char *pszName, double got, double want, double flRelTol = 0.001 )
{
	char sz[96];
	snprintf( sz, sizeof( sz ), "got %.2f want %.2f", got, want );
	Report( pszName, fabs( got - want ) <= flRelTol * fabs( want ) + 1e-6, sz );
}

// Axis-aligned box as 6 planes.
static void MakeBox( PortalCarvePlanes_t &p, const Vector &mins, const Vector &maxs )
{
	p.RemoveAll();
	p.AddToTail( PortalCarvePlane_t( Vector( 1, 0, 0 ), maxs.x ) );
	p.AddToTail( PortalCarvePlane_t( Vector( -1, 0, 0 ), -mins.x ) );
	p.AddToTail( PortalCarvePlane_t( Vector( 0, 1, 0 ), maxs.y ) );
	p.AddToTail( PortalCarvePlane_t( Vector( 0, -1, 0 ), -mins.y ) );
	p.AddToTail( PortalCarvePlane_t( Vector( 0, 0, 1 ), maxs.z ) );
	p.AddToTail( PortalCarvePlane_t( Vector( 0, 0, -1 ), -mins.z ) );
}

// Volume from the vertices: per face (plane), order the verts on it around their
// centroid and fan-triangulate; sum tetrahedra against the solid's centroid.
static double Volume( const PortalCarvePlanes_t &planes )
{
	CUtlVector<Vector> verts;
	if ( !PortalCarve_IsSolid( planes, &verts ) )
		return 0.0;

	Vector vC( 0, 0, 0 );
	for ( int v = 0; v < verts.Count(); v++ )
		vC += verts[v];
	vC /= (float)verts.Count();

	double flVol = 0.0;
	for ( int i = 0; i < planes.Count(); i++ )
	{
		// Skip duplicate (coplanar, same-facing) planes so faces aren't counted twice.
		bool bDup = false;
		for ( int j = 0; j < i && !bDup; j++ )
			bDup = ( planes[j].n - planes[i].n ).Length() < 1e-4f && fabs( planes[j].d - planes[i].d ) < 0.01f;
		if ( bDup )
			continue;

		CUtlVector<Vector> face;
		for ( int v = 0; v < verts.Count(); v++ )
		{
			if ( fabs( DotProduct( planes[i].n, verts[v] ) - planes[i].d ) <= 0.01f )
				face.AddToTail( verts[v] );
		}
		if ( face.Count() < 3 )
			continue;

		Vector vF( 0, 0, 0 );
		for ( int v = 0; v < face.Count(); v++ )
			vF += face[v];
		vF /= (float)face.Count();

		Vector vU = face[0] - vF, vW;
		VectorNormalize( vU );
		vW = CrossProduct( planes[i].n, vU );
		VectorNormalize( vW );

		// Selection sort by angle.
		float flAng[64];
		int nFace = MIN( face.Count(), 64 );
		for ( int v = 0; v < nFace; v++ )
		{
			Vector vD = face[v] - vF;
			flAng[v] = atan2f( DotProduct( vD, vW ), DotProduct( vD, vU ) );
		}
		for ( int a = 0; a < nFace; a++ )
		{
			for ( int b = a + 1; b < nFace; b++ )
			{
				if ( flAng[b] < flAng[a] )
				{
					float t = flAng[a]; flAng[a] = flAng[b]; flAng[b] = t;
					Vector vT = face[a]; face[a] = face[b]; face[b] = vT;
				}
			}
		}

		for ( int v = 0; v < nFace; v++ )
		{
			Vector a = face[v] - vC, b = face[( v + 1 ) % nFace] - vC, c = vF - vC;
			flVol += fabs( (double)DotProduct( a, CrossProduct( b, c ) ) ) / 6.0;
		}
	}
	return flVol;
}

static Vector Centroid( const PortalCarvePlanes_t &planes )
{
	CUtlVector<Vector> verts;
	PortalCarve_ComputeVerts( planes.Base(), planes.Count(), verts );
	Vector vC( 0, 0, 0 );
	for ( int v = 0; v < verts.Count(); v++ )
		vC += verts[v];
	return verts.Count() ? vC / (float)verts.Count() : vC;
}

static bool StrictlyInside( const PortalCarvePlanes_t &planes, const Vector &p, float flMargin = 0.01f )
{
	for ( int i = 0; i < planes.Count(); i++ )
	{
		if ( DotProduct( planes[i].n, p ) - planes[i].d >= -flMargin )
			return false;
	}
	return true;
}

static double Intersection( const PortalCarvePlanes_t &a, const PortalCarvePlanes_t &b )
{
	PortalCarvePlanes_t both;
	both.AddVectorToTail( a );
	both.AddVectorToTail( b );
	return Volume( both );
}

// Common checks on the result of subtracting prism from solid: conservation, solidity, no overlap.
static void CheckSubtract( const char *pszName, const PortalCarvePlanes_t &solid, const PortalCarvePlanes_t &prism,
	CUtlVector<PortalCarvePlanes_t> &pieces, double flWantVol )
{
	char sz[96];
	double flSum = 0.0;
	bool bAllSolid = true, bOverlap = false;
	for ( int i = 0; i < pieces.Count(); i++ )
	{
		flSum += Volume( pieces[i] );
		bAllSolid = bAllSolid && PortalCarve_IsSolid( pieces[i] );
		Vector vC = Centroid( pieces[i] );
		for ( int j = 0; j < pieces.Count(); j++ )
		{
			if ( i != j && StrictlyInside( pieces[j], vC ) )
				bOverlap = true;
		}
		// No piece may reach into the hole.
		if ( Intersection( pieces[i], prism ) > 0.001 * Volume( pieces[i] ) )
			bOverlap = true;
	}
	snprintf( sz, sizeof( sz ), "%s: volume (%d pieces)", pszName, pieces.Count() );
	CheckVol( sz, flSum, flWantVol );
	snprintf( sz, sizeof( sz ), "%s: every piece solid", pszName );
	CheckBool( sz, bAllSolid, true );
	snprintf( sz, sizeof( sz ), "%s: no overlaps", pszName );
	CheckBool( sz, !bOverlap, true );
}

int main()
{
	// a. Box verts.
	{
		PortalCarvePlanes_t box; MakeBox( box, Vector( -1, -2, -3 ), Vector( 4, 5, 6 ) );
		CUtlVector<Vector> verts;
		PortalCarve_ComputeVerts( box.Base(), box.Count(), verts );
		CheckInt( "box has 8 verts", verts.Count(), 8 );
		int nCorners = 0;
		for ( int v = 0; v < verts.Count(); v++ )
		{
			bool bx = fabs( verts[v].x + 1 ) < 0.001f || fabs( verts[v].x - 4 ) < 0.001f;
			bool by = fabs( verts[v].y + 2 ) < 0.001f || fabs( verts[v].y - 5 ) < 0.001f;
			bool bz = fabs( verts[v].z + 3 ) < 0.001f || fabs( verts[v].z - 6 ) < 0.001f;
			if ( bx && by && bz )
				nCorners++;
		}
		CheckInt( "all verts are box corners", nCorners, 8 );
		CheckBool( "box IsSolid", PortalCarve_IsSolid( box ), true );
		CheckVol( "box volume helper", Volume( box ), 5.0 * 7.0 * 9.0 );
	}

	// b. Degenerate / empty.
	{
		PortalCarvePlanes_t flat; MakeBox( flat, Vector( 0, 0, 0 ), Vector( 10, 10, 10 ) );
		flat[1].d = -10.0f; // x>=10 and x<=10: zero thickness
		CheckBool( "zero-thickness box not solid", PortalCarve_IsSolid( flat ), false );
		PortalCarvePlanes_t thin; MakeBox( thin, Vector( 0, 0, 0 ), Vector( 0.01f, 10, 10 ) );
		CheckBool( "0.01 thick box not solid", PortalCarve_IsSolid( thin ), false );
		PortalCarvePlanes_t empty; MakeBox( empty, Vector( 0, 0, 0 ), Vector( 10, 10, 10 ) );
		empty[1].d = -20.0f; // x>=20 and x<=10
		CheckBool( "empty intersection not solid", PortalCarve_IsSolid( empty ), false );
		PortalCarvePlanes_t none;
		CheckBool( "no planes not solid", PortalCarve_IsSolid( none ), false );
	}

	PortalCarvePlanes_t wall; MakeBox( wall, Vector( -16, -128, -128 ), Vector( 0, 128, 128 ) );
	double flWallVol = 16.0 * 256.0 * 256.0;
	double flHoleVol = 16.0 * 66.0 * 114.0;

	PortalCarvePlanes_t prism;
	prism.AddToTail( PortalCarvePlane_t( Vector( 1, 0, 0 ), 1 ) );
	prism.AddToTail( PortalCarvePlane_t( Vector( 0, 1, 0 ), 33 ) );
	prism.AddToTail( PortalCarvePlane_t( Vector( 0, -1, 0 ), 33 ) );
	prism.AddToTail( PortalCarvePlane_t( Vector( 0, 0, 1 ), 57 ) );
	prism.AddToTail( PortalCarvePlane_t( Vector( 0, 0, -1 ), 57 ) );
	// The prism is open toward -x; the wall brush is only 16 thick, so give the
	// prism volume by capping it far behind the wall for the intersection helper.
	PortalCarvePlanes_t prismCapped; prismCapped = prism;
	prismCapped.AddToTail( PortalCarvePlane_t( Vector( -1, 0, 0 ), 1000 ) );

	// c. Wall brush.
	{
		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( wall, prism, pieces );
		CheckSubtract( "wall", wall, prismCapped, pieces, flWallVol - flHoleVol );
		bool bHit = false;
		for ( int i = 0; i < pieces.Count(); i++ )
			bHit = bHit || StrictlyInside( pieces[i], Vector( -8, 0, 0 ), -0.0f );
		CheckBool( "wall: hole center (-8,0,0) empty", !bHit, true );
		bool bSolidPoint = false;
		for ( int i = 0; i < pieces.Count(); i++ )
			bSolidPoint = bSolidPoint || StrictlyInside( pieces[i], Vector( -8, 100, 0 ) );
		CheckBool( "wall: point (-8,100,0) still solid", bSolidPoint, true );
	}

	// d. Far-away brush.
	{
		PortalCarvePlanes_t far; MakeBox( far, Vector( -16, 500, 500 ), Vector( 0, 600, 600 ) );
		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( far, prism, pieces );
		CheckInt( "far brush: 1 piece", pieces.Count(), 1 );
		if ( pieces.Count() == 1 )
			CheckVol( "far brush: unchanged volume", Volume( pieces[0] ), Volume( far ) );
	}

	// e. Brush inside prism.
	{
		PortalCarvePlanes_t inside; MakeBox( inside, Vector( -10, -10, -10 ), Vector( 0, 10, 10 ) );
		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( inside, prism, pieces );
		CheckInt( "inside brush: 0 pieces", pieces.Count(), 0 );
	}

	// f. Slanted brush: x+y <= 2 cuts the corner of the wall brush.
	{
		PortalCarvePlanes_t slanted; slanted = wall;
		float s = 1.0f / sqrtf( 2.0f );
		slanted[0].d = 100.0f; // drop x<=0 so the slanted plane is the +x face
		slanted.AddToTail( PortalCarvePlane_t( Vector( s, s, 0 ), 2.0f * s ) );
		double flVol = Volume( slanted );
		double flInt = Intersection( slanted, prismCapped );
		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( slanted, prism, pieces );
		CheckBool( "slanted: overlaps prism", flInt > 1.0, true );
		CheckSubtract( "slanted", slanted, prismCapped, pieces, flVol - flInt );
	}

	// g. 16-gon (elliptic) prism, half extents 32 (y) x 56 (z).
	{
		PortalCarvePlanes_t gon;
		gon.AddToTail( PortalCarvePlane_t( Vector( 1, 0, 0 ), 1 ) );
		for ( int i = 0; i < 16; i++ )
		{
			float t = 2.0f * M_PI * ( i + 0.5f ) / 16.0f;
			float c = cosf( t ), s = sinf( t );
			float h = sqrtf( 32.0f * c * 32.0f * c + 56.0f * s * 56.0f * s );
			gon.AddToTail( PortalCarvePlane_t( Vector( 0, c, s ), h ) );
		}
		PortalCarvePlanes_t gonCapped; gonCapped = gon;
		gonCapped.AddToTail( PortalCarvePlane_t( Vector( -1, 0, 0 ), 1000 ) );
		double flInt = Intersection( wall, gonCapped );
		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( wall, gon, pieces );
		CheckBool( "16-gon: overlaps wall", flInt > 1.0, true );
		CheckSubtract( "16-gon", wall, gonCapped, pieces, flWallVol - flInt );
	}

	if ( s_nFailures == 0 )
		printf( "ALL PASSED (0 failures)\n" );
	else
		printf( "%d FAILURES\n", s_nFailures );
	return s_nFailures ? 1 : 0;
}
