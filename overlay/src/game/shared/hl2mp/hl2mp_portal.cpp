//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linked portal pair for HL2MP (the "portal mod"). See hl2mp_portal.h.
//
//=============================================================================//

#include "cbase.h"
#include "hl2mp_portal.h"

#ifdef CLIENT_DLL
	#include "materialsystem/imesh.h"
	#include "materialsystem/imaterial.h"
	#include "materialsystem/imaterialvar.h"
	#include "KeyValues.h"
	#include "viewrender.h"
	#include "view.h"
#else
	#include "player_pickup.h"
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

#ifdef CLIENT_DLL
ConVar cl_portal_render_views( "cl_portal_render_views", "1", FCVAR_ARCHIVE, "Render the view through portals (0 = draw portals as solid color)." );
#endif

// How far in front of the exit plane things are placed (must exceed TOUCH_DIST).
#define PORTAL_EXIT_GAP			2.0f
// An entity this close to the plane and not moving away goes through.
#define PORTAL_TOUCH_DIST		1.0f
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
#ifdef CLIENT_DLL
	m_iViewSlot = -1;
	m_iViewFrame = -1;
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

void CHL2MPPortal::GetPortalVectors( Vector *pForward, Vector *pRight, Vector *pUp ) const
{
	AngleVectors( GetAbsAngles(), pForward, pRight, pUp );
}

Vector CHL2MPPortal::GetSurfaceNormal() const
{
	Vector vForward;
	AngleVectors( GetAbsAngles(), &vForward );
	return vForward;
}

VMatrix CHL2MPPortal::MatrixThisToLinked() const
{
	VMatrix matResult;
	matResult.Identity();

	CHL2MPPortal *pLinked = GetLinkedPortal();
	if ( !pLinked )
		return matResult;

	VMatrix matThisToWorld, matWorldToThis, matLinkedToWorld, matRotate;
	matThisToWorld.SetupMatrixOrgAngles( GetAbsOrigin(), GetAbsAngles() );
	matLinkedToWorld.SetupMatrixOrgAngles( pLinked->GetAbsOrigin(), pLinked->GetAbsAngles() );
	MatrixInverseTR( matThisToWorld, matWorldToThis );

	// Half turn around local up: in through the front, out through the front.
	matRotate.Identity();
	matRotate.m[0][0] = -1.0f;
	matRotate.m[1][1] = -1.0f;

	matResult = matLinkedToWorld * matRotate * matWorldToThis;
	return matResult;
}

bool CHL2MPPortal::IsPointInEllipse( const Vector &vPoint, float flScale ) const
{
	Vector vRight, vUp;
	GetPortalVectors( NULL, &vRight, &vUp );

	Vector vDelta = vPoint - GetAbsOrigin();
	float flX = DotProduct( vDelta, vRight ) / ( HL2MP_PORTAL_HALF_WIDTH * flScale );
	float flY = DotProduct( vDelta, vUp ) / ( HL2MP_PORTAL_HALF_HEIGHT * flScale );
	return ( flX * flX + flY * flY ) <= 1.0f;
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

int UTIL_HL2MPPortal_TraceRay( const Vector &vecStart, const Vector &vecDir, float flDist,
	unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace, Vector *pVecDirOut,
	const Vector *pHullMins, const Vector *pHullMaxs, CHL2MPPortal *pIgnorePortal, CUtlVector<Vector> *pPath )
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

// Extent of an axis-aligned box (half sizes) along a direction.
static float BoxSupport( const Vector &vHalf, const Vector &vDir )
{
	return fabs( vDir.x ) * vHalf.x + fabs( vDir.y ) * vHalf.y + fabs( vDir.z ) * vHalf.z;
}

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

	// Gravity-gunned objects get dropped; the holder can't follow them.
	Pickup_ForcePlayerToDropThisObject( pEntity );

	Vector vNewOrigin = vNewCenter - vOffset;
	pEntity->Teleport( &vNewOrigin, &angNew, &vNewVelocity );

	if ( pEntity->IsPlayer() )
	{
		pEntity->SetGroundEntity( NULL );
	}
	else
	{
		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pPhys )
		{
			// Angular velocity is local, so it carries over untouched.
			pPhys->SetVelocity( &vNewVelocity, &angVelocity );
			pPhys->Wake();
		}
	}

	PortalStartCooldown( pEntity );

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

#endif // GAME_DLL

//=============================================================================
// Client
//=============================================================================
#ifdef CLIENT_DLL

bool g_bRenderingHL2MPPortalView = false;
VMatrix g_matHL2MPPortalWorldToProjection;
float g_flHL2MPPortalUScale = 1.0f;
float g_flHL2MPPortalVScale = 1.0f;

const char *HL2MPPortal_GetRenderTargetName( int iSlot )
{
	static const char *s_pszNames[HL2MP_PORTAL_MAX_VIEWS] =
	{
		"_rt_HL2MPPortal0",
		"_rt_HL2MPPortal1",
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
#define PORTAL_MESH_RINGS		10
#define PORTAL_MESH_SEGMENTS	40

struct PortalVertex_t
{
	Vector	vPos;
	float	u, v;
};

static void ComputePortalVertex( const Vector &vCenter, const Vector &vRight, const Vector &vUp, float flRadius, int iSegment, bool bScreenUV, PortalVertex_t &out )
{
	float flAngle = ( 2.0f * M_PI * iSegment ) / PORTAL_MESH_SEGMENTS;
	out.vPos = vCenter + vRight * ( cos( flAngle ) * HL2MP_PORTAL_HALF_WIDTH * flRadius ) + vUp * ( sin( flAngle ) * HL2MP_PORTAL_HALF_HEIGHT * flRadius );

	if ( bScreenUV )
	{
		Vector4D vClip;
		g_matHL2MPPortalWorldToProjection.V4Mul( Vector4D( out.vPos.x, out.vPos.y, out.vPos.z, 1.0f ), vClip );
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

// Filled ellipse from the center out to flOuterRadius.
static void DrawPortalFill( IMaterial *pMaterial, const Vector &vCenter, const Vector &vRight, const Vector &vUp, bool bScreenUV, const color32 &color )
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( pMaterial );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	int nTriangles = PORTAL_MESH_SEGMENTS + ( PORTAL_MESH_RINGS - 1 ) * PORTAL_MESH_SEGMENTS * 2;

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nTriangles );

	PortalVertex_t center;
	center.vPos = vCenter;
	center.u = center.v = 0.0f;
	if ( bScreenUV )
	{
		ComputePortalVertex( vCenter, vRight, vUp, 0.0f, 0, true, center );
	}

	for ( int iRing = 0; iRing < PORTAL_MESH_RINGS; iRing++ )
	{
		float flInner = (float)iRing / PORTAL_MESH_RINGS;
		float flOuter = (float)( iRing + 1 ) / PORTAL_MESH_RINGS;

		for ( int iSeg = 0; iSeg < PORTAL_MESH_SEGMENTS; iSeg++ )
		{
			PortalVertex_t a, b, c, d;
			ComputePortalVertex( vCenter, vRight, vUp, flOuter, iSeg, bScreenUV, a );
			ComputePortalVertex( vCenter, vRight, vUp, flOuter, iSeg + 1, bScreenUV, b );

			if ( iRing == 0 )
			{
				EmitPortalVertex( meshBuilder, center, color );
				EmitPortalVertex( meshBuilder, a, color );
				EmitPortalVertex( meshBuilder, b, color );
				continue;
			}

			ComputePortalVertex( vCenter, vRight, vUp, flInner, iSeg, bScreenUV, c );
			ComputePortalVertex( vCenter, vRight, vUp, flInner, iSeg + 1, bScreenUV, d );

			EmitPortalVertex( meshBuilder, c, color );
			EmitPortalVertex( meshBuilder, a, color );
			EmitPortalVertex( meshBuilder, b, color );

			EmitPortalVertex( meshBuilder, c, color );
			EmitPortalVertex( meshBuilder, b, color );
			EmitPortalVertex( meshBuilder, d, color );
		}
	}

	meshBuilder.End();
	pMesh->Draw();
}

// Glowing band from flInner (alpha 0) to flMid (full) to flOuter (alpha 0).
static void DrawPortalRim( const Vector &vCenter, const Vector &vRight, const Vector &vUp, const color32 &color )
{
	const float flRadii[3] = { 0.86f, 1.0f, 1.08f };
	const unsigned char alphas[3] = { 0, 255, 0 };

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( GetPortalRimMaterial() );
	IMesh *pMesh = pRenderContext->GetDynamicMesh();

	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, PORTAL_MESH_SEGMENTS * 4 );

	for ( int iBand = 0; iBand < 2; iBand++ )
	{
		color32 inner = color;
		color32 outer = color;
		inner.a = alphas[iBand];
		outer.a = alphas[iBand + 1];

		// Additive: scale color by alpha so the falloff shows.
		inner.r = ( color.r * inner.a ) / 255; inner.g = ( color.g * inner.a ) / 255; inner.b = ( color.b * inner.a ) / 255;
		outer.r = ( color.r * outer.a ) / 255; outer.g = ( color.g * outer.a ) / 255; outer.b = ( color.b * outer.a ) / 255;

		for ( int iSeg = 0; iSeg < PORTAL_MESH_SEGMENTS; iSeg++ )
		{
			PortalVertex_t a, b, c, d;
			ComputePortalVertex( vCenter, vRight, vUp, flRadii[iBand], iSeg, false, a );
			ComputePortalVertex( vCenter, vRight, vUp, flRadii[iBand], iSeg + 1, false, b );
			ComputePortalVertex( vCenter, vRight, vUp, flRadii[iBand + 1], iSeg, false, c );
			ComputePortalVertex( vCenter, vRight, vUp, flRadii[iBand + 1], iSeg + 1, false, d );

			EmitPortalVertex( meshBuilder, a, inner );
			EmitPortalVertex( meshBuilder, c, outer );
			EmitPortalVertex( meshBuilder, d, outer );

			EmitPortalVertex( meshBuilder, a, inner );
			EmitPortalVertex( meshBuilder, d, outer );
			EmitPortalVertex( meshBuilder, b, inner );
		}
	}

	meshBuilder.End();
	pMesh->Draw();
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

	bool bHasView = IsOpen() && !g_bRenderingHL2MPPortalView && CurrentViewID() == VIEW_MAIN &&
		m_iViewFrame == gpGlobals->framecount && m_iViewSlot >= 0 && GetPortalViewMaterial( m_iViewSlot );

	if ( bHasView )
	{
		color32 white = { 255, 255, 255, 255 };
		DrawPortalFill( GetPortalViewMaterial( m_iViewSlot ), vCenter, vRight, vUp, true, white );
	}
	else
	{
		// Closed portals are dark; open portals we can't render through are brighter.
		color32 fill;
		if ( IsOpen() )
			fill = bBlue ? color32{ 40, 100, 170, 255 } : color32{ 170, 95, 20, 255 };
		else
			fill = bBlue ? color32{ 10, 30, 70, 255 } : color32{ 70, 35, 5, 255 };

		DrawPortalFill( GetPortalFillMaterial(), vCenter, vRight, vUp, false, fill );
	}

	DrawPortalRim( vCenter + vForward * 0.1f, vRight, vUp, rimColor );
	return 1;
}

#endif // CLIENT_DLL
