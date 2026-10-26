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

struct PortalWorldCollide_t
{
	CPhysCollide	*pCollide;
	bool			bOwned;		// built here (brush pieces, displacements); static prop models aren't
	int				iContents;
	Vector			vecOrigin;
	QAngle			angAngles;
};

#ifdef GAME_DLL
class CHL2MPPortalCollider;
static void DestroyColliderObjects( CHL2MPPortalWorld *pWorld );
#endif

class CHL2MPPortalWorld
{
public:
	CHL2MPPortalWorld() : m_nVersion( 0 ), m_nLinkedVersion( 0 ), m_nLocalPieces( 0 ), m_nRemotePieces( 0 ), m_nStaticProps( 0 ), m_nDisplacements( 0 ), m_nChecksum( 0 ) {}
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
		m_nLocalPieces = m_nRemotePieces = m_nStaticProps = m_nDisplacements = 0;
		m_nChecksum = 0;
	}

	int		m_nVersion, m_nLinkedVersion;	// frame versions it was built for
	Vector	m_vecOrigin, m_vecForward, m_vecRight, m_vecUp;
	CUtlVector<PortalWorldCollide_t> m_Collides;
	int		m_nLocalPieces, m_nRemotePieces, m_nStaticProps, m_nDisplacements;
	unsigned int m_nChecksum;
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

static void AddConvex( CUtlVector<PortalWorldGroup_t> &groups, int iContents, CUtlVector<Vector> &verts, unsigned int &nHash )
{
	CUtlVector<Vector *> pointers;
	FOR_EACH_VEC( verts, i )
	{
		pointers.AddToTail( &verts[i] );
		HashVector( nHash, verts[i] );
	}
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

		if ( pProp->GetSolid() == SOLID_VPHYSICS )
		{
			vcollide_t *pVCollide = modelinfo->GetVCollide( pProp->GetCollisionModel() );
			if ( !pVCollide )
				continue;
			for ( int iSolid = 0; iSolid < pVCollide->solidCount; iSolid++ )
			{
				collide.pCollide = pVCollide->solids[iSolid];
				collide.bOwned = false;
				pWorld->m_Collides.AddToTail( collide );
			}
		}
		else
		{
			collide.pCollide = physcollision->BBoxToCollide( pProp->OBBMins(), pProp->OBBMaxs() );
			collide.bOwned = true;
			if ( !collide.pCollide )
				continue;
			pWorld->m_Collides.AddToTail( collide );
		}
		pWorld->m_nStaticProps++;
		HashVector( nHash, vecOrigin );
	}
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
			AddConvex( groups, iContents, verts, nHash );
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
		AddConvex( groups, iContents, verts, nHash );
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
		PortalWorldCollide_t collide = { pDisp, true, CONTENTS_SOLID, vec3_origin, vec3_angle };
		pWorld->m_Collides.AddToTail( collide );
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
		pWorld->m_Collides.AddToTail( collide );
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

bool HL2MPPortalWorld_Trace( const CHL2MPPortal *pPortal, const Ray_t &ray, unsigned int mask, trace_t *pTrace )
{
	CHL2MPPortalWorld *pWorld = GetPortalWorld( pPortal );
	if ( !pWorld )
		return false;

	memset( pTrace, 0, sizeof( *pTrace ) );
	pTrace->fraction = 1.0f;
	pTrace->fractionleftsolid = 0.0f;

	FOR_EACH_VEC( pWorld->m_Collides, i )
	{
		const PortalWorldCollide_t &collide = pWorld->m_Collides[i];
		if ( !( collide.iContents & mask ) )
			continue;

		trace_t tr;
		physcollision->TraceBox( ray, collide.pCollide, collide.vecOrigin, collide.angAngles, &tr );
		if ( tr.startsolid )
		{
			pTrace->startsolid = true;
			pTrace->allsolid = pTrace->allsolid || tr.allsolid;
			pTrace->fraction = 0.0f;
			pTrace->plane = tr.plane;
			pTrace->contents = collide.iContents;
		}
		else if ( !pTrace->startsolid && tr.fraction < pTrace->fraction )
		{
			pTrace->fraction = tr.fraction;
			pTrace->plane = tr.plane;
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
		Msg( "portal %d (%s): %d pieces here, %d from the other side, %d static props, %d displacement sets, checksum %08x\n",
			pPortal->entindex(), pPortal->GetPortalType() == HL2MP_PORTAL_BLUE ? "blue" : "orange", nLocal, nRemote, nProps, nDisp, nChecksum );
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
			if ( !( collide.iContents & PORTAL_WORLD_PROP_CONTENTS ) )
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
