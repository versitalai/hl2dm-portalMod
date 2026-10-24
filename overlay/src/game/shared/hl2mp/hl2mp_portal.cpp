//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linked portal pair for HL2MP (the "portal mod"). See hl2mp_portal.h.
//
//=============================================================================//

#include "cbase.h"
#include "hl2mp_portal.h"
#include "filesystem.h"

#ifdef CLIENT_DLL
	#include "materialsystem/imesh.h"
	#include "materialsystem/imaterial.h"
	#include "materialsystem/imaterialvar.h"
	#include "KeyValues.h"
	#include "viewrender.h"
	#include "view.h"
	#include "c_world.h"
	#include "hl2mp_hud_chat.h"
#else
	#include "world.h"
	#include "vphysics/player_controller.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Tuning
//-----------------------------------------------------------------------------
#ifdef GAME_DLL
ConVar sv_portal_debug( "sv_portal_debug", "0", FCVAR_CHEAT, "Draw portal teleport volumes and placement traces." );
ConVar sv_portal_lookahead( "sv_portal_lookahead", "3", FCVAR_NONE, "How many ticks ahead fast movers are caught before they would hit the portal surface." );
ConVar sv_portal_cooldown( "sv_portal_cooldown", "0.05", FCVAR_NONE, "Seconds before an entity that just went through a portal can go through again." );
#endif

// The release this build came from, from version.txt (written by
// scripts/package.sh). Servers publish theirs so players can tell when their
// copy is out of date.
ConVar sv_portal_mod_version( "sv_portal_mod_version", "", FCVAR_REPLICATED | FCVAR_DONTRECORD, "HL2DM Portal release the server runs (from its version.txt)." );

const char *UTIL_HL2MPPortal_GetModVersion()
{
	static char s_szVersion[64] = "";
	if ( !s_szVersion[0] )
	{
		FileHandle_t hFile = filesystem->Open( "version.txt", "rb", "MOD" );
		if ( hFile )
		{
			int nRead = filesystem->Read( s_szVersion, sizeof( s_szVersion ) - 1, hFile );
			filesystem->Close( hFile );
			s_szVersion[MAX( nRead, 0 )] = '\0';
			Q_StripPrecedingAndTrailingWhitespace( s_szVersion );
		}
		if ( !s_szVersion[0] )
		{
			Q_strncpy( s_szVersion, "dev", sizeof( s_szVersion ) );
		}
	}
	return s_szVersion;
}

#ifdef GAME_DLL
void UTIL_HL2MPPortal_PublishModVersion()
{
	sv_portal_mod_version.SetValue( UTIL_HL2MPPortal_GetModVersion() );
}
#else
void UTIL_HL2MPPortal_CheckServerVersion()
{
	// Once per server version per game session; a server that predates this
	// (empty) or a test build on either end ("dev", a commit) says nothing.
	static char s_szWarned[64] = "";
	const char *pszServer = sv_portal_mod_version.GetString();
	const char *pszMine = UTIL_HL2MPPortal_GetModVersion();
	if ( pszServer[0] != 'v' || pszMine[0] != 'v' || !Q_strcmp( pszServer, pszMine ) || !Q_strcmp( pszServer, s_szWarned ) )
		return;
	Q_strncpy( s_szWarned, pszServer, sizeof( s_szWarned ) );

	char szMessage[256];
	Q_snprintf( szMessage, sizeof( szMessage ), "This server runs HL2DM Portal %s, you have %s. Quit and start the game from the HL2DM Portal desktop shortcut to update (or run update.bat in the mod folder).", pszServer, pszMine );
	Msg( "%s\n", szMessage );
	CBaseHudChat *pChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );
	if ( pChat )
	{
		pChat->ChatPrintf( 0, CHAT_FILTER_NONE, "%s", szMessage );
	}
}
#endif

#ifdef CLIENT_DLL
ConVar cl_portal_render_views( "cl_portal_render_views", "1", FCVAR_ARCHIVE, "Render the view through portals (0 = draw portals as solid color)." );
#endif

// How far in front of the exit plane things are placed (must exceed TOUCH_DIST).
#define PORTAL_EXIT_GAP			2.0f
// An entity this close to the plane and not moving away goes through.
#define PORTAL_TOUCH_DIST		1.0f
// How far past the plane a held object's hold point must be before it goes through.
#define PORTAL_HELD_THROUGH_DIST	2.0f
// Depth of the detection volume in front of / behind the plane.
#define PORTAL_DETECT_FRONT		160.0f
#define PORTAL_DETECT_BACK		8.0f
// Mesh offset off the wall to avoid z-fighting.
#define PORTAL_RENDER_OFFSET	0.8f

//-----------------------------------------------------------------------------
// Global portal list
//-----------------------------------------------------------------------------
static CUtlVector<CHL2MPPortal *> s_HL2MPPortals;

int CHL2MPPortal::GetPortalCount()
{
	return s_HL2MPPortals.Count();
}

CHL2MPPortal *CHL2MPPortal::GetPortal( int i )
{
	return s_HL2MPPortals[i];
}

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( prop_hl2mp_portal, CHL2MPPortal );

IMPLEMENT_NETWORKCLASS_ALIASED( HL2MPPortal, DT_HL2MPPortal )

BEGIN_NETWORK_TABLE( CHL2MPPortal, DT_HL2MPPortal )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bActivated ) ),
	RecvPropInt( RECVINFO( m_iPortalType ) ),
	RecvPropEHandle( RECVINFO( m_hLinkedPortal ) ),
#else
	SendPropBool( SENDINFO( m_bActivated ) ),
	SendPropInt( SENDINFO( m_iPortalType ), 2, SPROP_UNSIGNED ),
	SendPropEHandle( SENDINFO( m_hLinkedPortal ) ),
#endif
END_NETWORK_TABLE()

//-----------------------------------------------------------------------------
// Shared
//-----------------------------------------------------------------------------
CHL2MPPortal::CHL2MPPortal()
{
	m_bActivated = false;
	m_iPortalType = HL2MP_PORTAL_BLUE;
	m_hLinkedPortal = NULL;
	m_nFrameVersion = 0;
	m_nMatVersion = m_nMatLinkedVersion = 0;
	m_nRoomTick = -1;
	m_pRoomIgnore = NULL;
	m_vecRoomHalf.Init();
	m_bRoom = true;
	m_iCacheAxis = -1;
	m_flCacheAxisSign = 1.0f;
#ifdef CLIENT_DLL
	for ( int i = 0; i < HL2MP_PORTAL_MAX_DEPTH; i++ )
	{
		m_iViewSlot[i] = -1;
		m_iViewStamp[i] = -1;
	}
	m_flTunnelDepth = 0.0f;
	m_nTunnelVersion = 0;
#endif
	s_HL2MPPortals.AddToTail( this );
}

CHL2MPPortal::~CHL2MPPortal()
{
	s_HL2MPPortals.FindAndRemove( this );
}

void CHL2MPPortal::Spawn( void )
{
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );

#ifdef GAME_DLL
	SetThink( &CHL2MPPortal::PortalThink );
	SetNextThink( gpGlobals->curtime );
#endif
}

bool CHL2MPPortal::IsOpen() const
{
	CHL2MPPortal *pLinked = GetLinkedPortal();
	return m_bActivated && pLinked && pLinked->IsActivated();
}

void CHL2MPPortal::UpdateFrameCache() const
{
	const Vector &vOrigin = GetAbsOrigin();
	const QAngle &angles = GetAbsAngles();
	if ( m_nFrameVersion != 0 && vOrigin == m_vecCacheOrigin && angles == m_angCacheAngles )
		return;

	m_vecCacheOrigin = vOrigin;
	m_angCacheAngles = angles;
	AngleVectors( angles, &m_vecCacheForward, &m_vecCacheRight, &m_vecCacheUp );

	m_iCacheAxis = -1;
	for ( int i = 0; i < 3; i++ )
	{
		if ( fabs( m_vecCacheForward[i] ) > 0.999f )
		{
			m_iCacheAxis = i;
			m_flCacheAxisSign = ( m_vecCacheForward[i] > 0.0f ) ? 1.0f : -1.0f;
			break;
		}
	}

	// Global counter: a portal freed and re-created at the same address never
	// matches a matrix cached for the old one.
	static int s_nFrameVersions = 0;
	m_nFrameVersion = ++s_nFrameVersions;
}

void CHL2MPPortal::GetPortalVectors( Vector *pForward, Vector *pRight, Vector *pUp ) const
{
	UpdateFrameCache();
	if ( pForward )
		*pForward = m_vecCacheForward;
	if ( pRight )
		*pRight = m_vecCacheRight;
	if ( pUp )
		*pUp = m_vecCacheUp;
}

Vector CHL2MPPortal::GetSurfaceNormal() const
{
	UpdateFrameCache();
	return m_vecCacheForward;
}

VMatrix CHL2MPPortal::MatrixThisToLinked() const
{
	VMatrix matResult;
	matResult.Identity();

	CHL2MPPortal *pLinked = GetLinkedPortal();
	if ( !pLinked )
		return matResult;

	UpdateFrameCache();
	pLinked->UpdateFrameCache();
	if ( m_nMatVersion == m_nFrameVersion && m_nMatLinkedVersion == pLinked->m_nFrameVersion )
		return m_matCacheToLinked;

	VMatrix matThisToWorld, matWorldToThis, matLinkedToWorld, matRotate;
	matThisToWorld.SetupMatrixOrgAngles( GetAbsOrigin(), GetAbsAngles() );
	matLinkedToWorld.SetupMatrixOrgAngles( pLinked->GetAbsOrigin(), pLinked->GetAbsAngles() );
	MatrixInverseTR( matThisToWorld, matWorldToThis );

	// Half turn around local up: in through the front, out through the front.
	matRotate.Identity();
	matRotate.m[0][0] = -1.0f;
	matRotate.m[1][1] = -1.0f;

	matResult = matLinkedToWorld * matRotate * matWorldToThis;

	m_matCacheToLinked = matResult;
	m_nMatVersion = m_nFrameVersion;
	m_nMatLinkedVersion = pLinked->m_nFrameVersion;
	return matResult;
}

bool CHL2MPPortal::IsPointInEllipse( const Vector &vPoint, float flScale ) const
{
	UpdateFrameCache();
	Vector vDelta = vPoint - m_vecCacheOrigin;
	float flX = DotProduct( vDelta, m_vecCacheRight ) / ( HL2MP_PORTAL_HALF_WIDTH * flScale );
	float flY = DotProduct( vDelta, m_vecCacheUp ) / ( HL2MP_PORTAL_HALF_HEIGHT * flScale );
	return ( flX * flX + flY * flY ) <= 1.0f;
}

Vector CHL2MPPortal::ClampBehindPointToHole( const Vector &vPoint, float flScale ) const
{
	UpdateFrameCache();
	Vector vDelta = vPoint - m_vecCacheOrigin;
	if ( DotProduct( vDelta, m_vecCacheForward ) >= 0.0f )
		return vPoint;

	float flX = DotProduct( vDelta, m_vecCacheRight ) / ( HL2MP_PORTAL_HALF_WIDTH * flScale );
	float flY = DotProduct( vDelta, m_vecCacheUp ) / ( HL2MP_PORTAL_HALF_HEIGHT * flScale );
	float flLen = sqrtf( flX * flX + flY * flY );
	if ( flLen <= 1.0f )
		return vPoint;

	float flShrink = 1.0f / flLen - 1.0f;
	return vPoint + m_vecCacheRight * ( flX * HL2MP_PORTAL_HALF_WIDTH * flScale * flShrink ) +
		m_vecCacheUp * ( flY * HL2MP_PORTAL_HALF_HEIGHT * flScale * flShrink );
}

CHL2MPPortal *CHL2MPPortal::FindPortalAtSurface( const Vector &vPoint, const Vector &vNormal, float flPlaneTolerance, CHL2MPPortal *pIgnore )
{
	for ( int i = 0; i < s_HL2MPPortals.Count(); i++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[i];
		if ( pPortal == pIgnore || !pPortal->IsOpen() )
			continue;

#ifdef CLIENT_DLL
		if ( pPortal->IsDormant() || pPortal->GetLinkedPortal()->IsDormant() )
			continue;
#endif

		Vector vPortalNormal = pPortal->GetSurfaceNormal();
		if ( DotProduct( vPortalNormal, vNormal ) < 0.95f )
			continue;

		if ( fabs( DotProduct( vPortalNormal, vPoint - pPortal->GetAbsOrigin() ) ) > flPlaneTolerance )
			continue;

		if ( !pPortal->IsPointInEllipse( vPoint ) )
			continue;

		return pPortal;
	}

	return NULL;
}

bool CHL2MPPortal::GetPlaneAxis( int *pAxis, float *pSign, float *pDist ) const
{
	UpdateFrameCache();
	if ( m_iCacheAxis < 0 )
		return false;

	*pAxis = m_iCacheAxis;
	*pSign = m_flCacheAxisSign;
	*pDist = m_vecCacheOrigin[m_iCacheAxis];
	return true;
}

bool CHL2MPPortal::CanCarve() const
{
	int iAxis;
	float flSign, flDist;
	CHL2MPPortal *pLinked = GetLinkedPortal();
	return IsOpen() && GetPlaneAxis( &iAxis, &flSign, &flDist ) && pLinked->GetPlaneAxis( &iAxis, &flSign, &flDist );
}

// Hull back face clearance from the plane (negative = sticking through).
static float PortalHullGap( const Vector &vecCenter, const Vector &vecHalf, int iAxis, float flSign, float flDist )
{
	return flSign * ( vecCenter[iAxis] - flDist ) - vecHalf[iAxis];
}

// Does the hull's cross-section fit inside the hole? For movement the hole is
// the portal's bounding rectangle: an upright 32x72 player hull only fits the
// 64x112 ellipse when centered within ~1.5 units, which is unplayable.
static bool PortalHullFootprintInside( const CHL2MPPortal *pPortal, const Vector &vecCenter, const Vector &vecHalf, int iAxis, float flDist )
{
	Vector vecRight, vecUp;
	pPortal->GetPortalVectors( NULL, &vecRight, &vecUp );

	int a = ( iAxis + 1 ) % 3;
	int b = ( iAxis + 2 ) % 3;
	for ( int i = 0; i < 4; i++ )
	{
		Vector vecCorner = vecCenter;
		vecCorner[iAxis] = flDist;
		vecCorner[a] += ( i & 1 ) ? vecHalf[a] : -vecHalf[a];
		vecCorner[b] += ( i & 2 ) ? vecHalf[b] : -vecHalf[b];

		Vector vecDelta = vecCorner - pPortal->GetAbsOrigin();
		// 1 unit of slack: a hull standing on the floor at a floor-level portal
		// sits right on its bottom edge.
		if ( fabs( DotProduct( vecDelta, vecRight ) ) > HL2MP_PORTAL_HALF_WIDTH + 1.0f ||
			fabs( DotProduct( vecDelta, vecUp ) ) > HL2MP_PORTAL_HALF_HEIGHT + 1.0f )
			return false;
	}
	return true;
}

bool CHL2MPPortal::HullFitsHole( const Vector &vecCenter, const Vector &vecHalf ) const
{
	int k;
	float flSign, flDist;
	return GetPlaneAxis( &k, &flSign, &flDist ) && PortalHullFootprintInside( this, vecCenter, vecHalf, k, flDist );
}

// Extent of an axis-aligned box (half sizes) along a direction.
static float BoxSupport( const Vector &vHalf, const Vector &vDir )
{
	return fabs( vDir.x ) * vHalf.x + fabs( vDir.y ) * vHalf.y + fabs( vDir.z ) * vHalf.z;
}

bool CHL2MPPortal::GetHoleFitShift( const Vector &vecCenter, const Vector &vecHalf, Vector *pShift ) const
{
	int k;
	float flSign, flDist;
	if ( !GetPlaneAxis( &k, &flSign, &flDist ) )
		return false;

	// Room for the center (same rectangle as the collision carving, a little
	// inside its 1 unit of slack).
	Vector vecRight, vecUp;
	GetPortalVectors( NULL, &vecRight, &vecUp );
	Vector vecPlanarHalf = vecHalf;
	vecPlanarHalf[k] = 0.0f;
	float flRoomRight = HL2MP_PORTAL_HALF_WIDTH + 0.75f - BoxSupport( vecPlanarHalf, vecRight );
	float flRoomUp = HL2MP_PORTAL_HALF_HEIGHT + 0.75f - BoxSupport( vecPlanarHalf, vecUp );
	if ( flRoomRight < 0.0f || flRoomUp < 0.0f )
		return false;

	Vector vecDelta = vecCenter - GetAbsOrigin();
	float flX = DotProduct( vecDelta, vecRight );
	float flY = DotProduct( vecDelta, vecUp );
	*pShift = vecRight * ( clamp( flX, -flRoomRight, flRoomRight ) - flX ) +
		vecUp * ( clamp( flY, -flRoomUp, flRoomUp ) - flY );
	( *pShift )[k] = 0.0f;
	return true;
}

// What can block a portal's exit: anything a player can't push out of the
// way. Loose physics props (a can, a held box) never do.
class CPortalExitFilter : public CTraceFilterSimple
{
public:
	CPortalExitFilter( const IHandleEntity *pIgnore ) : CTraceFilterSimple( pIgnore, COLLISION_GROUP_PLAYER_MOVEMENT ) {}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity && pEntity->GetMoveType() == MOVETYPE_VPHYSICS )
			return false;
		return CTraceFilterSimple::ShouldHitEntity( pHandleEntity, contentsMask );
	}
};

bool CHL2MPPortal::HasRoomForHull( const Vector &vecMins, const Vector &vecMaxs, IHandleEntity *pIgnore ) const
{
	Vector vecHalf = ( vecMaxs - vecMins ) * 0.5f;
	if ( m_nRoomTick == gpGlobals->tickcount && m_pRoomIgnore == pIgnore && m_vecRoomHalf == vecHalf )
		return m_bRoom;

	// Centered on the hole, just clear of the surface.
	Vector vecNormal = GetSurfaceNormal();
	Vector vecCenter = GetAbsOrigin() + vecNormal * ( BoxSupport( vecHalf, vecNormal ) + 1.0f );
	trace_t tr;
	CPortalExitFilter filter( pIgnore );
	UTIL_TraceHull( vecCenter, vecCenter, -vecHalf, vecHalf, MASK_PLAYERSOLID, &filter, &tr );

	m_nRoomTick = gpGlobals->tickcount;
	m_pRoomIgnore = pIgnore;
	m_vecRoomHalf = vecHalf;
	m_bRoom = !tr.startsolid;
	return m_bRoom;
}

Vector UTIL_HL2MPPortal_PlayerFunnelMove( const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, const Vector &vecVelocity, float flFrameTime )
{
	Vector vecMid = ( vecMins + vecMaxs ) * 0.5f;
	Vector vecHalf = ( vecMaxs - vecMins ) * 0.5f;
	Vector vecCenter = vecOrigin + vecMid;

	for ( int iPortal = 0; iPortal < s_HL2MPPortals.Count(); iPortal++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[iPortal];
		if ( !pPortal->CanCarve() )
			continue;
#ifdef CLIENT_DLL
		if ( pPortal->IsDormant() || pPortal->GetLinkedPortal()->IsDormant() )
			continue;
#endif

		int k;
		float flSign, flDist;
		pPortal->GetPlaneAxis( &k, &flSign, &flDist );

		// Already into the hole: the carved collision takes it from here.
		float flGap = PortalHullGap( vecCenter, vecHalf, k, flSign, flDist );
		if ( flGap < -1.0f )
			continue;

		// Heading into it: moving toward the plane, or standing on a floor portal.
		float flApproach = -flSign * vecVelocity[k];
		bool bStanding = ( flApproach < 1.0f );
		if ( bStanding && !( k == 2 && flSign > 0.0f ) )
			continue;
		float flLookahead = MAX( 2.0f, flApproach * 0.25f );
		if ( flGap > flLookahead )
			continue;

		// Only when aimed at the hole: the hull's center over the portal. Someone
		// standing on a floor portal has to be well over it: its edge is
		// somewhere to stand, not a slope into the hole.
		Vector vecOnPlane = vecCenter;
		vecOnPlane[k] = flDist;
		if ( !pPortal->IsPointInEllipse( vecOnPlane, bStanding ? 0.8f : 1.1f ) )
			continue;

		Vector vecShift;
		if ( !pPortal->GetHoleFitShift( vecOnPlane, vecHalf, &vecShift ) )
			continue;	// never fits
		float flShift = vecShift.Length();
		if ( flShift < 0.01f )
			return vec3_origin;		// lined up

		// Walking off a floor portal's edge: let them (pulling them back in
		// trapped players there until they jumped).
		Vector vecPlanarVelocity = vecVelocity;
		vecPlanarVelocity[k] = 0.0f;
		if ( bStanding && DotProduct( vecPlanarVelocity, vecShift ) < 0.0f && vecPlanarVelocity.LengthSqr() > 10.0f * 10.0f )
			continue;

		// Get there before reaching the plane, but no faster than a quick
		// sidestep; standing still, a slow slide.
		float flTimeToPlane = bStanding ? 0.0f : MAX( flGap, 0.0f ) / flApproach;
		float flSpeed = bStanding ? 80.0f : clamp( flShift / MAX( flTimeToPlane, flFrameTime ), 150.0f, 600.0f );
		float flStep = MIN( flShift, flSpeed * flFrameTime );
		return vecShift * ( flStep / flShift );
	}

	return vec3_origin;
}

#define PORTAL_CARVE_EPSILON	0.1f

bool UTIL_HL2MPPortal_TracePlayerHull( const Vector &vecStart, const Vector &vecEnd, const Vector &vecMins, const Vector &vecMaxs,
	unsigned int mask, IHandleEntity *pIgnore, int collisionGroup, trace_t &pm )
{
	Vector vecMid = ( vecMins + vecMaxs ) * 0.5f;
	Vector vecHalf = ( vecMaxs - vecMins ) * 0.5f;
	Vector vecCenterStart = vecStart + vecMid;
	Vector vecCenterEnd = vecEnd + vecMid;

	for ( int iPortal = 0; iPortal < s_HL2MPPortals.Count(); iPortal++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[iPortal];
		if ( !pPortal->CanCarve() )
			continue;
#ifdef CLIENT_DLL
		if ( pPortal->IsDormant() || pPortal->GetLinkedPortal()->IsDormant() )
			continue;
#endif

		int k;
		float flSign, flDist;
		pPortal->GetPlaneAxis( &k, &flSign, &flDist );

		float flGapStart = PortalHullGap( vecCenterStart, vecHalf, k, flSign, flDist );
		float flGapEnd = PortalHullGap( vecCenterEnd, vecHalf, k, flSign, flDist );

		// Not touching the plane at either end, or entirely behind it.
		if ( MIN( flGapStart, flGapEnd ) > 1.0f )
			continue;
		if ( flGapStart < -2.0f * vecHalf[k] && flGapEnd < -2.0f * vecHalf[k] )
			continue;

		bool bInsideStart = PortalHullFootprintInside( pPortal, vecCenterStart, vecHalf, k, flDist );
		bool bInsideEnd = PortalHullFootprintInside( pPortal, vecCenterEnd, vecHalf, k, flDist );
		if ( !bInsideStart && !bInsideEnd )
			continue;	// against the wall next to the hole: normal collision

		// Nowhere to come out (something in front of the other portal): the
		// portal is a solid surface. Once partly through, carry on; the exit
		// pushes the hull clear.
		if ( flGapStart >= -PORTAL_CARVE_EPSILON && !pPortal->GetLinkedPortal()->HasRoomForHull( vecMins, vecMaxs, pIgnore ) )
			continue;

		// A position sticking through the plane outside the hole is inside the
		// rim of the wall around it.
		bool bValidStart = ( flGapStart >= -PORTAL_CARVE_EPSILON ) || bInsideStart;
		bool bValidEnd = ( flGapEnd >= -PORTAL_CARVE_EPSILON ) || bInsideEnd;
		if ( !bValidStart )
			continue;	// already stuck in the rim; let normal collision deal with it

		pm = trace_t();
		pm.startpos = vecStart;
		pm.endpos = vecStart;
		pm.fraction = 1.0f;

		if ( !bValidEnd )
		{
			// Not through the plane yet: it's the wall around the hole, which a
			// normal trace handles (with a real plane to slide along).
			if ( flGapStart >= -PORTAL_CARVE_EPSILON )
				continue;

			// Through the hole and moving sideways into the rim: stop, pushing
			// back toward the hole. Never leave a zero normal: that freezes the
			// player (nothing to clip the velocity against).
			Vector vecLateral = vecEnd - vecStart;
			vecLateral[k] = 0.0f;
			if ( VectorNormalize( vecLateral ) < 1e-4f )
			{
				vecLateral = vec3_origin;
				vecLateral[k] = -flSign;	// normal points out of the wall
			}
			pm.fraction = 0.0f;
			pm.plane.normal = -vecLateral;
#ifdef CLIENT_DLL
			pm.m_pEnt = GetClientWorldEntity();
#else
			pm.m_pEnt = GetWorldEntity();
#endif
			pm.contents = CONTENTS_SOLID;
			return true;
		}

		// Part in front of the plane: clip the hull's back at the plane (for the
		// deepest point of the sweep) and trace normally.
		Vector vecFrontMins = vecMins, vecFrontMaxs = vecMaxs;
		if ( flSign > 0.0f )
			vecFrontMins[k] = MAX( vecMins[k], flDist + PORTAL_CARVE_EPSILON - MIN( vecStart[k], vecEnd[k] ) );
		else
			vecFrontMaxs[k] = MIN( vecMaxs[k], flDist - PORTAL_CARVE_EPSILON - MAX( vecStart[k], vecEnd[k] ) );

		trace_t trFront;
		memset( &trFront, 0, sizeof( trFront ) );
		if ( vecFrontMins[k] < vecFrontMaxs[k] )
		{
			Ray_t ray;
			ray.Init( vecStart, vecEnd, vecFrontMins, vecFrontMaxs );
			UTIL_TraceRay( ray, mask, pIgnore, collisionGroup, &trFront );
		}
		else
		{
			trFront.fraction = 1.0f;
		}

		// Part through the hole (for the shallowest point of the sweep) collides
		// on the far side of the linked portal.
		Vector vecBackMins = vecMins, vecBackMaxs = vecMaxs;
		if ( flSign > 0.0f )
			vecBackMaxs[k] = MIN( vecMaxs[k], flDist - PORTAL_CARVE_EPSILON - MAX( vecStart[k], vecEnd[k] ) );
		else
			vecBackMins[k] = MAX( vecMins[k], flDist + PORTAL_CARVE_EPSILON - MIN( vecStart[k], vecEnd[k] ) );

		trace_t trBack;
		memset( &trBack, 0, sizeof( trBack ) );
		trBack.fraction = 1.0f;
		VMatrix matToLinked = pPortal->MatrixThisToLinked();
		if ( vecBackMins[k] < vecBackMaxs[k] )
		{
			Vector vecBackMid = ( vecBackMins + vecBackMaxs ) * 0.5f;
			Vector vecBackHalf = ( vecBackMaxs - vecBackMins ) * 0.5f;

			// The box rotated by the portal transform, as an axis-aligned box.
			Vector vecRemoteHalf;
			for ( int i = 0; i < 3; i++ )
			{
				vecRemoteHalf[i] = fabs( matToLinked.m[i][0] ) * vecBackHalf.x + fabs( matToLinked.m[i][1] ) * vecBackHalf.y + fabs( matToLinked.m[i][2] ) * vecBackHalf.z;
			}

			Ray_t ray;
			ray.Init( matToLinked.VMul4x3( vecStart + vecBackMid ), matToLinked.VMul4x3( vecEnd + vecBackMid ), -vecRemoteHalf, vecRemoteHalf );
			UTIL_TraceRay( ray, mask, pIgnore, collisionGroup, &trBack );
		}

		bool bBackFirst = trBack.startsolid || ( !trFront.startsolid && trBack.fraction < trFront.fraction );
		trace_t &trHit = bBackFirst ? trBack : trFront;

		pm = trHit;
		pm.startpos = vecStart;
		pm.endpos = vecStart + ( vecEnd - vecStart ) * trHit.fraction;
		if ( bBackFirst )
		{
			// Normal back into this side's space.
			pm.plane.normal = matToLinked.VMul3x3Transpose( trBack.plane.normal );
		}
		return true;
	}

	return false;
}

CHL2MPPortal *UTIL_HL2MPPortal_FindPlayerTransition( const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, const Vector &vecEyeStart, const Vector &vecEye )
{
	Vector vecCenter = vecOrigin + ( vecMins + vecMaxs ) * 0.5f;

	for ( int iPortal = 0; iPortal < s_HL2MPPortals.Count(); iPortal++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[iPortal];
		if ( !pPortal->CanCarve() )
			continue;
#ifdef CLIENT_DLL
		if ( pPortal->IsDormant() || pPortal->GetLinkedPortal()->IsDormant() )
			continue;
#endif

		int k;
		float flSign, flDist;
		pPortal->GetPlaneAxis( &k, &flSign, &flDist );

		// The eye crossed the plane during this move.
		if ( flSign * ( vecEyeStart[k] - flDist ) < 0.0f || flSign * ( vecEye[k] - flDist ) >= 0.0f )
			continue;

		// Same hole test as the collision carving, or the player could end up
		// behind the plane without going through.
		if ( !pPortal->HullFitsHole( vecCenter, ( vecMaxs - vecMins ) * 0.5f ) )
			continue;

		return pPortal;
	}

	return NULL;
}

int UTIL_HL2MPPortal_TraceRay( const Vector &vecStart, const Vector &vecDir, float flDist,
	unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace, Vector *pVecDirOut,
	const Vector *pHullMins, const Vector *pHullMaxs, CHL2MPPortal *pIgnorePortal, CUtlVector<Vector> *pPath, ITraceFilter *pFilterAfterPortal )
{
	if ( pPath )
	{
		pPath->RemoveAll();
	}

	Vector vStart = vecStart;
	Vector vDir = vecDir;
	float flRemaining = flDist;
	int nPortals = 0;

	// Hull traces stop with their center off the wall.
	float flTolerance = 2.0f;
	if ( pHullMins && pHullMaxs )
	{
		flTolerance += MAX( pHullMaxs->x - pHullMins->x, MAX( pHullMaxs->y - pHullMins->y, pHullMaxs->z - pHullMins->z ) );
	}

	for ( ;; )
	{
		Vector vEnd = vStart + vDir * flRemaining;
		if ( pHullMins && pHullMaxs )
		{
			UTIL_TraceHull( vStart, vEnd, *pHullMins, *pHullMaxs, mask, pFilter, pTrace );
		}
		else
		{
			UTIL_TraceLine( vStart, vEnd, mask, pFilter, pTrace );
		}

		if ( nPortals == 0 && pFilterAfterPortal )
		{
			// Every segment after the first uses the other filter.
			pFilter = pFilterAfterPortal;
		}

		if ( pPath )
		{
			pPath->AddToTail( vStart );
		}

		CHL2MPPortal *pPortal = NULL;
		if ( nPortals < HL2MP_PORTAL_MAX_RECURSION && pTrace->fraction < 1.0f && !pTrace->startsolid && pTrace->DidHitWorld() )
		{
			pPortal = CHL2MPPortal::FindPortalAtSurface( pTrace->endpos, pTrace->plane.normal, flTolerance, pIgnorePortal );
		}

		if ( !pPortal )
		{
			if ( pPath )
			{
				pPath->AddToTail( pTrace->endpos );
			}
			break;
		}

		// Bring the hit point onto the portal plane, then out of the linked portal.
		Vector vNormal = pPortal->GetSurfaceNormal();
		Vector vHit = pTrace->endpos - vNormal * DotProduct( vNormal, pTrace->endpos - pPortal->GetAbsOrigin() );

		if ( pPath )
		{
			pPath->AddToTail( vHit );
		}

		VMatrix matToLinked = pPortal->MatrixThisToLinked();
		CHL2MPPortal *pLinked = pPortal->GetLinkedPortal();

		flRemaining -= flRemaining * pTrace->fraction;
		vDir = matToLinked.VMul3x3( vDir );
		vStart = matToLinked.VMul4x3( vHit ) + pLinked->GetSurfaceNormal() * 1.0f;
		nPortals++;
	}

	if ( pVecDirOut )
	{
		*pVecDirOut = vDir;
	}

	return nPortals;
}

//=============================================================================
// Server
//=============================================================================
#ifdef GAME_DLL

static CUtlVector<EHANDLE> s_TeleportLog;

int CHL2MPPortal::CountTeleports( CBaseEntity *pEntity )
{
	int nCount = 0;
	for ( int i = 0; i < s_TeleportLog.Count(); i++ )
	{
		if ( s_TeleportLog[i] == pEntity )
			nCount++;
	}
	return nCount;
}

bool CHL2MPPortal::WasTeleported( CBaseEntity *pEntity )
{
	for ( int i = 0; i < s_TeleportLog.Count(); i++ )
	{
		if ( s_TeleportLog[i] == pEntity )
			return true;
	}
	return false;
}

// Per-entity teleport cooldown, keyed by edict index + handle serial.
static float s_flPortalNextTeleport[MAX_EDICTS];
static int s_iPortalSerial[MAX_EDICTS];

static bool PortalCooldownActive( CBaseEntity *pEntity )
{
	int iIndex = pEntity->entindex();
	if ( iIndex < 0 || iIndex >= MAX_EDICTS )
		return false;

	if ( s_iPortalSerial[iIndex] != pEntity->GetRefEHandle().GetSerialNumber() )
		return false;

	return gpGlobals->curtime < s_flPortalNextTeleport[iIndex];
}

static void PortalStartCooldown( CBaseEntity *pEntity )
{
	int iIndex = pEntity->entindex();
	if ( iIndex < 0 || iIndex >= MAX_EDICTS )
		return;

	s_iPortalSerial[iIndex] = pEntity->GetRefEHandle().GetSerialNumber();
	s_flPortalNextTeleport[iIndex] = gpGlobals->curtime + sv_portal_cooldown.GetFloat();
}

// Velocity of anything, including vphysics objects (whose real velocity
// lives in the physics object).
static void GetPortalEntityVelocity( CBaseEntity *pEntity, Vector *pVelocity, AngularImpulse *pAngVelocity )
{
	pAngVelocity->Init();

	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pPhys )
	{
		pPhys->GetVelocity( pVelocity, pAngVelocity );
	}
	else
	{
		*pVelocity = pEntity->GetAbsVelocity();
	}
}

// Keep held objects held through portals (weapon_physcannon.cpp).
extern bool PhysCannon_GetHeldObjectTarget( CBaseEntity *pObject, Vector *pTarget );
extern void PhysCannon_HeldObjectWentThroughPortal( CBaseEntity *pObject, CHL2MPPortal *pPortal );
extern void PhysCannon_HolderWentThroughPortal( CBasePlayer *pPlayer, CHL2MPPortal *pPortal );


int CHL2MPPortal::UpdateTransmitState( void )
{
	// Clients need both ends of every pair to render and predict through portals.
	return SetTransmitState( FL_EDICT_ALWAYS );
}

bool CHL2MPPortal::PlaceOnSurface( const Vector &vHitPos, const Vector &vNormal, const Vector &vViewForward )
{
	// Walls: portal up is world up. Floors/ceilings: portal up points along the shooter's view.
	Vector vUp;
	if ( fabs( vNormal.z ) < 0.7f )
	{
		vUp = Vector( 0, 0, 1 ) - vNormal * vNormal.z;
	}
	else
	{
		vUp = vViewForward - vNormal * DotProduct( vViewForward, vNormal );
		if ( vUp.LengthSqr() < 0.001f )
		{
			vUp = Vector( 1, 0, 0 ) - vNormal * vNormal.x;
		}
	}
	VectorNormalize( vUp );

	QAngle angPortal;
	VectorAngles( vNormal, vUp, angPortal );

	Vector vForward, vRight;
	AngleVectors( angPortal, &vForward, &vRight, &vUp );

	CHL2MPPortal *pLinked = GetLinkedPortal();

	// Slide the portal away from edges, corners and the partner until it fits.
	const int nSamples = 16;
	const int nMaxIterations = 12;
	const float flStep = 6.0f;

	Vector vCenter = vHitPos;
	bool bFits = false;

	for ( int iIteration = 0; iIteration < nMaxIterations; iIteration++ )
	{
		Vector vPush( 0, 0, 0 );
		bFits = true;

		for ( int i = 0; i <= nSamples; i++ )
		{
			Vector vSample = vCenter;
			if ( i < nSamples )
			{
				float flAngle = ( 2.0f * M_PI * i ) / nSamples;
				vSample += vRight * ( cos( flAngle ) * HL2MP_PORTAL_HALF_WIDTH ) + vUp * ( sin( flAngle ) * HL2MP_PORTAL_HALF_HEIGHT );
			}

			// Needs a flat world surface behind the sample...
			trace_t tr;
			UTIL_TraceLine( vSample + vForward * 4.0f, vSample - vForward * 4.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );

			bool bGood = !tr.startsolid && tr.fraction < 1.0f && tr.DidHitWorld() &&
				!( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) &&
				DotProduct( tr.plane.normal, vForward ) > 0.98f &&
				fabs( DotProduct( vForward, tr.endpos - vCenter ) ) < 1.5f;

			// ...and nothing between it and the center (inside corners, ledges).
			if ( bGood && i < nSamples )
			{
				UTIL_TraceLine( vCenter + vForward * 2.0f, vSample + vForward * 2.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				bGood = ( tr.fraction >= 1.0f ) && !tr.startsolid;
			}

			if ( sv_portal_debug.GetBool() )
			{
				NDebugOverlay::Line( vSample + vForward * 4.0f, vSample - vForward * 4.0f, bGood ? 0 : 255, bGood ? 255 : 0, 0, true, 3.0f );
			}

			if ( !bGood )
			{
				bFits = false;
				if ( i == nSamples )
				{
					// The center itself is bad; nowhere to push from.
					return false;
				}
				vPush -= ( vSample - vCenter );
			}
		}

		// Don't overlap the partner.
		if ( pLinked && pLinked->IsActivated() )
		{
			Vector vLinkedNormal = pLinked->GetSurfaceNormal();
			Vector vDelta = pLinked->GetAbsOrigin() - vCenter;
			if ( DotProduct( vLinkedNormal, vForward ) > 0.98f && fabs( DotProduct( vForward, vDelta ) ) < 2.0f )
			{
				float flX = DotProduct( vDelta, vRight ) / ( 2.0f * HL2MP_PORTAL_HALF_WIDTH );
				float flY = DotProduct( vDelta, vUp ) / ( 2.0f * HL2MP_PORTAL_HALF_HEIGHT );
				if ( flX * flX + flY * flY < 1.0f )
				{
					bFits = false;
					vPush -= vDelta;
				}
			}
		}

		if ( bFits )
			break;

		// Keep the push on the surface plane.
		vPush -= vForward * DotProduct( vPush, vForward );
		if ( vPush.LengthSqr() < 0.0001f )
			return false;

		VectorNormalize( vPush );
		vCenter += vPush * flStep;
	}

	if ( !bFits )
		return false;

	// Wall portals just above a floor drop onto it, so players can walk in
	// (their hull has to fit the hole to go through seamlessly).
	if ( fabs( vForward.z ) < 0.1f )
	{
		trace_t tr;
		Vector vBottom = vCenter - vUp * HL2MP_PORTAL_HALF_HEIGHT + vForward * 2.0f;
		UTIL_TraceLine( vBottom, vBottom - Vector( 0, 0, 12 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal.z > 0.7f )
		{
			vCenter.z = tr.endpos.z + HL2MP_PORTAL_HALF_HEIGHT;
		}
	}

	Teleport( &vCenter, &angPortal, NULL );
	m_bActivated = true;
	return true;
}

void CHL2MPPortal::Fizzle( void )
{
	m_bActivated = false;
}

bool CHL2MPPortal::ShouldTeleport( CBaseEntity *pEntity )
{
	if ( !pEntity || pEntity == this || pEntity->IsWorld() )
		return false;

	if ( pEntity->IsMarkedForDeletion() || pEntity->GetMoveParent() )
		return false;

	if ( FClassnameIs( pEntity, "prop_hl2mp_portal" ) )
		return false;

	switch ( pEntity->GetMoveType() )
	{
	case MOVETYPE_NONE:
	case MOVETYPE_PUSH:
	case MOVETYPE_NOCLIP:
	case MOVETYPE_OBSERVER:
	case MOVETYPE_LADDER:
		return false;
	default:
		break;
	}

	// Brush entities (doors, trains...) stay put unless they're physics.
	if ( pEntity->GetSolid() == SOLID_BSP && pEntity->GetMoveType() != MOVETYPE_VPHYSICS )
		return false;

	// Frozen physics objects (motion disabled props, stuck tripmines) stay put.
	if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS )
	{
		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( !pPhys || !pPhys->IsMotionEnabled() )
			return false;
	}

	// Players move through axis-aligned portals only in their own movement
	// code (UTIL_HL2MPPortal_TracePlayerHull, funneled into the hole), which
	// the client predicts. Snapping them here as well fought that prediction:
	// snaps at an angle, at the edge, between floor portals, lost speed.
	// Only portals on sloped surfaces still teleport players here.
	if ( pEntity->IsPlayer() && CanCarve() && pEntity->GetMoveType() == MOVETYPE_WALK )
		return false;

	if ( pEntity->IsPlayer() )
	{
		CBasePlayer *pPlayer = static_cast<CBasePlayer *>( pEntity );
		if ( !pPlayer->IsAlive() || pPlayer->IsObserver() || pPlayer->IsInAVehicle() )
			return false;
	}

	if ( PortalCooldownActive( pEntity ) )
		return false;

	Vector vForward, vRight, vUp;
	GetPortalVectors( &vForward, &vRight, &vUp );

	Vector vMins, vMaxs;
	pEntity->CollisionProp()->WorldSpaceAABB( &vMins, &vMaxs );
	Vector vCenter = ( vMins + vMaxs ) * 0.5f;
	Vector vHalf = ( vMaxs - vMins ) * 0.5f;

	// Center must be in front of the plane (things behind the wall are in another room).
	float flCenterDist = DotProduct( vForward, vCenter - GetAbsOrigin() );
	if ( flCenterDist < 0.0f )
		return false;

	// Gap between the entity's leading face and the plane.
	float flGap = flCenterDist - BoxSupport( vHalf, vForward );

	Vector vVelocity;
	AngularImpulse angVelocity;
	GetPortalEntityVelocity( pEntity, &vVelocity, &angVelocity );
	float flNormalSpeed = DotProduct( vVelocity, vForward );

	// Non-physics, non-player things only move by their velocity; a still one
	// is attached to something (SLAM tripmine on a wall) and stays put.
	if ( !pEntity->IsPlayer() && pEntity->GetMoveType() != MOVETYPE_VPHYSICS && vVelocity.LengthSqr() < 1.0f )
		return false;

	// A held object only goes through when the spot it's held at is past the
	// plane. Otherwise the hold pulls it back against the exit, it touches
	// that portal and flips back, over and over.
	IPhysicsObject *pHeldPhys = pEntity->VPhysicsGetObject();
	if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pHeldPhys && ( pHeldPhys->GetGameFlags() & FVPHYSICS_PLAYER_HELD ) )
	{
		Vector vecHeldTarget;
		if ( PhysCannon_GetHeldObjectTarget( pEntity, &vecHeldTarget ) &&
			DotProduct( vForward, vecHeldTarget - GetAbsOrigin() ) > -PORTAL_HELD_THROUGH_DIST )
			return false;
	}

	bool bTouching = ( flGap <= PORTAL_TOUCH_DIST ) && ( flNormalSpeed <= 1.0f );
	bool bArriving = ( flNormalSpeed < -1.0f ) && ( flGap <= -flNormalSpeed * TICK_INTERVAL * sv_portal_lookahead.GetFloat() + PORTAL_TOUCH_DIST );
	if ( !bTouching && !bArriving )
		return false;

	// Where does the center cross the plane?
	Vector vCross = vCenter;
	if ( flNormalSpeed < -1.0f && flGap > 0.0f )
	{
		vCross += vVelocity * ( flGap / -flNormalSpeed );
	}
	vCross -= vForward * DotProduct( vForward, vCross - GetAbsOrigin() );

	return IsPointInEllipse( vCross );
}

bool CHL2MPPortal::TeleportEntity( CBaseEntity *pEntity )
{
	CHL2MPPortal *pLinked = GetLinkedPortal();
	if ( !pLinked )
		return false;

	VMatrix matToLinked = MatrixThisToLinked();

	Vector vExitForward, vExitRight, vExitUp;
	pLinked->GetPortalVectors( &vExitForward, &vExitRight, &vExitUp );

	Vector vVelocity;
	AngularImpulse angVelocity;
	GetPortalEntityVelocity( pEntity, &vVelocity, &angVelocity );

	Vector vNewVelocity = matToLinked.VMul3x3( vVelocity );

	Vector vMins, vMaxs;
	pEntity->CollisionProp()->WorldSpaceAABB( &vMins, &vMaxs );
	Vector vCenter = ( vMins + vMaxs ) * 0.5f;
	Vector vNewCenter = matToLinked.VMul4x3( vCenter );

	QAngle angNew;
	Vector vHalf;		// half size of the world AABB after teleporting
	Vector vOffset;		// origin -> AABB center after teleporting

	if ( pEntity->IsPlayer() )
	{
		// Player hulls never rotate; only the view does.
		CBasePlayer *pPlayer = static_cast<CBasePlayer *>( pEntity );

		Vector vEyeForward;
		AngleVectors( pPlayer->EyeAngles(), &vEyeForward );
		vEyeForward = matToLinked.VMul3x3( vEyeForward );
		VectorAngles( vEyeForward, angNew );
		angNew.x = AngleNormalize( angNew.x );
		angNew.x = clamp( angNew.x, -89.0f, 89.0f );
		angNew.z = 0.0f;

		vHalf = ( vMaxs - vMins ) * 0.5f;
		vOffset = vCenter - pEntity->GetAbsOrigin();
	}
	else
	{
		VMatrix matEntity, matNew;
		matEntity.SetupMatrixOrgAngles( pEntity->GetAbsOrigin(), pEntity->GetAbsAngles() );
		matNew = matToLinked * matEntity;
		MatrixAngles( matNew.As3x4(), angNew );

		matrix3x4_t matRotation;
		AngleMatrix( angNew, matRotation );

		Vector vRotMins, vRotMaxs;
		RotateAABB( matRotation, pEntity->CollisionProp()->OBBMins(), pEntity->CollisionProp()->OBBMaxs(), vRotMins, vRotMaxs );
		vHalf = ( vRotMaxs - vRotMins ) * 0.5f;
		vOffset = ( vRotMins + vRotMaxs ) * 0.5f;
	}

	// The transformed center is behind the exit plane; bring it out in front.
	float flExtent = BoxSupport( vHalf, vExitForward );
	float flDist = DotProduct( vExitForward, vNewCenter - pLinked->GetAbsOrigin() );
	vNewCenter += vExitForward * ( flExtent + PORTAL_EXIT_GAP - flDist );

	// Find a spot that's not in a wall/floor: as is, centered on the portal,
	// then raised (wall portals near the floor).
	Vector vCentered = pLinked->GetAbsOrigin() + vExitForward * ( flExtent + PORTAL_EXIT_GAP );
	Vector vCandidates[] =
	{
		vNewCenter,
		vCentered,
		vNewCenter + Vector( 0, 0, 16 ),
		vNewCenter + Vector( 0, 0, 32 ),
		vCentered + Vector( 0, 0, 16 ),
		vCentered + Vector( 0, 0, 32 ),
		vCentered - Vector( 0, 0, 16 ),
	};

	unsigned int mask = pEntity->PhysicsSolidMaskForEntity();
	bool bFound = false;
	for ( int i = 0; i < ARRAYSIZE( vCandidates ); i++ )
	{
		// Never put it behind the exit plane.
		if ( DotProduct( vExitForward, vCandidates[i] - pLinked->GetAbsOrigin() ) < flExtent )
			continue;

		trace_t tr;
		UTIL_TraceHull( vCandidates[i], vCandidates[i], -vHalf, vHalf, mask, pEntity, pEntity->GetCollisionGroup(), &tr );
		if ( !tr.startsolid && !tr.allsolid )
		{
			vNewCenter = vCandidates[i];
			bFound = true;
			break;
		}
	}

	if ( !bFound )
	{
		if ( sv_portal_debug.GetBool() )
		{
			NDebugOverlay::Box( vNewCenter, -vHalf, vHalf, 255, 0, 0, 32, 2.0f );
		}
		return false;
	}

	Vector vNewOrigin = vNewCenter - vOffset;
	pEntity->Teleport( &vNewOrigin, &angNew, &vNewVelocity );

	if ( pEntity->IsPlayer() )
	{
		pEntity->SetGroundEntity( NULL );
		PhysCannon_HolderWentThroughPortal( static_cast<CBasePlayer *>( pEntity ), this );
	}
	else
	{
		// Still held (gravity gun or +use) on the other side.
		PhysCannon_HeldObjectWentThroughPortal( pEntity, this );

		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pPhys )
		{
			// Angular velocity is local, so it carries over untouched.
			pPhys->SetVelocity( &vNewVelocity, &angVelocity );
			pPhys->Wake();
		}
	}

	PortalStartCooldown( pEntity );

	s_TeleportLog.AddToTail( pEntity );
	if ( s_TeleportLog.Count() > 64 )
	{
		s_TeleportLog.Remove( 0 );
	}

	if ( sv_portal_debug.GetBool() )
	{
		NDebugOverlay::Box( vNewCenter, -vHalf, vHalf, 0, 255, 0, 32, 2.0f );
		NDebugOverlay::Line( vNewCenter, vNewCenter + vNewVelocity * 0.25f, 255, 255, 0, true, 2.0f );
	}

	return true;
}

void CHL2MPPortal::PortalThink( void )
{
	SetNextThink( gpGlobals->curtime + TICK_INTERVAL );

	if ( !IsOpen() )
		return;

	Vector vForward, vRight, vUp;
	GetPortalVectors( &vForward, &vRight, &vUp );

	// World bounds of the detection volume.
	Vector vMins( FLT_MAX, FLT_MAX, FLT_MAX );
	Vector vMaxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	for ( int i = 0; i < 8; i++ )
	{
		Vector vCorner = GetAbsOrigin() +
			vForward * ( ( i & 1 ) ? PORTAL_DETECT_FRONT : -PORTAL_DETECT_BACK ) +
			vRight * ( ( i & 2 ) ? HL2MP_PORTAL_HALF_WIDTH : -HL2MP_PORTAL_HALF_WIDTH ) +
			vUp * ( ( i & 4 ) ? HL2MP_PORTAL_HALF_HEIGHT : -HL2MP_PORTAL_HALF_HEIGHT );
		VectorMin( vMins, vCorner, vMins );
		VectorMax( vMaxs, vCorner, vMaxs );
	}

	if ( sv_portal_debug.GetBool() )
	{
		NDebugOverlay::Box( vec3_origin, vMins, vMaxs, m_iPortalType == HL2MP_PORTAL_BLUE ? 0 : 255, 128, m_iPortalType == HL2MP_PORTAL_BLUE ? 255 : 0, 0, TICK_INTERVAL * 2 );
	}

	CBaseEntity *pList[256];
	int nCount = UTIL_EntitiesInBox( pList, ARRAYSIZE( pList ), vMins, vMaxs, 0 );

	// Teleporting can change the list's entities; gather handles first.
	CUtlVector<EHANDLE> candidates;
	for ( int i = 0; i < nCount; i++ )
	{
		if ( ShouldTeleport( pList[i] ) )
		{
			candidates.AddToTail( pList[i] );
		}
	}

	for ( int i = 0; i < candidates.Count(); i++ )
	{
		if ( candidates[i].Get() )
		{
			TeleportEntity( candidates[i].Get() );
		}
	}
}

void CHL2MPPortal::AddPortalsToPVS( unsigned char *pvs, int pvssize )
{
	for ( int i = 0; i < s_HL2MPPortals.Count(); i++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[i];
		if ( !pPortal->IsOpen() )
			continue;

		Vector vTestPoint = pPortal->GetAbsOrigin() + pPortal->GetSurfaceNormal() * 4.0f;
		if ( !engine->CheckOriginInPVS( vTestPoint, pvs, pvssize ) )
			continue;

		CHL2MPPortal *pLinked = pPortal->GetLinkedPortal();
		engine->AddOriginToPVS( pLinked->GetAbsOrigin() + pLinked->GetSurfaceNormal() * 4.0f );
	}
}

void UTIL_HL2MPPortal_RadiusDamage( const CTakeDamageInfo &info, const Vector &vecSrc, float flRadius, int iClassIgnore, CBaseEntity *pEntityIgnore )
{
	if ( flRadius <= 0.0f || info.GetDamage() <= 0.0f )
		return;

	for ( int i = 0; i < s_HL2MPPortals.Count(); i++ )
	{
		CHL2MPPortal *pPortal = s_HL2MPPortals[i];
		if ( !pPortal->IsOpen() )
			continue;

		// In front of the portal and within reach of its hole.
		Vector vecNormal = pPortal->GetSurfaceNormal();
		Vector vecToSrc = vecSrc - pPortal->GetAbsOrigin();
		float flFront = DotProduct( vecToSrc, vecNormal );
		if ( flFront < -1.0f || flFront >= flRadius )
			continue;

		// Closest point of the hole (ellipse) to the blast.
		Vector vecRight, vecUp;
		pPortal->GetPortalVectors( NULL, &vecRight, &vecUp );
		float flX = DotProduct( vecToSrc, vecRight ) / HL2MP_PORTAL_HALF_WIDTH;
		float flY = DotProduct( vecToSrc, vecUp ) / HL2MP_PORTAL_HALF_HEIGHT;
		float flLen = sqrtf( flX * flX + flY * flY );
		if ( flLen > 1.0f )
		{
			flX /= flLen;
			flY /= flLen;
		}
		Vector vecHole = pPortal->GetAbsOrigin() + vecRight * ( flX * HL2MP_PORTAL_HALF_WIDTH ) + vecUp * ( flY * HL2MP_PORTAL_HALF_HEIGHT );
		float flToHole = ( vecSrc - vecHole ).Length();
		float flRemaining = flRadius - flToHole;
		if ( flRemaining < 8.0f )
			continue;

		// Nothing solid between the blast and the hole.
		trace_t tr;
		UTIL_TraceLine( vecSrc, vecHole + vecNormal * 2.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction < 1.0f )
			continue;

		// The rest goes off just out of the other portal, with the damage it
		// would still have there (same falloff over the whole path).
		CHL2MPPortal *pLinked = pPortal->GetLinkedPortal();
		VMatrix matToLinked = pPortal->MatrixThisToLinked();
		Vector vecExitSrc = matToLinked.VMul4x3( vecHole ) + pLinked->GetSurfaceNormal() * 2.0f;

		CTakeDamageInfo infoExit = info;
		infoExit.SetDamage( info.GetDamage() * flRemaining / flRadius );
		infoExit.SetDamagePosition( vecExitSrc );
		infoExit.SetReportedPosition( vecExitSrc );
		g_pGameRules->CGameRules::RadiusDamage( infoExit, vecExitSrc, flRemaining, iClassIgnore, pEntityIgnore );
	}
}

#endif // GAME_DLL

//=============================================================================
// Client
//=============================================================================
#ifdef CLIENT_DLL

bool g_bRenderingHL2MPPortalView = false;
int g_iHL2MPPortalRenderDepth = 0;
VMatrix g_matHL2MPPortalWorldToProjection[HL2MP_PORTAL_MAX_DEPTH];
int g_iHL2MPPortalViewStamp[HL2MP_PORTAL_MAX_DEPTH] = { 0 };
float g_flHL2MPPortalUScale = 1.0f;
float g_flHL2MPPortalVScale = 1.0f;

const char *HL2MPPortal_GetRenderTargetName( int iSlot )
{
	static const char *s_pszNames[HL2MP_PORTAL_MAX_VIEWS] =
	{
		"_rt_HL2MPPortal0",
		"_rt_HL2MPPortal1",
		"_rt_HL2MPPortal2",
		"_rt_HL2MPPortal3",
	};

	if ( iSlot < 0 || iSlot >= HL2MP_PORTAL_MAX_VIEWS )
		return NULL;

	return s_pszNames[iSlot];
}

// Materials are made at runtime so the mod needs no extra content.
static IMaterial *CreatePortalMaterial( const char *pszName, const char *pszBaseTexture, bool bAdditive )
{
	KeyValues *pKeys = new KeyValues( "UnlitGeneric" );
	if ( pszBaseTexture )
	{
		pKeys->SetString( "$basetexture", pszBaseTexture );
	}
	else
	{
		pKeys->SetInt( "$vertexcolor", 1 );
		pKeys->SetInt( "$vertexalpha", 1 );
	}
	pKeys->SetInt( "$nocull", 1 );
	if ( bAdditive )
	{
		pKeys->SetInt( "$additive", 1 );
		pKeys->SetInt( "$translucent", 1 );
	}

	IMaterial *pMaterial = materials->CreateMaterial( pszName, pKeys );
	if ( pMaterial )
	{
		pMaterial->IncrementReferenceCount();
	}
	return pMaterial;
}

static IMaterial *GetPortalFillMaterial()
{
	static IMaterial *s_pMaterial = NULL;
	if ( !s_pMaterial )
	{
		s_pMaterial = CreatePortalMaterial( "__hl2mp_portal_fill", NULL, false );
	}
	return s_pMaterial;
}

static IMaterial *GetPortalRimMaterial()
{
	static IMaterial *s_pMaterial = NULL;
	if ( !s_pMaterial )
	{
		s_pMaterial = CreatePortalMaterial( "__hl2mp_portal_rim", NULL, true );
	}
	return s_pMaterial;
}

static IMaterial *GetPortalViewMaterial( int iSlot )
{
	static IMaterial *s_pMaterials[HL2MP_PORTAL_MAX_VIEWS] = { NULL };
	if ( iSlot < 0 || iSlot >= HL2MP_PORTAL_MAX_VIEWS )
		return NULL;

	if ( !s_pMaterials[iSlot] )
	{
		char szName[64];
		Q_snprintf( szName, sizeof( szName ), "__hl2mp_portal_view%d", iSlot );
		s_pMaterials[iSlot] = CreatePortalMaterial( szName, HL2MPPortal_GetRenderTargetName( iSlot ), false );
	}
	return s_pMaterials[iSlot];
}

bool C_HL2MPPortal::ShouldDraw( void )
{
	return m_bActivated && !IsDormant();
}

void C_HL2MPPortal::GetRenderBounds( Vector &mins, Vector &maxs )
{
	// Entity space: x is the surface normal.
	mins.Init( -2.0f, -HL2MP_PORTAL_HALF_WIDTH * 1.1f, -HL2MP_PORTAL_HALF_HEIGHT * 1.1f );
	maxs.Init( 2.0f, HL2MP_PORTAL_HALF_WIDTH * 1.1f, HL2MP_PORTAL_HALF_HEIGHT * 1.1f );
}

// Ellipse tessellation. The view texture is sampled in screen space, which
// the rasterizer can't do exactly (it interpolates perspective-correct), so
// the fill is split into many small triangles to keep the error invisible.
// The error shrinks with distance (less perspective across the portal), so
// far portals get fewer rings.
#define PORTAL_MESH_RINGS		10
#define PORTAL_MESH_SEGMENTS	40

struct PortalVertex_t
{
	Vector	vPos;
	float	u, v;
};

// Unit circle, computed once.
static float s_flPortalCos[PORTAL_MESH_SEGMENTS];
static float s_flPortalSin[PORTAL_MESH_SEGMENTS];
static void InitPortalCircle()
{
	static bool s_bInit = false;
	if ( s_bInit )
		return;
	for ( int i = 0; i < PORTAL_MESH_SEGMENTS; i++ )
	{
		float flAngle = ( 2.0f * M_PI * i ) / PORTAL_MESH_SEGMENTS;
		s_flPortalCos[i] = cos( flAngle );
		s_flPortalSin[i] = sin( flAngle );
	}
	s_bInit = true;
}

static void ComputePortalVertex( const Vector &vCenter, const Vector &vRight, const Vector &vUp, float flRadius, int iSegment, bool bScreenUV, PortalVertex_t &out )
{
	iSegment %= PORTAL_MESH_SEGMENTS;
	out.vPos = vCenter + vRight * ( s_flPortalCos[iSegment] * HL2MP_PORTAL_HALF_WIDTH * flRadius ) + vUp * ( s_flPortalSin[iSegment] * HL2MP_PORTAL_HALF_HEIGHT * flRadius );

	if ( bScreenUV )
	{
		Vector4D vClip;
		g_matHL2MPPortalWorldToProjection[g_iHL2MPPortalRenderDepth].V4Mul( Vector4D( out.vPos.x, out.vPos.y, out.vPos.z, 1.0f ), vClip );
		float flW = MAX( vClip.w, 0.001f );
		out.u = ( 0.5f + 0.5f * vClip.x / flW ) * g_flHL2MPPortalUScale;
		out.v = ( 0.5f - 0.5f * vClip.y / flW ) * g_flHL2MPPortalVScale;
	}
	else
	{
		out.u = out.v = 0.0f;
	}
}

static void EmitPortalVertex( CMeshBuilder &meshBuilder, const PortalVertex_t &vert, const color32 &color )
{
	meshBuilder.Position3fv( vert.vPos.Base() );
	meshBuilder.Color4ub( color.r, color.g, color.b, color.a );
	meshBuilder.TexCoord2f( 0, vert.u, vert.v );
	meshBuilder.AdvanceVertex();
}

// Two triangles between ring vertices: inner (i0, i1) and outer (o0, o1).
static void EmitPortalQuad( CMeshBuilder &meshBuilder, int i0, int i1, int o0, int o1 )
{
	meshBuilder.FastIndex( i0 ); meshBuilder.FastIndex( o0 ); meshBuilder.FastIndex( o1 );
	meshBuilder.FastIndex( i0 ); meshBuilder.FastIndex( o1 ); meshBuilder.FastIndex( i1 );
}

// Filled ellipse, nRings rings from the center out. Indexed: each vertex is
// computed (and projected) once.
static void DrawPortalFill( IMaterial *pMaterial, const Vector &vCenter, const Vector &vRight, const Vector &vUp, bool bScreenUV, const color32 &color, int nRings )
{
	InitPortalCircle();
	nRings = clamp( nRings, 1, PORTAL_MESH_RINGS );
	const int S = PORTAL_MESH_SEGMENTS;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMaterial );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 1 + nRings * S, 3 * S + ( nRings - 1 ) * S * 6 );

	// Vertex 0 is the center, then ring r (1..nRings) segment i is 1 + (r - 1) * S + i.
	PortalVertex_t vert;
	ComputePortalVertex( vCenter, vRight, vUp, 0.0f, 0, bScreenUV, vert );
	EmitPortalVertex( meshBuilder, vert, color );
	for ( int iRing = 1; iRing <= nRings; iRing++ )
	{
		float flRadius = (float)iRing / nRings;
		for ( int iSeg = 0; iSeg < S; iSeg++ )
		{
			ComputePortalVertex( vCenter, vRight, vUp, flRadius, iSeg, bScreenUV, vert );
			EmitPortalVertex( meshBuilder, vert, color );
		}
	}

	for ( int iSeg = 0; iSeg < S; iSeg++ )
	{
		meshBuilder.FastIndex( 0 ); meshBuilder.FastIndex( 1 + iSeg ); meshBuilder.FastIndex( 1 + ( iSeg + 1 ) % S );
	}
	for ( int iRing = 2; iRing <= nRings; iRing++ )
	{
		int iInner = 1 + ( iRing - 2 ) * S;
		int iOuter = 1 + ( iRing - 1 ) * S;
		for ( int iSeg = 0; iSeg < S; iSeg++ )
		{
			int iNext = ( iSeg + 1 ) % S;
			EmitPortalQuad( meshBuilder, iInner + iSeg, iInner + iNext, iOuter + iSeg, iOuter + iNext );
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

// A tunnel behind the portal surface: ellipse walls from the plane back to
// flDepth plus a cap. It's hidden by the wall from any distance, but when the
// camera is within its near-plane distance of the portal the wall and the
// portal surface get clipped away, and this (sampled in screen space like the
// surface) keeps showing the far side instead of the inside of the wall.
static void DrawPortalTunnel( IMaterial *pMaterial, const Vector &vCenter, const Vector &vForward, const Vector &vRight, const Vector &vUp, float flDepth )
{
	InitPortalCircle();
	const int S = PORTAL_MESH_SEGMENTS;
	color32 white = { 255, 255, 255, 255 };
	Vector vBack = vCenter - vForward * flDepth;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMaterial );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 2 * S + 1, S * 9 );

	// Front ring 0..S-1, back ring S..2S-1, cap center 2S.
	PortalVertex_t vert;
	for ( int iRing = 0; iRing < 2; iRing++ )
	{
		for ( int iSeg = 0; iSeg < S; iSeg++ )
		{
			ComputePortalVertex( iRing ? vBack : vCenter, vRight, vUp, 1.0f, iSeg, true, vert );
			EmitPortalVertex( meshBuilder, vert, white );
		}
	}
	ComputePortalVertex( vBack, vRight, vUp, 0.0f, 0, true, vert );
	EmitPortalVertex( meshBuilder, vert, white );

	for ( int iSeg = 0; iSeg < S; iSeg++ )
	{
		int iNext = ( iSeg + 1 ) % S;
		EmitPortalQuad( meshBuilder, iSeg, iNext, S + iSeg, S + iNext );
		meshBuilder.FastIndex( 2 * S ); meshBuilder.FastIndex( S + iNext ); meshBuilder.FastIndex( S + iSeg );
	}

	meshBuilder.End();
	pMesh->Draw();
}

// Glowing band from flInner (alpha 0) to flMid (full) to flOuter (alpha 0).
static void DrawPortalRim( const Vector &vCenter, const Vector &vRight, const Vector &vUp, const color32 &color )
{
	InitPortalCircle();
	const int S = PORTAL_MESH_SEGMENTS;
	const float flRadii[3] = { 0.86f, 1.0f, 1.08f };
	const unsigned char alphas[3] = { 0, 255, 0 };

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( GetPortalRimMaterial() );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 3 * S, 2 * S * 6 );

	// Ring r segment i is r * S + i.
	PortalVertex_t vert;
	for ( int iRing = 0; iRing < 3; iRing++ )
	{
		// Additive: scale color by alpha so the falloff shows.
		color32 ringColor = color;
		ringColor.a = alphas[iRing];
		ringColor.r = ( color.r * ringColor.a ) / 255; ringColor.g = ( color.g * ringColor.a ) / 255; ringColor.b = ( color.b * ringColor.a ) / 255;
		for ( int iSeg = 0; iSeg < S; iSeg++ )
		{
			ComputePortalVertex( vCenter, vRight, vUp, flRadii[iRing], iSeg, false, vert );
			EmitPortalVertex( meshBuilder, vert, ringColor );
		}
	}

	for ( int iBand = 0; iBand < 2; iBand++ )
	{
		for ( int iSeg = 0; iSeg < S; iSeg++ )
		{
			int iNext = ( iSeg + 1 ) % S;
			EmitPortalQuad( meshBuilder, iBand * S + iSeg, iBand * S + iNext, ( iBand + 1 ) * S + iSeg, ( iBand + 1 ) * S + iNext );
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

#define PORTAL_TUNNEL_DEPTH			24.0f
// The tunnel is only needed when the camera is close enough to the plane for
// the near plane to clip the wall and the surface.
#define PORTAL_TUNNEL_DRAW_DIST		32.0f

float C_HL2MPPortal::GetTunnelDepth()
{
	UpdateFrameCache();
	if ( m_nTunnelVersion == m_nFrameVersion )
		return m_flTunnelDepth;

	// Trace into the wall from just behind the surface; where it leaves the
	// solid is the wall's thickness.
	trace_t tr;
	Vector vStart = m_vecCacheOrigin - m_vecCacheForward * 0.5f;
	UTIL_TraceLine( vStart, vStart - m_vecCacheForward * PORTAL_TUNNEL_DEPTH * 2.0f, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	float flThickness = PORTAL_TUNNEL_DEPTH;
	if ( tr.startsolid && !tr.allsolid )
	{
		flThickness = tr.fractionleftsolid * PORTAL_TUNNEL_DEPTH * 2.0f;
	}
	m_flTunnelDepth = clamp( flThickness - 1.0f, 0.0f, PORTAL_TUNNEL_DEPTH );
	m_nTunnelVersion = m_nFrameVersion;
	return m_flTunnelDepth;
}

int C_HL2MPPortal::DrawModel( int flags )
{
	if ( !m_bActivated )
		return 0;

	Vector vForward, vRight, vUp;
	GetPortalVectors( &vForward, &vRight, &vUp );
	Vector vCenter = GetAbsOrigin() + vForward * PORTAL_RENDER_OFFSET;

	bool bBlue = ( m_iPortalType == HL2MP_PORTAL_BLUE );
	color32 rimColor = bBlue ? color32{ 64, 160, 255, 255 } : color32{ 255, 140, 20, 255 };

	// Only draw from the front.
	if ( DotProduct( CurrentViewOrigin() - GetAbsOrigin(), vForward ) < 0.0f )
		return 0;

	// A live view exists for this portal at this depth if one was rendered
	// for the view being drawn (main view, or the portal view it's inside).
	int iDepth = g_iHL2MPPortalRenderDepth;
	view_id_t viewID = CurrentViewID();
	bool bRightView = ( iDepth == 0 ) ? ( viewID == VIEW_MAIN ) : ( viewID == VIEW_MONITOR );
	int iSlot = ( iDepth < HL2MP_PORTAL_MAX_DEPTH ) ? m_iViewSlot[iDepth] : -1;
	bool bHasView = IsOpen() && bRightView && iDepth < HL2MP_PORTAL_MAX_DEPTH &&
		m_iViewStamp[iDepth] == g_iHL2MPPortalViewStamp[iDepth] && GetPortalViewMaterial( iSlot );

	if ( bHasView )
	{
		color32 white = { 255, 255, 255, 255 };
		float flTunnelDepth = GetTunnelDepth();
		if ( flTunnelDepth > 1.0f && DotProduct( CurrentViewOrigin() - GetAbsOrigin(), vForward ) < PORTAL_TUNNEL_DRAW_DIST )
		{
			DrawPortalTunnel( GetPortalViewMaterial( iSlot ), GetAbsOrigin(), vForward, vRight, vUp, flTunnelDepth );
		}
		// Close up the screen-space mapping needs fine tessellation; far away it's nearly affine.
		float flDist = ( CurrentViewOrigin() - GetAbsOrigin() ).Length();
		int nRings = ( flDist < 256.0f ) ? PORTAL_MESH_RINGS : ( flDist < 1024.0f ) ? 6 : 3;
		DrawPortalFill( GetPortalViewMaterial( iSlot ), vCenter, vRight, vUp, true, white, nRings );
	}
	else
	{
		// Closed portals are dark; open portals we can't render through are brighter.
		color32 fill;
		if ( IsOpen() )
			fill = bBlue ? color32{ 40, 100, 170, 255 } : color32{ 170, 95, 20, 255 };
		else
			fill = bBlue ? color32{ 10, 30, 70, 255 } : color32{ 70, 35, 5, 255 };

		// Solid color: no texture mapping to keep accurate.
		DrawPortalFill( GetPortalFillMaterial(), vCenter, vRight, vUp, false, fill, 1 );
	}

	DrawPortalRim( vCenter + vForward * 0.1f, vRight, vUp, rimColor );
	return 1;
}

#endif // CLIENT_DLL
