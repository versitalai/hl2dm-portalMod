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
#include "hl2mp_player.h"
#include "player_pickup.h"
#include "usercmd.h"
#include "in_buttons.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar sv_portal_selftest( "sv_portal_selftest", "0", FCVAR_NONE, "Run the portal self-test on map load, print PORTALTEST lines, then quit." );

class CHL2MPPortalSelfTest : public CAutoGameSystemPerFrame
{
public:
	CHL2MPPortalSelfTest() : CAutoGameSystemPerFrame( "CHL2MPPortalSelfTest" )
	{
		m_iStep = -1;
		m_bBotWalk = false;
		m_nBotCommand = 0;
		m_flBotClosest = FLT_MAX;
		m_flNextBotLog = 0.0f;
		m_flTransitTime = -1.0f;
		m_bRocketThrough = false;
		m_bSnapFirst = m_bHeldAfterTransit = false;
	}

	virtual void LevelInitPostEntity()
	{
		m_iStep = sv_portal_selftest.GetBool() ? 0 : -1;
		m_flNextStep = gpGlobals->curtime + 2.0f;
		m_flRealDeadline = Plat_FloatTime() + 180.0f;
		m_nPass = m_nFail = 0;
	}

	virtual void FrameUpdatePostEntityThink()
	{
		if ( m_iStep < 0 )
			return;

		// The server only simulates (advances curtime, runs physics) with a
		// client connected, so connect the test bot first. It stays parked,
		// non-solid and in noclip, until the walk test needs it.
		if ( !m_hBot && !CreateBot() )
		{
			Result( "create_bot", false, "couldn't create a bot" );
			Finish();
			return;
		}

		DriveBot();

		// Things that go through and then blow up or fly off: note it while they exist.
		if ( m_hRocket && CHL2MPPortal::WasTeleported( m_hRocket ) )
		{
			m_bRocketThrough = true;
		}

		// Wall clock, not curtime: curtime stops if the server stops simulating.
		if ( Plat_FloatTime() > m_flRealDeadline )
		{
			Result( "timeout", false, "step %d, curtime %.1f", m_iStep, gpGlobals->curtime );
			Finish();
			return;
		}

		if ( gpGlobals->curtime < m_flNextStep )
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

	bool CreateBot()
	{
		edict_t *pEdict = engine->CreateFakeClient( "PortalTestBot" );
		CBasePlayer *pBot = pEdict ? ToBasePlayer( CBaseEntity::Instance( pEdict ) ) : NULL;
		if ( !pBot )
			return false;

		pBot->ClearFlags();
		pBot->AddFlag( FL_CLIENT | FL_FAKECLIENT );
		pBot->Spawn();
		pBot->SetMoveType( MOVETYPE_NOCLIP );
		pBot->AddSolidFlags( FSOLID_NOT_SOLID );
		pBot->AddEffects( EF_NODRAW );
		m_hBot = pBot;
		Msg( "PORTALTEST info bot connected\n" );
		return true;
	}

	// Keep the parked bot away from the test area: the farthest spawn point.
	void ParkBot()
	{
		CBasePlayer *pBot = ToBasePlayer( m_hBot );
		if ( !pBot )
			return;

		CBaseEntity *pBest = NULL;
		float flBest = -1.0f;
		for ( CBaseEntity *pSpot = gEntList.FindEntityByClassname( NULL, "info_player_deathmatch" ); pSpot; pSpot = gEntList.FindEntityByClassname( pSpot, "info_player_deathmatch" ) )
		{
			float flDist = ( pSpot->GetAbsOrigin() - m_vecFloor ).Length();
			if ( flDist > flBest )
			{
				flBest = flDist;
				pBest = pSpot;
			}
		}
		if ( pBest )
		{
			Vector vecSpot = pBest->GetAbsOrigin();
			pBot->Teleport( &vecSpot, NULL, &vec3_origin );
		}
	}

	// Walk the bot forward along m_angBot every frame and track how close it
	// gets to the exit portal.
	void DriveBot()
	{
		CBasePlayer *pBot = ToBasePlayer( m_hBot );
		if ( !pBot )
			return;

		CUserCmd cmd;
		cmd.command_number = ++m_nBotCommand;
		cmd.tick_count = gpGlobals->tickcount;
		cmd.viewangles = m_angBot;
		cmd.forwardmove = m_bBotWalk ? 200.0f : 0.0f;
		cmd.buttons = m_bBotWalk ? IN_FORWARD : 0;
		pBot->ProcessUsercmds( &cmd, 1, 1, 0, false );

		if ( !m_bBotWalk || !m_hFloor )
			return;

		float flDist = ( pBot->WorldSpaceCenter() - m_hFloor->GetAbsOrigin() ).Length();
		m_flBotClosest = MIN( m_flBotClosest, flDist );

		// Score the first time through only: walking on, the bot comes back
		// around off-center and snaps through, which is expected.
		CHL2_Player *pHL2Bot = static_cast<CHL2_Player *>( pBot );
		if ( m_flTransitTime < 0.0f && flDist < 80.0f )
		{
			m_flTransitTime = gpGlobals->curtime;
			m_bSnapFirst = CHL2MPPortal::WasTeleported( pBot );
			Msg( "PORTALTEST info bot came out of the exit, snapped=%d\n", m_bSnapFirst );
		}
		else if ( m_flTransitTime >= 0.0f && gpGlobals->curtime > m_flTransitTime + 0.5f )
		{
			m_bHeldAfterTransit = m_hHeldBox && pHL2Bot->IsHoldingEntity( m_hHeldBox );
			m_bBotWalk = false;
		}

		if ( gpGlobals->curtime > m_flNextBotLog )
		{
			m_flNextBotLog = gpGlobals->curtime + 0.25f;
			Vector v = pBot->GetAbsOrigin();
			Msg( "PORTALTEST info bot at (%.0f %.0f %.0f) vel %.0f dist_to_exit %.0f holding=%d\n", v.x, v.y, v.z, pBot->GetAbsVelocity().Length(), flDist,
				m_hHeldBox ? pHL2Bot->IsHoldingEntity( m_hHeldBox ) : -1 );

			// What would stop it moving forward 4 units right now?
			Vector vecFwd;
			AngleVectors( m_angBot, &vecFwd );
			vecFwd.z = 0;
			VectorNormalize( vecFwd );
			trace_t tr;
			bool bPortal = UTIL_HL2MPPortal_TracePlayerHull( v, v + vecFwd * 4, VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID, pBot, COLLISION_GROUP_PLAYER_MOVEMENT, tr );
			if ( !bPortal )
			{
				UTIL_TraceHull( v, v + vecFwd * 4, VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID, pBot, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
			}
			if ( tr.fraction < 1.0f || tr.startsolid )
			{
				Msg( "PORTALTEST info   blocked: carved=%d frac=%.2f startsolid=%d ent=%s normal (%.2f %.2f %.2f)\n", bPortal, tr.fraction, tr.startsolid,
					tr.m_pEnt ? tr.m_pEnt->GetClassname() : "none", tr.plane.normal.x, tr.plane.normal.y, tr.plane.normal.z );
			}
			if ( m_hHeldBox )
			{
				Vector b = m_hHeldBox->WorldSpaceCenter();
				Msg( "PORTALTEST info   box at (%.0f %.0f %.0f)\n", b.x, b.y, b.z );
			}
		}
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

			ParkBot();

			m_hWall = CreatePortal( HL2MP_PORTAL_BLUE );
			m_hFloor = CreatePortal( HL2MP_PORTAL_ORANGE );
			m_hWall->SetLinkedPortal( m_hFloor );
			m_hFloor->SetLinkedPortal( m_hWall );

			Result( "place_wall", m_hWall->PlaceOnSurface( m_vecWall, m_vecWallNormal, -m_vecWallNormal ) );

			// Small map props must stay server-side (stock HL2MP makes them
			// client-only, which the server can't move through portals).
			{
				CBaseEntity *pCan = CreateEntityByName( "prop_physics_multiplayer" );
				pCan->SetAbsOrigin( m_vecFloor + Vector( 48, 0, 16 ) );
				pCan->KeyValue( "model", "models/props_junk/popcan01a.mdl" );
				pCan->Precache();
				DispatchSpawn( pCan );
				m_hCan = pCan;
			}

			// A prop resting where the floor portal will open.
			m_hProp = CreateProp( m_vecFloor + Vector( 0, 0, 24 ) );
			Wait( 5.0f );	// let it settle and fall asleep
			return;

		case 1:
		{
			Result( "small_props_server_side", m_hCan.Get() != NULL && !m_hCan->IsMarkedForDeletion() );

			IPhysicsObject *pPhys = m_hProp ? m_hProp->VPhysicsGetObject() : NULL;
			m_vecPropStart = m_hProp ? m_hProp->GetAbsOrigin() : vec3_origin;
			Msg( "PORTALTEST info prop asleep=%d at (%.0f %.0f %.0f)\n", pPhys ? pPhys->IsAsleep() : -1, m_vecPropStart.x, m_vecPropStart.y, m_vecPropStart.z );

			// Open the floor portal right under where the prop came to rest.
			Vector vecUnder = m_vecFloor;
			if ( m_hProp )
			{
				trace_t tr;
				Vector vecCenter = m_hProp->WorldSpaceCenter();
				UTIL_TraceLine( vecCenter, vecCenter - Vector( 0, 0, 256 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
				if ( tr.fraction < 1.0f && tr.plane.normal.z > 0.9f )
				{
					vecUnder = tr.endpos;
				}
			}
			Result( "place_floor", m_hFloor->PlaceOnSurface( vecUnder, Vector( 0, 0, 1 ), Vector( 1, 0, 0 ) ) );
			Vector vecFloorPortal = m_hFloor->GetAbsOrigin();
			Msg( "PORTALTEST info floor portal at (%.0f %.0f %.0f) should_teleport_prop=%d\n", vecFloorPortal.x, vecFloorPortal.y, vecFloorPortal.z,
				m_hProp ? m_hFloor->ShouldTeleport( m_hProp ) : -1 );
			Wait( 1.5f );
			return;
		}

		case 2:
		{
			// Bug report: props at rest stayed frozen over a new portal.
			bool bMoved = m_hProp && ( m_hProp->GetAbsOrigin() - m_vecPropStart ).Length() > 64;
			Result( "sleeping_prop_falls_through", m_hProp && CHL2MPPortal::WasTeleported( m_hProp ) && bMoved,
				"teleported=%d moved=%d", m_hProp ? CHL2MPPortal::WasTeleported( m_hProp ) : 0, bMoved );
			if ( m_hProp )
			{
				Vector v = m_hProp->GetAbsOrigin();
				Msg( "PORTALTEST info prop now at (%.0f %.0f %.0f)\n", v.x, v.y, v.z );
				UTIL_Remove( m_hProp );
			}
			if ( m_hCan )
			{
				UTIL_Remove( m_hCan );
			}

			// Bug report: a held prop reaching a portal crashed the game.
			m_hHeldProp = CreateProp( m_hFloor->GetAbsOrigin() + Vector( 0, 0, 80 ) );
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
			if ( m_hHeldProp )
			{
				UTIL_Remove( m_hHeldProp );
			}

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
			// Start it inside the room (up to 300 units out, short of whatever is behind).
			trace_t trRoom;
			UTIL_TraceLine( m_hWall->GetAbsOrigin() + m_vecWallNormal * 8, m_hWall->GetAbsOrigin() + m_vecWallNormal * 300, MASK_SOLID, NULL, COLLISION_GROUP_NONE, &trRoom );
			float flRocketDist = MAX( 8.0f + ( 292.0f * trRoom.fraction ) - 32.0f, 48.0f );
			Msg( "PORTALTEST info rocket starts %.0f out\n", flRocketDist );
			m_hRocket = CMissile::Create( m_hWall->GetAbsOrigin() + m_vecWallNormal * flRocketDist, angRocket, NULL );
			if ( m_hRocket )
			{
				m_hRocket->SetAbsVelocity( -m_vecWallNormal * 1500 );
			}
			Wait( 1.0f );
			return;
		}

		case 4:
		{
			Result( "rocket_through_portal", m_bRocketThrough, "alive=%d", m_hRocket.Get() != NULL );

			// A player walking into the wall portal goes through in its own
			// movement (no snap teleport) and comes out of the floor portal.
			CBasePlayer *pBot = ToBasePlayer( m_hBot );
			if ( !pBot )
			{
				Result( "player_walks_through", false, "bot is gone" );
				Finish();
				return;
			}
			pBot->SetMoveType( MOVETYPE_WALK );
			pBot->RemoveSolidFlags( FSOLID_NOT_SOLID );
			pBot->RemoveEffects( EF_NODRAW );

			Vector vecStart = m_hWall->GetAbsOrigin() + m_vecWallNormal * 64;
			trace_t tr;
			UTIL_TraceHull( vecStart, vecStart - Vector( 0, 0, 256 ), VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID, pBot, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
			vecStart = tr.endpos;
			VectorAngles( -m_vecWallNormal, m_angBot );
			pBot->Teleport( &vecStart, &m_angBot, &vec3_origin );
			pBot->SnapEyeAngles( m_angBot );

			Vector vecBottom = m_hWall->GetAbsOrigin() - Vector( 0, 0, HL2MP_PORTAL_HALF_HEIGHT );
			Msg( "PORTALTEST info wall portal bottom z %.1f, bot feet z %.1f\n", vecBottom.z, vecStart.z );

			// The bot carries a box with +use; it must stay held through the portal.
			{
				CBaseEntity *pBox = CreateEntityByName( "prop_physics" );
				pBox->SetAbsOrigin( vecStart + Vector( 0, 0, 48 ) - m_vecWallNormal * 40 );
				pBox->KeyValue( "model", "models/props_junk/cardboard_box001a.mdl" );
				pBox->Precache();
				DispatchSpawn( pBox );
				pBox->Activate();
				PlayerPickupObject( pBot, pBox );
				m_hHeldBox = pBox;
				Msg( "PORTALTEST info bot holding box=%d\n", static_cast<CHL2_Player *>( pBot )->IsHoldingEntity( pBox ) );
			}

			m_bBotWalk = true;
			m_flBotClosest = FLT_MAX;
			Wait( 3.0f );
			return;
		}

		case 5:
		{
			bool bThrough = m_flTransitTime >= 0.0f;
			Result( "player_walks_through", bThrough, "closest_to_exit=%.0f", m_flBotClosest );
			Result( "player_through_seamless", bThrough && !m_bSnapFirst, "snap_teleported=%d", m_bSnapFirst );
			Result( "held_prop_stays_held_through_portal", bThrough && m_bHeldAfterTransit && m_hHeldBox && CHL2MPPortal::WasTeleported( m_hHeldBox ),
				"held=%d box_teleported=%d", m_bHeldAfterTransit, m_hHeldBox ? CHL2MPPortal::WasTeleported( m_hHeldBox ) : 0 );

			m_bBotWalk = false;
			m_hBot = NULL;
			Finish();
			return;
		}
		}
	}

	int		m_iStep;
	float	m_flNextStep;
	double	m_flRealDeadline;
	int		m_nPass, m_nFail;

	Vector	m_vecFloor, m_vecWall, m_vecWallNormal, m_vecPropStart;
	CHandle<CHL2MPPortal> m_hWall, m_hFloor;
	EHANDLE	m_hBot;
	QAngle	m_angBot;
	bool	m_bBotWalk;
	int		m_nBotCommand;
	float	m_flBotClosest;
	float	m_flNextBotLog;
	float	m_flTransitTime;
	bool	m_bSnapFirst;
	bool	m_bRocketThrough;
	bool	m_bHeldAfterTransit;
	EHANDLE	m_hProp, m_hHeldProp, m_hRocket, m_hCan, m_hHeldBox;
};

static CHL2MPPortalSelfTest g_HL2MPPortalSelfTest;

//-----------------------------------------------------------------------------
// Purpose: An empty dedicated server hibernates (no game frames), and HL2MP
//			has no sv_hibernate_when_empty. The engine asks the game's GC lobby
//			whether to hibernate, so keep the server awake during the self-test.
//-----------------------------------------------------------------------------
class CHL2MPSelfTestLobby : public IServerGCLobby
{
public:
	CHL2MPSelfTestLobby() : m_bLogged( false ) {}

	virtual bool HasLobby() const { return sv_portal_selftest.GetBool(); }
	virtual bool SteamIDAllowedToConnect( const CSteamID &steamId ) const { return true; }
	virtual void UpdateServerDetails( void ) {}
	virtual bool ShouldHibernate()
	{
		if ( !m_bLogged && sv_portal_selftest.GetBool() )
		{
			Msg( "PORTALTEST info engine asked to hibernate, staying awake\n" );
			m_bLogged = true;
		}
		return !sv_portal_selftest.GetBool();
	}
	virtual bool MatchAllowsNameChanges() { return true; }
	virtual bool GetPlayerGCMatchName( const CSteamID &steamId, char *pszOutGCMatchName, size_t nGCMatchNameLen ) { return false; }

private:
	bool m_bLogged;
};

static CHL2MPSelfTestLobby g_HL2MPSelfTestLobby;

IServerGCLobby *HL2MPPortal_GetSelfTestLobby()
{
	return &g_HL2MPSelfTestLobby;
}
