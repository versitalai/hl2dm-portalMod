//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Runtime self-test for the portal mod, run by CI on a real
//			dedicated server:  +sv_portal_selftest 1 +map <map>  (see scripts/selftest.sh)
//
//			Finds a floor and a wall near the first spawn point, places portals
//			there and checks what goes through. Prints one
//			"PORTALTEST <name> PASS|FAIL ..." line per test, then
//			"PORTALTEST DONE pass=N fail=M" and quits the server.
//
//=============================================================================//

#include "cbase.h"
#include "hl2mp_portal.h"
#include "weapon_rpg.h"
#include "igamesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sv_portal_selftest( "sv_portal_selftest", "0", FCVAR_NONE, "Run the portal self-test on map load, print PORTALTEST lines, then quit." );

class CHL2MPPortalSelfTest : public CAutoGameSystemPerFrame
{
public:
	CHL2MPPortalSelfTest() : CAutoGameSystemPerFrame( "CHL2MPPortalSelfTest" ) { m_iStep = -1; }

	virtual void LevelInitPostEntity()
	{
		m_iStep = sv_portal_selftest.GetBool() ? 0 : -1;
		m_flNextStep = gpGlobals->curtime + 2.0f;
		m_nPass = m_nFail = 0;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( m_iStep < 0 || gpGlobals->curtime < m_flNextStep )
			return;
		RunStep();
	}

private:
	void Result( const char *pszName, bool bPass, const char *pszFormat = "", ... )
	{
		char szDetail[512];
		va_list args;
		va_start( args, pszFormat );
		Q_vsnprintf( szDetail, sizeof( szDetail ), pszFormat, args );
		va_end( args );

		Msg( "PORTALTEST %s %s %s\n", pszName, bPass ? "PASS" : "FAIL", szDetail );
		if ( bPass )
			m_nPass++;
		else
			m_nFail++;
	}

	void Wait( float flSeconds ) { m_flNextStep = gpGlobals->curtime + flSeconds; }

	void Finish()
	{
		Msg( "PORTALTEST DONE pass=%d fail=%d\n", m_nPass, m_nFail );
		m_iStep = -1;
		engine->ServerCommand( "quit\n" );
	}

	CHL2MPPortal *CreatePortal( int iType )
	{
		CHL2MPPortal *pPortal = static_cast<CHL2MPPortal *>( CreateEntityByName( "prop_hl2mp_portal" ) );
		pPortal->SetPortalType( iType );
		DispatchSpawn( pPortal );
		return pPortal;
	}

	CBaseEntity *CreateProp( const Vector &vecOrigin )
	{
		CBaseEntity *pProp = CreateEntityByName( "prop_physics" );
		pProp->SetAbsOrigin( vecOrigin );
		pProp->KeyValue( "model", "models/props_c17/oildrum001.mdl" );
		pProp->Precache();
		DispatchSpawn( pProp );
		pProp->Activate();
		return pProp;
	}

	bool FindGeometry()
	{
		CBaseEntity *pSpawn = gEntList.FindEntityByClassname( NULL, "info_player_deathmatch" );
		if ( !pSpawn )
			pSpawn = gEntList.FindEntityByClassname( NULL, "info_player_start" );
		if ( !pSpawn )
			return false;

		trace_t tr;
		Vector vecStart = pSpawn->GetAbsOrigin() + Vector( 0, 0, 32 );
		UTIL_TraceLine( vecStart, vecStart - Vector( 0, 0, 512 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 1.0f || tr.plane.normal.z < 0.9f )
			return false;
		m_vecFloor = tr.endpos;

		// Closest wall at least 160 units away, looking around from head height.
		float flBest = FLT_MAX;
		for ( int i = 0; i < 16; i++ )
		{
			float flYaw = i * ( 2.0f * M_PI / 16.0f );
			Vector vecDir( cos( flYaw ), sin( flYaw ), 0 );
			Vector vecEye = m_vecFloor + Vector( 0, 0, 64 );
			UTIL_TraceLine( vecEye, vecEye + vecDir * 1500, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			float flDist = tr.fraction * 1500;
			if ( tr.fraction < 1.0f && fabs( tr.plane.normal.z ) < 0.1f && flDist > 160 && flDist < flBest )
			{
				flBest = flDist;
				m_vecWall = tr.endpos;
				m_vecWallNormal = tr.plane.normal;
			}
		}
		return flBest < FLT_MAX;
	}

	void RunStep()
	{
		switch ( m_iStep++ )
		{
		case 0:
			if ( !FindGeometry() )
			{
				Result( "geometry", false, "no floor/wall near a spawn point" );
				Finish();
				return;
			}
			Result( "geometry", true, "floor (%.0f %.0f %.0f) wall (%.0f %.0f %.0f)", m_vecFloor.x, m_vecFloor.y, m_vecFloor.z, m_vecWall.x, m_vecWall.y, m_vecWall.z );

			m_hWall = CreatePortal( HL2MP_PORTAL_BLUE );
			m_hFloor = CreatePortal( HL2MP_PORTAL_ORANGE );
			m_hWall->SetLinkedPortal( m_hFloor );
			m_hFloor->SetLinkedPortal( m_hWall );

			Result( "place_wall", m_hWall->PlaceOnSurface( m_vecWall, m_vecWallNormal, -m_vecWallNormal ) );

			// A prop resting where the floor portal will open.
			m_hProp = CreateProp( m_vecFloor + Vector( 0, 0, 24 ) );
			Wait( 5.0f );	// let it settle and fall asleep
			return;

		case 1:
		{
			IPhysicsObject *pPhys = m_hProp ? m_hProp->VPhysicsGetObject() : NULL;
			m_vecPropStart = m_hProp ? m_hProp->GetAbsOrigin() : vec3_origin;
			Msg( "PORTALTEST info prop asleep=%d\n", pPhys ? pPhys->IsAsleep() : -1 );

			Result( "place_floor", m_hFloor->PlaceOnSurface( m_vecFloor, Vector( 0, 0, 1 ), Vector( 1, 0, 0 ) ) );
			Wait( 1.5f );
			return;
		}

		case 2:
		{
			// Bug report: props at rest stayed frozen over a new portal.
			bool bMoved = m_hProp && ( m_hProp->GetAbsOrigin() - m_vecPropStart ).Length() > 64;
			Result( "sleeping_prop_falls_through", m_hProp && CHL2MPPortal::WasTeleported( m_hProp ) && bMoved,
				"teleported=%d moved=%d", m_hProp ? CHL2MPPortal::WasTeleported( m_hProp ) : 0, bMoved );

			// Bug report: a held prop reaching a portal crashed the game.
			m_hHeldProp = CreateProp( m_vecFloor + Vector( 0, 0, 80 ) );
			IPhysicsObject *pPhys = m_hHeldProp->VPhysicsGetObject();
			if ( pPhys )
			{
				pPhys->SetGameFlags( pPhys->GetGameFlags() | FVPHYSICS_PLAYER_HELD );
			}
			Wait( 2.0f );
			return;
		}

		case 3:
		{
			Result( "held_prop_no_crash", m_hHeldProp && CHL2MPPortal::WasTeleported( m_hHeldProp ) );

			// Bullet-style trace into the wall portal comes out of the floor one.
			trace_t tr;
			Vector vecFrom = m_hWall->GetAbsOrigin() + m_vecWallNormal * 128;
			CTraceFilterWorldOnly filter;
			int nPortals = UTIL_HL2MPPortal_TraceRay( vecFrom, -m_vecWallNormal, 4096, MASK_SHOT, &filter, &tr );
			float flFromFloor = ( tr.startpos - m_hFloor->GetAbsOrigin() ).Length();
			Result( "trace_through_portal", nPortals == 1 && flFromFloor < 4, "portals=%d exit_dist=%.1f", nPortals, flFromFloor );

			// A rocket fired at the wall portal must come out, not explode on the wall.
			QAngle angRocket;
			VectorAngles( -m_vecWallNormal, angRocket );
			m_hRocket = CMissile::Create( m_hWall->GetAbsOrigin() + m_vecWallNormal * 400, angRocket, NULL );
			if ( m_hRocket )
			{
				m_hRocket->SetAbsVelocity( -m_vecWallNormal * 1500 );
			}
			Wait( 1.0f );
			return;
		}

		case 4:
			Result( "rocket_through_portal", m_hRocket && CHL2MPPortal::WasTeleported( m_hRocket ) );
			Finish();
			return;
		}
	}

	int		m_iStep;
	float	m_flNextStep;
	int		m_nPass, m_nFail;

	Vector	m_vecFloor, m_vecWall, m_vecWallNormal, m_vecPropStart;
	CHandle<CHL2MPPortal> m_hWall, m_hFloor;
	EHANDLE	m_hProp, m_hHeldProp, m_hRocket;
};

static CHL2MPPortalSelfTest g_HL2MPPortalSelfTest;
