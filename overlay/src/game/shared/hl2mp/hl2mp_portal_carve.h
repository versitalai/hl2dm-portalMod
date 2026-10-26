//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Convex geometry for the portal's local collision world (see
//			CHL2MPPortalWorld): convex solids as plane sets, their vertices,
//			and cutting a convex hole (prism) out of them. Pure math, no
//			engine calls, so tests/portal_carve_test.cpp runs it standalone.
//
//=============================================================================//

#ifndef HL2MP_PORTAL_CARVE_H
#define HL2MP_PORTAL_CARVE_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "utlvector.h"

// A point is inside when DotProduct( n, p ) <= d (normal points out of the solid).
struct PortalCarvePlane_t
{
	Vector	n;
	float	d;

	PortalCarvePlane_t() {}
	PortalCarvePlane_t( const Vector &vNormal, float flDist ) : n( vNormal ), d( flDist ) {}
	PortalCarvePlane_t Flipped() const { return PortalCarvePlane_t( -n, -d ); }
};

typedef CUtlVector<PortalCarvePlane_t> PortalCarvePlanes_t;

// Vertices are accepted this far outside a plane, and merged this close together.
#define PORTAL_CARVE_VERT_EPSILON	0.01f
// Solids thinner than this (slivers left by cuts along a face) are dropped.
#define PORTAL_CARVE_MIN_THICKNESS	0.05f

// The corners of the convex solid bounded by the planes: every point where
// three planes meet that's inside all the others. Empty if the solid is.
inline void PortalCarve_ComputeVerts( const PortalCarvePlane_t *pPlanes, int nPlanes, CUtlVector<Vector> &verts )
{
	verts.RemoveAll();
	for ( int i = 0; i < nPlanes; i++ )
	{
		for ( int j = i + 1; j < nPlanes; j++ )
		{
			Vector vIJ = CrossProduct( pPlanes[i].n, pPlanes[j].n );
			if ( vIJ.LengthSqr() < 1e-10f )
				continue;
			for ( int k = j + 1; k < nPlanes; k++ )
			{
				// Cramer's rule: p = ( d_i (n_j x n_k) + d_j (n_k x n_i) + d_k (n_i x n_j) ) / det.
				float flDet = DotProduct( pPlanes[k].n, vIJ );
				if ( fabs( flDet ) < 1e-6f )
					continue;
				Vector vPoint = ( CrossProduct( pPlanes[j].n, pPlanes[k].n ) * pPlanes[i].d +
					CrossProduct( pPlanes[k].n, pPlanes[i].n ) * pPlanes[j].d +
					vIJ * pPlanes[k].d ) / flDet;

				bool bInside = true;
				for ( int m = 0; m < nPlanes && bInside; m++ )
				{
					bInside = DotProduct( pPlanes[m].n, vPoint ) - pPlanes[m].d <= PORTAL_CARVE_VERT_EPSILON;
				}
				if ( !bInside )
					continue;

				bool bDuplicate = false;
				for ( int m = 0; m < verts.Count() && !bDuplicate; m++ )
				{
					bDuplicate = ( verts[m] - vPoint ).LengthSqr() < PORTAL_CARVE_VERT_EPSILON * PORTAL_CARVE_VERT_EPSILON;
				}
				if ( !bDuplicate )
				{
					verts.AddToTail( vPoint );
				}
			}
		}
	}
}

// Does the solid have volume? Its thinnest extent across any of its planes
// that actually bounds it must reach PORTAL_CARVE_MIN_THICKNESS.
inline bool PortalCarve_IsSolid( const PortalCarvePlane_t *pPlanes, int nPlanes, const CUtlVector<Vector> &verts )
{
	if ( verts.Count() < 4 )
		return false;

	for ( int i = 0; i < nPlanes; i++ )
	{
		float flNearest = FLT_MAX, flFarthest = 0.0f;
		for ( int v = 0; v < verts.Count(); v++ )
		{
			float flDepth = pPlanes[i].d - DotProduct( pPlanes[i].n, verts[v] );
			flNearest = MIN( flNearest, flDepth );
			flFarthest = MAX( flFarthest, flDepth );
		}
		// Only planes the solid touches are faces.
		if ( flNearest <= PORTAL_CARVE_VERT_EPSILON && flFarthest < PORTAL_CARVE_MIN_THICKNESS )
			return false;
	}
	return true;
}

inline bool PortalCarve_IsSolid( const PortalCarvePlanes_t &planes, CUtlVector<Vector> *pVerts = NULL )
{
	CUtlVector<Vector> verts;
	CUtlVector<Vector> &out = pVerts ? *pVerts : verts;
	PortalCarve_ComputeVerts( planes.Base(), planes.Count(), out );
	return PortalCarve_IsSolid( planes.Base(), planes.Count(), out );
}

// Cut a convex prism (the hole) out of a convex solid. The pieces left are
// convex and don't overlap: for each prism plane in turn, the part of what's
// left that's outside that plane is a piece, and the rest carries on to the
// next plane. Whatever is left after the last plane is inside the hole and
// is dropped. A solid that doesn't reach into the prism comes back whole.
inline void PortalCarve_Subtract( const PortalCarvePlanes_t &solid, const PortalCarvePlanes_t &prism, CUtlVector<PortalCarvePlanes_t> &pieces )
{
	PortalCarvePlanes_t overlap;
	overlap.AddVectorToTail( solid );
	overlap.AddVectorToTail( prism );
	if ( !PortalCarve_IsSolid( overlap ) )
	{
		if ( PortalCarve_IsSolid( solid ) )
		{
			pieces[pieces.AddToTail()] = solid;
		}
		return;
	}

	PortalCarvePlanes_t rest;
	rest.AddVectorToTail( solid );
	for ( int i = 0; i < prism.Count(); i++ )
	{
		PortalCarvePlanes_t piece;
		piece.AddVectorToTail( rest );
		piece.AddToTail( prism[i].Flipped() );
		if ( PortalCarve_IsSolid( piece ) )
		{
			pieces[pieces.AddToTail()] = piece;
		}

		rest.AddToTail( prism[i] );
		if ( !PortalCarve_IsSolid( rest ) )
			return;
	}
}

#endif // HL2MP_PORTAL_CARVE_H
