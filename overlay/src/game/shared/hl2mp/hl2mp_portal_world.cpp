//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A portal's local collision world. The map's collision doesn't
//			know about portals: the wall behind the hole is solid. Near a
//			portal, movement is traced against this instead: the world in a
//			box around the portal with the hole cut out of the wall, plus
//			what's in front of the linked portal, mapped in behind this one's
//			plane (through the hole is the other side). Built on both client
//			and server from the same inputs, so prediction matches.
//
//=============================================================================//

#include "cbase.h"
#include "hl2mp_portal.h"
#include "hl2mp_portal_carve.h"
#include "physics_shared.h"
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "vcollide.h"
#include "vphysics_interface.h"

#ifdef CLIENT_DLL
	#include "c_world.h"
#else
	#include "world.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sv_portal_local_world( "sv_portal_local_world", "1", FCVAR_REPLICATED, "Players near a portal collide with its local world (the wall with the hole cut out and the other side behind it) instead of the carving approximation." );

// The box around the portal it covers: this far in front of and behind the
// plane, and this far beyond the hole on each side. Big enough for any one
// tick of movement of a hull touching the hole (anything bigger falls back).
#define PORTAL_WORLD_DEPTH		128.0f
#define PORTAL_WORLD_MARGIN		96.0f
// The hole: the portal's bounding rectangle plus a unit of slack (a hull on
// the floor at a floor-level wall portal sits right on its bottom edge), from
// just in front of the surface back.
#define PORTAL_WORLD_HOLE_SLACK	1.0f
#define PORTAL_WORLD_HOLE_FRONT	1.0f

#define PORTAL_WORLD_BRUSH_CONTENTS	( CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP )

// Which side of the portal's plane a collide counts on. Brush pieces are cut
// exactly (the hole, and the other side's copy clipped at the plane). Static
// props and displacements can't be cut: one that reaches through the plane
// (a prop pushed into the wall, terrain dipping under the floor next to the
// other portal) would otherwise stick out on the wrong side, an invisible
// wall at the hole. Those only collide with the part of a hull on their side.
enum
{
	PORTAL_WORLD_SIDE_BOTH = 0,
	PORTAL_WORLD_SIDE_FRONT,	// this side's uncut geometry
	PORTAL_WORLD_SIDE_BACK,		// the other side's uncut geometry, behind this plane
};

struct PortalWorldCollide_t
{
	CPhysCollide	*pCollide;
	bool			bOwned;		// built here (brush pieces, displacements); static prop models aren't
	int				iContents;
	Vector			vecOrigin;
	QAngle			angAngles;
	int				iSide;
	bool			bPropSafe;	// doesn't reach to where it doesn't belong: props can collide with it whole
};

// Piece outlines for sv_portal_world_draw.
struct PortalWorldBox_t
{
	Vector	vecMins, vecMaxs;
	int		iSide;
	bool	bRemote;
	bool	bPropSafe;
};

#ifdef GAME_DLL
class CHL2MPPortalCollider;
static void DestroyColliderObjects( CHL2MPPortalWorld *pWorld );
#endif

class CHL2MPPortalWorld
{
public:
	CHL2MPPortalWorld() : m_nVersion( 0 ), m_nLinkedVersion( 0 ), m_nLocalPieces( 0 ), m_nRemotePieces( 0 ), m_nStaticProps( 0 ), m_nDisplacements( 0 ), m_nChecksum( 0 ), m_bPropsCanCross( true ), m_nPropUnsafe( 0 ) {}
	~CHL2MPPortalWorld() { Clear(); }

	void Clear()
	{
#ifdef GAME_DLL
		// The collider's physics objects use these collides.
		DestroyColliderObjects( this );
#endif
		FOR_EACH_VEC( m_Collides, i )
		{
			if ( m_Collides[i].bOwned )
			{
				physcollision->DestroyCollide( m_Collides[i].pCollide );
			}
		}
		m_Collides.RemoveAll();
		m_Boxes.RemoveAll();
		m_nLocalPieces = m_nRemotePieces = m_nStaticProps = m_nDisplacements = 0;
		m_nChecksum = 0;
		m_bPropsCanCross = true;
		m_nPropUnsafe = 0;
	}

	int		m_nVersion, m_nLinkedVersion;	// frame versions it was built for
	Vector	m_vecOrigin, m_vecForward, m_vecRight, m_vecUp;
	CUtlVector<PortalWorldCollide_t> m_Collides;
	CUtlVector<PortalWorldBox_t> m_Boxes;
	int		m_nLocalPieces, m_nRemotePieces, m_nStaticProps, m_nDisplacements;
	unsigned int m_nChecksum;
	// Every uncut collide is prop-safe: the prop collider has the whole world,
	// so props can slide through (otherwise they pop through as before).
	bool	m_bPropsCanCross;
	int		m_nPropUnsafe;		// how many uncut collides stop that
#ifdef GAME_DLL
	CHandle<CHL2MPPortalCollider> m_hCollider;
#endif
};

void HL2MPPortalWorld_Destroy( CHL2MPPortalWorld *pWorld )
{
	delete pWorld;
}

//-----------------------------------------------------------------------------
// Building
//-----------------------------------------------------------------------------
static void HashFloat( unsigned int &nHash, float fl )
{
	// To 1/64 unit: the same geometry on client and server hashes the same.
	int n = (int)floor( fl * 64.0f + 0.5f );
	nHash = ( nHash ^ (unsigned int)n ) * 16777619u;
}

static void HashVector( unsigned int &nHash, const Vector &v )
{
	HashFloat( nHash, v.x );
	HashFloat( nHash, v.y );
	HashFloat( nHash, v.z );
}

// The box a portal's world covers, as planes; bFrontOnly keeps just the part
// in front of the plane (the linked portal's side that's copied over).
static void AddRegionPlanes( PortalCarvePlanes_t &planes, const Vector &vOrigin, const Vector &vForward, const Vector &vRight, const Vector &vUp, bool bFrontOnly )
{
	float flHalfWidth = HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_MARGIN;
	float flHalfHeight = HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_MARGIN;
	planes.AddToTail( PortalCarvePlane_t( vForward, DotProduct( vForward, vOrigin ) + PORTAL_WORLD_DEPTH ) );
	planes.AddToTail( PortalCarvePlane_t( -vForward, -DotProduct( vForward, vOrigin ) + ( bFrontOnly ? 0.0f : PORTAL_WORLD_DEPTH ) ) );
	planes.AddToTail( PortalCarvePlane_t( vRight, DotProduct( vRight, vOrigin ) + flHalfWidth ) );
	planes.AddToTail( PortalCarvePlane_t( -vRight, -DotProduct( vRight, vOrigin ) + flHalfWidth ) );
	planes.AddToTail( PortalCarvePlane_t( vUp, DotProduct( vUp, vOrigin ) + flHalfHeight ) );
	planes.AddToTail( PortalCarvePlane_t( -vUp, -DotProduct( vUp, vOrigin ) + flHalfHeight ) );
}

static void GetRegionAABB( const Vector &vOrigin, const Vector &vForward, const Vector &vRight, const Vector &vUp, bool bFrontOnly, Vector *pMins, Vector *pMaxs )
{
	pMins->Init( FLT_MAX, FLT_MAX, FLT_MAX );
	pMaxs->Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for ( int i = 0; i < 8; i++ )
	{
		Vector vCorner = vOrigin +
			vForward * ( ( i & 1 ) ? PORTAL_WORLD_DEPTH : ( bFrontOnly ? 0.0f : -PORTAL_WORLD_DEPTH ) ) +
			vRight * ( ( i & 2 ) ? 1.0f : -1.0f ) * ( HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_MARGIN ) +
			vUp * ( ( i & 4 ) ? 1.0f : -1.0f ) * ( HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_MARGIN );
		VectorMin( *pMins, vCorner, *pMins );
		VectorMax( *pMaxs, vCorner, *pMaxs );
	}
}

// Convexes gathered per contents value, turned into one collide each.
struct PortalWorldGroup_t
{
	int iContents;
	CUtlVector<CPhysConvex *> convexes;
};

static void AddConvex( CHL2MPPortalWorld *pWorld, CUtlVector<PortalWorldGroup_t> &groups, int iContents, CUtlVector<Vector> &verts, bool bRemote, unsigned int &nHash )
{
	CUtlVector<Vector *> pointers;
	PortalWorldBox_t box;
	box.vecMins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	box.vecMaxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	box.iSide = PORTAL_WORLD_SIDE_BOTH;
	box.bRemote = bRemote;
	box.bPropSafe = true;
	FOR_EACH_VEC( verts, i )
	{
		pointers.AddToTail( &verts[i] );
		HashVector( nHash, verts[i] );
		VectorMin( box.vecMins, verts[i], box.vecMins );
		VectorMax( box.vecMaxs, verts[i], box.vecMaxs );
	}
	pWorld->m_Boxes.AddToTail( box );
	CPhysConvex *pConvex = physcollision->ConvexFromVerts( pointers.Base(), pointers.Count() );
	if ( !pConvex )
		return;

	int iGroup = -1;
	FOR_EACH_VEC( groups, i )
	{
		if ( groups[i].iContents == iContents )
		{
			iGroup = i;
			break;
		}
	}
	if ( iGroup < 0 )
	{
		iGroup = groups.AddToTail();
		groups[iGroup].iContents = iContents;
	}
	groups[iGroup].convexes.AddToTail( pConvex );
}

static void AddUncutCollide( CHL2MPPortalWorld *pWorld, PortalWorldCollide_t &collide );

static void AddStaticProps( CHL2MPPortalWorld *pWorld, const Vector &vMins, const Vector &vMaxs, const VMatrix *pTransform, unsigned int &nHash )
{
	CUtlVector<ICollideable *> props;
	staticpropmgr->GetAllStaticPropsInAABB( vMins, vMaxs, &props );
	FOR_EACH_VEC( props, i )
	{
		ICollideable *pProp = props[i];
		if ( pProp->GetSolid() == SOLID_NONE || ( pProp->GetSolidFlags() & FSOLID_NOT_SOLID ) )
			continue;

		Vector vecOrigin = pProp->GetCollisionOrigin();
		QAngle angAngles = pProp->GetCollisionAngles();
		if ( pTransform )
		{
			VMatrix matProp, matMapped;
			matProp.SetupMatrixOrgAngles( vecOrigin, angAngles );
			matMapped = ( *pTransform ) * matProp;
			MatrixAngles( matMapped.As3x4(), angAngles, vecOrigin );
		}

		PortalWorldCollide_t collide;
		collide.iContents = CONTENTS_SOLID;
		collide.vecOrigin = vecOrigin;
		collide.angAngles = angAngles;
		collide.iSide = pTransform ? PORTAL_WORLD_SIDE_BACK : PORTAL_WORLD_SIDE_FRONT;

		if ( pProp->GetSolid() == SOLID_VPHYSICS )
		{
			vcollide_t *pVCollide = modelinfo->GetVCollide( pProp->GetCollisionModel() );
			if ( !pVCollide )
				continue;
			for ( int iSolid = 0; iSolid < pVCollide->solidCount; iSolid++ )
			{
				collide.pCollide = pVCollide->solids[iSolid];
				collide.bOwned = false;
				AddUncutCollide( pWorld, collide );
			}
		}
		else
		{
			collide.pCollide = physcollision->BBoxToCollide( pProp->OBBMins(), pProp->OBBMaxs() );
			collide.bOwned = true;
			if ( !collide.pCollide )
				continue;
			AddUncutCollide( pWorld, collide );
		}
		pWorld->m_nStaticProps++;
		HashVector( nHash, vecOrigin );
	}
}

// A static prop or displacement set (can't be cut). Traces only let it touch
// the part of a hull on its own side of the plane; the prop collider (whole
// shapes, no such clipping) only gets it if it stays where it belongs: this
// side's out of the hole (behind the plane, inside its outline), the other
// side's behind the plane.
static void AddUncutCollide( CHL2MPPortalWorld *pWorld, PortalWorldCollide_t &collide )
{
	PortalWorldBox_t box;
	physcollision->CollideGetAABB( &box.vecMins, &box.vecMaxs, collide.pCollide, collide.vecOrigin, collide.angAngles );

	// Only the server's prop collider uses this.
	collide.bPropSafe = true;
#ifdef GAME_DLL
	// Where it mustn't be, as a box in the portal's frame: this side's, the
	// hole behind the plane; the other side's, in front of the plane where a
	// prop crossing the hole can be (the hole plus a margin; a couple of units
	// of slack in front: terrain meeting the other portal's floor). Its actual
	// shape is tested (a displacement set's bounds cover the whole region),
	// placed in the portal's frame (floor portals are turned to the shooter's
	// view, so the frame isn't the world's).
	const float flCrossMargin = 16.0f;
	Vector vBoxCenter, vBoxHalf;
	if ( collide.iSide == PORTAL_WORLD_SIDE_BACK )
	{
		vBoxCenter.Init( ( 2.0f + PORTAL_WORLD_DEPTH ) * 0.5f, 0, 0 );
		vBoxHalf.Init( ( PORTAL_WORLD_DEPTH - 2.0f ) * 0.5f, HL2MP_PORTAL_HALF_WIDTH + flCrossMargin, HL2MP_PORTAL_HALF_HEIGHT + flCrossMargin );
	}
	else
	{
		float flFront = PORTAL_WORLD_HOLE_FRONT - 2.0f;
		vBoxCenter.Init( ( flFront - PORTAL_WORLD_DEPTH ) * 0.5f, 0, 0 );
		vBoxHalf.Init( ( flFront + PORTAL_WORLD_DEPTH ) * 0.5f, HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_HOLE_SLACK, HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_HOLE_SLACK );
	}

	// Portal frame: x forward, y left, z up.
	matrix3x4_t matPortalToWorld, matWorldToPortal, matCollide, matLocal;
	matPortalToWorld.Init( pWorld->m_vecForward, -pWorld->m_vecRight, pWorld->m_vecUp, pWorld->m_vecOrigin );
	MatrixInvert( matPortalToWorld, matWorldToPortal );
	AngleMatrix( collide.angAngles, collide.vecOrigin, matCollide );
	ConcatTransforms( matWorldToPortal, matCollide, matLocal );
	Vector vLocalOrigin;
	QAngle angLocal;
	MatrixAngles( matLocal, angLocal, vLocalOrigin );

	Ray_t ray;
	ray.Init( vBoxCenter, vBoxCenter, -vBoxHalf, vBoxHalf );
	trace_t tr;
	physcollision->TraceBox( ray, collide.pCollide, vLocalOrigin, angLocal, &tr );
	collide.bPropSafe = !tr.startsolid;
#endif
	if ( !collide.bPropSafe )
	{
		pWorld->m_bPropsCanCross = false;
		pWorld->m_nPropUnsafe++;
	}
	pWorld->m_Collides.AddToTail( collide );

	box.iSide = collide.iSide;
	box.bRemote = ( collide.iSide == PORTAL_WORLD_SIDE_BACK );
	box.bPropSafe = collide.bPropSafe;
	pWorld->m_Boxes.AddToTail( box );
}

static void BuildPortalWorld( const CHL2MPPortal *pPortal, CHL2MPPortalWorld *pWorld )
{
	pWorld->Clear();

	CHL2MPPortal *pLinked = pPortal->GetLinkedPortal();
	Vector vOrigin = pPortal->GetAbsOrigin();
	Vector vForward, vRight, vUp;
	pPortal->GetPortalVectors( &vForward, &vRight, &vUp );
	Vector vLinkedOrigin = pLinked->GetAbsOrigin();
	Vector vLinkedForward, vLinkedRight, vLinkedUp;
	pLinked->GetPortalVectors( &vLinkedForward, &vLinkedRight, &vLinkedUp );
	VMatrix matLinkedToThis = pLinked->MatrixThisToLinked();

	pWorld->m_vecOrigin = vOrigin;
	pWorld->m_vecForward = vForward;
	pWorld->m_vecRight = vRight;
	pWorld->m_vecUp = vUp;

	unsigned int nHash = 2166136261u;
	CUtlVector<PortalWorldGroup_t> groups;

	// The hole, cut out of everything behind (and just in front of) the surface.
	PortalCarvePlanes_t hole;
	hole.AddToTail( PortalCarvePlane_t( vForward, DotProduct( vForward, vOrigin ) + PORTAL_WORLD_HOLE_FRONT ) );
	hole.AddToTail( PortalCarvePlane_t( vRight, DotProduct( vRight, vOrigin ) + HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_HOLE_SLACK ) );
	hole.AddToTail( PortalCarvePlane_t( -vRight, -DotProduct( vRight, vOrigin ) + HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_HOLE_SLACK ) );
	hole.AddToTail( PortalCarvePlane_t( vUp, DotProduct( vUp, vOrigin ) + HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_HOLE_SLACK ) );
	hole.AddToTail( PortalCarvePlane_t( -vUp, -DotProduct( vUp, vOrigin ) + HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_HOLE_SLACK ) );

	CUtlVector<int> brushes;
	CUtlVector<Vector4D> brushPlanes;
	CUtlVector<Vector> verts;

	// This side: brushes in the box, the hole cut out.
	Vector vMins, vMaxs;
	GetRegionAABB( vOrigin, vForward, vRight, vUp, false, &vMins, &vMaxs );
	enginetrace->GetBrushesInAABB( vMins, vMaxs, &brushes, PORTAL_WORLD_BRUSH_CONTENTS );
	FOR_EACH_VEC( brushes, i )
	{
		int iContents = 0;
		brushPlanes.RemoveAll();
		if ( !enginetrace->GetBrushInfo( brushes[i], &brushPlanes, &iContents ) || !( iContents & PORTAL_WORLD_BRUSH_CONTENTS ) )
			continue;

		PortalCarvePlanes_t solid;
		FOR_EACH_VEC( brushPlanes, p )
		{
			solid.AddToTail( PortalCarvePlane_t( brushPlanes[p].AsVector3D(), brushPlanes[p].w ) );
		}
		AddRegionPlanes( solid, vOrigin, vForward, vRight, vUp, false );

		CUtlVector<PortalCarvePlanes_t> pieces;
		PortalCarve_Subtract( solid, hole, pieces );
		FOR_EACH_VEC( pieces, p )
		{
			if ( !PortalCarve_IsSolid( pieces[p], &verts ) )
				continue;
			AddConvex( pWorld, groups, iContents, verts, false, nHash );
			pWorld->m_nLocalPieces++;
		}
	}

	// The other side: brushes in front of the linked portal, moved in behind this one.
	brushes.RemoveAll();
	GetRegionAABB( vLinkedOrigin, vLinkedForward, vLinkedRight, vLinkedUp, true, &vMins, &vMaxs );
	enginetrace->GetBrushesInAABB( vMins, vMaxs, &brushes, PORTAL_WORLD_BRUSH_CONTENTS );
	FOR_EACH_VEC( brushes, i )
	{
		int iContents = 0;
		brushPlanes.RemoveAll();
		if ( !enginetrace->GetBrushInfo( brushes[i], &brushPlanes, &iContents ) || !( iContents & PORTAL_WORLD_BRUSH_CONTENTS ) )
			continue;

		PortalCarvePlanes_t solid;
		FOR_EACH_VEC( brushPlanes, p )
		{
			solid.AddToTail( PortalCarvePlane_t( brushPlanes[p].AsVector3D(), brushPlanes[p].w ) );
		}
		AddRegionPlanes( solid, vLinkedOrigin, vLinkedForward, vLinkedRight, vLinkedUp, true );
		if ( !PortalCarve_IsSolid( solid, &verts ) )
			continue;

		FOR_EACH_VEC( verts, v )
		{
			verts[v] = matLinkedToThis.VMul4x3( verts[v] );
		}
		AddConvex( pWorld, groups, iContents, verts, true, nHash );
		pWorld->m_nRemotePieces++;
	}

	FOR_EACH_VEC( groups, i )
	{
		PortalWorldCollide_t collide;
		collide.pCollide = physcollision->ConvertConvexToCollide( groups[i].convexes.Base(), groups[i].convexes.Count() );
		collide.bOwned = true;
		collide.iContents = groups[i].iContents;
		collide.vecOrigin = vec3_origin;
		collide.angAngles = vec3_angle;
		collide.iSide = PORTAL_WORLD_SIDE_BOTH;
		collide.bPropSafe = true;
		if ( collide.pCollide )
		{
			pWorld->m_Collides.AddToTail( collide );
		}
	}

	// Static props and displacements on both sides (not cut: portals can't go on them).
	GetRegionAABB( vOrigin, vForward, vRight, vUp, false, &vMins, &vMaxs );
	AddStaticProps( pWorld, vMins, vMaxs, NULL, nHash );
	CPhysCollide *pDisp = enginetrace->GetCollidableFromDisplacementsInAABB( vMins, vMaxs );
	if ( pDisp )
	{
		PortalWorldCollide_t collide;
		collide.pCollide = pDisp;
		collide.bOwned = true;
		collide.iContents = CONTENTS_SOLID;
		collide.vecOrigin = vec3_origin;
		collide.angAngles = vec3_angle;
		collide.iSide = PORTAL_WORLD_SIDE_FRONT;
		AddUncutCollide( pWorld, collide );
		pWorld->m_nDisplacements++;
	}

	GetRegionAABB( vLinkedOrigin, vLinkedForward, vLinkedRight, vLinkedUp, true, &vMins, &vMaxs );
	AddStaticProps( pWorld, vMins, vMaxs, &matLinkedToThis, nHash );
	pDisp = enginetrace->GetCollidableFromDisplacementsInAABB( vMins, vMaxs );
	if ( pDisp )
	{
		PortalWorldCollide_t collide;
		collide.pCollide = pDisp;
		collide.bOwned = true;
		collide.iContents = CONTENTS_SOLID;
		MatrixAngles( matLinkedToThis.As3x4(), collide.angAngles, collide.vecOrigin );
		collide.iSide = PORTAL_WORLD_SIDE_BACK;
		AddUncutCollide( pWorld, collide );
		pWorld->m_nDisplacements++;
	}

	HashFloat( nHash, (float)pWorld->m_nLocalPieces );
	HashFloat( nHash, (float)pWorld->m_nRemotePieces );
	pWorld->m_nChecksum = nHash;
}

// The portal's world, (re)built when either portal of the pair has moved.
static CHL2MPPortalWorld *GetPortalWorld( const CHL2MPPortal *pPortal )
{
	if ( !pPortal->CanCarve() )
		return NULL;
	CHL2MPPortal *pLinked = pPortal->GetLinkedPortal();

	CHL2MPPortalWorld *&pWorld = pPortal->LocalWorldStorage();
	if ( !pWorld )
	{
		pWorld = new CHL2MPPortalWorld;
	}
	int nVersion = pPortal->GetFrameVersion();
	int nLinkedVersion = pLinked->GetFrameVersion();
	if ( pWorld->m_nVersion != nVersion || pWorld->m_nLinkedVersion != nLinkedVersion )
	{
		BuildPortalWorld( pPortal, pWorld );
		pWorld->m_nVersion = nVersion;
		pWorld->m_nLinkedVersion = nLinkedVersion;
	}
	return pWorld;
}

//-----------------------------------------------------------------------------
// Queries
//-----------------------------------------------------------------------------
bool HL2MPPortalWorld_Contains( const CHL2MPPortal *pPortal, const Vector &vecCenterStart, const Vector &vecCenterEnd, const Vector &vecHalf )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( !pWorld )
		return false;

	// A unit inside the edge: a hull right on the boundary could touch geometry outside.
	const Vector vAxes[3] = { pWorld->m_vecForward, pWorld->m_vecRight, pWorld->m_vecUp };
	const float flLimits[3] = { PORTAL_WORLD_DEPTH - 1.0f, HL2MP_PORTAL_HALF_WIDTH + PORTAL_WORLD_MARGIN - 1.0f, HL2MP_PORTAL_HALF_HEIGHT + PORTAL_WORLD_MARGIN - 1.0f };
	for ( int i = 0; i < 3; i++ )
	{
		float flExtent = fabs( vAxes[i].x ) * vecHalf.x + fabs( vAxes[i].y ) * vecHalf.y + fabs( vAxes[i].z ) * vecHalf.z;
		float flStart = DotProduct( vAxes[i], vecCenterStart - pWorld->m_vecOrigin );
		float flEnd = DotProduct( vAxes[i], vecCenterEnd - pWorld->m_vecOrigin );
		if ( MAX( fabs( flStart ), fabs( flEnd ) ) + flExtent > flLimits[i] )
			return false;
	}
	return true;
}

// Trace a collide that only counts on one side of the plane (see
// PORTAL_WORLD_SIDE_*) with the part of the swept box on that side. The
// portal's normal is along world axis k. Clipping the box for a whole sweep
// that crosses the plane would cut away most of it, so the sweep goes in
// steps of at most PORTAL_WORLD_SIDE_STEP along the normal, each clipped for
// itself. Returns the fraction of the whole sweep it hit at (1 = no hit).
#define PORTAL_WORLD_SIDE_STEP		4.0f
#define PORTAL_WORLD_SIDE_EPSILON	0.1f

static float TraceSideCollide( const CHL2MPPortalWorld *pWorld, const PortalWorldCollide_t &collide, const Ray_t &ray, int k, float flSign,
	bool *pStartSolid, cplane_t *pPlane )
{
	float flStartDist = flSign * ( ray.m_Start[k] - pWorld->m_vecOrigin[k] );
	float flMove = flSign * ray.m_Delta[k];
	float flExtent = ray.m_Extents[k];
	int nSteps = clamp( (int)ceil( fabs( flMove ) / PORTAL_WORLD_SIDE_STEP ), 1, 64 );
	bool bFront = ( collide.iSide == PORTAL_WORLD_SIDE_FRONT );

	for ( int iStep = 0; iStep < nSteps; iStep++ )
	{
		float t0 = (float)iStep / nSteps;
		float t1 = (float)( iStep + 1 ) / nSteps;
		float flDist0 = flStartDist + flMove * t0;
		float flDist1 = flStartDist + flMove * t1;

		Ray_t step = ray;
		step.m_Start = ray.m_Start + ray.m_Delta * t0;
		step.m_Delta = ray.m_Delta * ( t1 - t0 );
		if ( flExtent < 0.001f )
		{
			// A point: the step counts whole if it stays on that side.
			if ( bFront ? MIN( flDist0, flDist1 ) < PORTAL_WORLD_SIDE_EPSILON : MAX( flDist0, flDist1 ) > -PORTAL_WORLD_SIDE_EPSILON )
				continue;
		}
		else
		{
			// Along the normal, relative to the box center: [flLo, flHi].
			float flLo = -flExtent, flHi = flExtent;
			if ( bFront )
				flLo = MAX( flLo, PORTAL_WORLD_SIDE_EPSILON - MIN( flDist0, flDist1 ) );
			else
				flHi = MIN( flHi, -PORTAL_WORLD_SIDE_EPSILON - MAX( flDist0, flDist1 ) );
			if ( flLo >= flHi )
				continue;
			step.m_Start[k] += flSign * ( flLo + flHi ) * 0.5f;
			step.m_Extents[k] = ( flHi - flLo ) * 0.5f;
			step.m_IsRay = false;
		}
		step.m_IsSwept = step.m_Delta.LengthSqr() != 0.0f;

		trace_t tr;
		physcollision->TraceBox( step, collide.pCollide, collide.vecOrigin, collide.angAngles, &tr );
		if ( tr.startsolid )
		{
			// Overlapping it where this step starts: stuck from the start for
			// the first step, a hit at the step's start after that.
			*pStartSolid = ( iStep == 0 );
			*pPlane = tr.plane;
			return t0;
		}
		if ( tr.fraction < 1.0f )
		{
			*pPlane = tr.plane;
			return t0 + tr.fraction * ( t1 - t0 );
		}
	}
	return 1.0f;
}

bool HL2MPPortalWorld_Trace( const CHL2MPPortal *pPortal, const Ray_t &ray, unsigned int mask, trace_t *pTrace )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( !pWorld )
		return false;

	memset( pTrace, 0, sizeof( *pTrace ) );
	pTrace->fraction = 1.0f;
	pTrace->fractionleftsolid = 0.0f;

	int k;
	float flSign, flDist;
	if ( !pPortal->GetPlaneAxis( &k, &flSign, &flDist ) )
		return false;

	FOR_EACH_VEC( pWorld->m_Collides, i )
	{
		const PortalWorldCollide_t &collide = pWorld->m_Collides[i];
		if ( !( collide.iContents & mask ) )
			continue;

		bool bStartSolid = false;
		float flFraction;
		cplane_t plane;
		if ( collide.iSide == PORTAL_WORLD_SIDE_BOTH )
		{
			trace_t tr;
			physcollision->TraceBox( ray, collide.pCollide, collide.vecOrigin, collide.angAngles, &tr );
			bStartSolid = tr.startsolid;
			flFraction = tr.startsolid ? 0.0f : tr.fraction;
			plane = tr.plane;
			if ( tr.allsolid )
			{
				pTrace->allsolid = true;
			}
		}
		else
		{
			flFraction = TraceSideCollide( pWorld, collide, ray, k, flSign, &bStartSolid, &plane );
		}

		if ( bStartSolid )
		{
			pTrace->startsolid = true;
			pTrace->fraction = 0.0f;
			pTrace->plane = plane;
			pTrace->contents = collide.iContents;
		}
		else if ( !pTrace->startsolid && flFraction < pTrace->fraction )
		{
			pTrace->fraction = flFraction;
			pTrace->plane = plane;
			pTrace->contents = collide.iContents;
		}
	}

	pTrace->startpos = ray.m_Start + ray.m_StartOffset;
	pTrace->endpos = pTrace->startpos + ray.m_Delta * pTrace->fraction;
	if ( pTrace->fraction < 1.0f || pTrace->startsolid )
	{
#ifdef CLIENT_DLL
		pTrace->m_pEnt = GetClientWorldEntity();
#else
		pTrace->m_pEnt = GetWorldEntity();
#endif
		pTrace->surface.name = "**studio**";
	}
	return true;
}

bool HL2MPPortalWorld_PropsCanCross( const CHL2MPPortal *pPortal, int *pUnsafe )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( pUnsafe )
	{
		*pUnsafe = pWorld ? pWorld->m_nPropUnsafe : 0;
	}
	return pWorld && pWorld->m_bPropsCanCross;
}

bool HL2MPPortalWorld_GetInfo( const CHL2MPPortal *pPortal, int *pLocalPieces, int *pRemotePieces, int *pStaticProps, int *pDisplacements, unsigned int *pChecksum )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( !pWorld )
		return false;
	*pLocalPieces = pWorld->m_nLocalPieces;
	*pRemotePieces = pWorld->m_nRemotePieces;
	*pStaticProps = pWorld->m_nStaticProps;
	*pDisplacements = pWorld->m_nDisplacements;
	*pChecksum = pWorld->m_nChecksum;
	return true;
}

// Print each portal's world (compare the server's and a client's checksums:
// they must match for prediction to agree).
static void PrintPortalWorlds( const CCommand &args )
{
	for ( int i = 0; i < CHL2MPPortal::GetPortalCount(); i++ )
	{
		CHL2MPPortal *pPortal = CHL2MPPortal::GetPortal( i );
		int nLocal, nRemote, nProps, nDisp;
		unsigned int nChecksum;
		if ( !HL2MPPortalWorld_GetInfo( pPortal, &nLocal, &nRemote, &nProps, &nDisp, &nChecksum ) )
		{
			Msg( "portal %d (%s): no local world\n", pPortal->entindex(), pPortal->GetPortalType() == HL2MP_PORTAL_BLUE ? "blue" : "orange" );
			continue;
		}
		int nUnsafe = 0;
		bool bPropsSlide = HL2MPPortalWorld_PropsCanCross( pPortal, &nUnsafe );
		Msg( "portal %d (%s): %d pieces here, %d from the other side, %d static props, %d displacement sets, checksum %08x\n",
			pPortal->entindex(), pPortal->GetPortalType() == HL2MP_PORTAL_BLUE ? "blue" : "orange", nLocal, nRemote, nProps, nDisp, nChecksum );
#ifdef GAME_DLL
		Msg( "  props %s\n", bPropsSlide ? "slide through" : "pop through" );
		if ( nUnsafe )
		{
			Msg( "  %d uncut prop/terrain pieces would be in the way of a sliding prop\n", nUnsafe );
		}
#endif
	}
}

#ifdef CLIENT_DLL
static ConCommand cl_portal_world_info( "cl_portal_world_info", PrintPortalWorlds, "Print each portal's local collision world (as this client built it)." );
#else
static ConCommand sv_portal_world_info( "sv_portal_world_info", PrintPortalWorlds, "Print each portal's local collision world (as the server built it)." );
#endif

#ifdef GAME_DLL
//-----------------------------------------------------------------------------
// Props: the local world as static physics objects, which only props crossing
// through the portal collide with (their collisions with the real world are
// off while they cross). Nothing else touches it: it's not solid, and only
// forces collisions with those props.
//-----------------------------------------------------------------------------
#define PORTAL_WORLD_PROP_CONTENTS	( CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE )

class CHL2MPPortalCollider : public CBaseEntity
{
public:
	DECLARE_CLASS( CHL2MPPortalCollider, CBaseEntity );

	virtual void Spawn()
	{
		BaseClass::Spawn();
		SetSolid( SOLID_NONE );
		SetMoveType( MOVETYPE_NONE );
		AddEffects( EF_NODRAW );
	}

	virtual int UpdateTransmitState() { return SetTransmitState( FL_EDICT_DONTSEND ); }

	virtual bool ForceVPhysicsCollide( CBaseEntity *pEntity )
	{
		return m_hPortal && CHL2MPPortal::IsCrossingThrough( pEntity, m_hPortal );
	}

	virtual void UpdateOnRemove()
	{
		DestroyObjects();
		BaseClass::UpdateOnRemove();
	}

	void DestroyObjects()
	{
		FOR_EACH_VEC( m_Objects, i )
		{
			if ( physenv )
			{
				physenv->DestroyObject( m_Objects[i] );
			}
		}
		m_Objects.RemoveAll();
		m_bBuilt = false;
	}

	void Build( CHL2MPPortalWorld *pWorld )
	{
		DestroyObjects();
		objectparams_t params = g_PhysDefaultObjectParams;
		params.pGameData = static_cast<void *>( this );
		int iSurface = physprops->GetSurfaceIndex( "default" );
		FOR_EACH_VEC( pWorld->m_Collides, i )
		{
			const PortalWorldCollide_t &collide = pWorld->m_Collides[i];
			if ( !( collide.iContents & PORTAL_WORLD_PROP_CONTENTS ) || !collide.bPropSafe )
				continue;
			IPhysicsObject *pObject = physenv->CreatePolyObjectStatic( collide.pCollide, iSurface, collide.vecOrigin, collide.angAngles, &params );
			if ( pObject )
			{
				m_Objects.AddToTail( pObject );
			}
		}
		m_bBuilt = true;
	}

	CHandle<CHL2MPPortal>	m_hPortal;
	CUtlVector<IPhysicsObject *> m_Objects;
	bool					m_bBuilt;
};
LINK_ENTITY_TO_CLASS( hl2mp_portal_collider, CHL2MPPortalCollider );

static void DestroyColliderObjects( CHL2MPPortalWorld *pWorld )
{
	CHL2MPPortalCollider *pCollider = pWorld->m_hCollider;
	if ( pCollider )
	{
		pCollider->DestroyObjects();
	}
}

// Outline every piece of each portal's local world (listen server): this
// side's brush pieces green, the other side's blue, uncut static props and
// displacements yellow (red when props can't use them whole).
static ConVar sv_portal_world_draw( "sv_portal_world_draw", "0", FCVAR_CHEAT, "Draw the outline of every piece of each portal's local collision world." );

class CHL2MPPortalWorldDraw : public CAutoGameSystemPerFrame
{
public:
	CHL2MPPortalWorldDraw() : CAutoGameSystemPerFrame( "CHL2MPPortalWorldDraw" ), m_flNextDraw( 0.0f ) {}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( !sv_portal_world_draw.GetBool() )
			return;
		// curtime starts over on a new map.
		if ( gpGlobals->curtime < m_flNextDraw && m_flNextDraw - gpGlobals->curtime < 1.0f )
			return;
		const float flInterval = 0.25f;
		m_flNextDraw = gpGlobals->curtime + flInterval;
		for ( int i = 0; i < CHL2MPPortal::GetPortalCount(); i++ )
		{
			CHL2MPPortalWorld *pWorld = GetPortalWorld( CHL2MPPortal::GetPortal( i ) );
			if ( !pWorld )
				continue;
			FOR_EACH_VEC( pWorld->m_Boxes, b )
			{
				const PortalWorldBox_t &box = pWorld->m_Boxes[b];
				int r = 0, g = 200, bl = 0;
				if ( box.iSide != PORTAL_WORLD_SIDE_BOTH )
				{
					r = 255; g = box.bPropSafe ? 200 : 0;
				}
				else if ( box.bRemote )
				{
					g = 120; bl = 255;
				}
				NDebugOverlay::Box( vec3_origin, box.vecMins, box.vecMaxs, r, g, bl, 0, flInterval + 0.05f );
			}
		}
	}

private:
	float m_flNextDraw;
};
static CHL2MPPortalWorldDraw g_HL2MPPortalWorldDraw;

int HL2MPPortalWorld_SyncCollider( CHL2MPPortal *pPortal )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( !pWorld )
		return -1;

	CHL2MPPortalCollider *pCollider = pWorld->m_hCollider;
	if ( !pCollider )
	{
		pCollider = static_cast<CHL2MPPortalCollider *>( CreateEntityByName( "hl2mp_portal_collider" ) );
		if ( !pCollider )
			return -1;
		pCollider->m_hPortal = pPortal;
		pCollider->m_bBuilt = false;
		pCollider->SetAbsOrigin( pPortal->GetAbsOrigin() );
		DispatchSpawn( pCollider );
		pWorld->m_hCollider = pCollider;
	}
	if ( !pCollider->m_bBuilt )
	{
		pCollider->Build( pWorld );
	}
	// Uncut props / terrain in the way: the collider is missing them, so
	// nothing may cross with it (a crossing in progress when a portal was
	// re-placed ends here too, instead of losing the floor).
	if ( !pWorld->m_bPropsCanCross )
		return 0;
	return pCollider->m_Objects.Count();
}

void HL2MPPortalWorld_RemoveCollider( CHL2MPPortal *pPortal )
{
	CHL2MPPortalWorld *pWorld = pPortal->LocalWorldStorage();
	if ( pWorld && pWorld->m_hCollider )
	{
		UTIL_Remove( pWorld->m_hCollider );
		pWorld->m_hCollider = NULL;
	}
}
#endif // GAME_DLL
