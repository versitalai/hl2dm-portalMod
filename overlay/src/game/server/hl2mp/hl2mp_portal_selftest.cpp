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
#include "explode.h"

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
		m_bScenario = false;
		m_flBotForward = 0.0f;
		m_flAttackUntil = 0.0f;
		m_flExitSpeed = -1.0f;
		m_bTrackExitSpeed = false;
		m_hExitTrack = NULL;
		m_flFastFallMin = FLT_MAX;
		m_flHeldLogUntil = m_flNextHeldLog = 0.0f;
		m_flTickLogUntil = 0.0f;
		m_flLoopStart = -1.0f;
		m_flPeakEarly = m_flPeakLate = -FLT_MAX;
		m_flLastVz = 0.0f;
		m_flLastZ = m_flLastTime = 0.0f;
		m_flScenarioClosestF = m_flScenarioClosestW = FLT_MAX;
		m_nSnapsBefore = m_nBoxTeleportsBefore = 0;
	}

	virtual void LevelInitPostEntity()
	{
		m_iStep = sv_portal_selftest.GetBool() ? 0 : -1;
		m_flNextStep = gpGlobals->curtime + 2.0f;
		m_flRealDeadline = Plat_FloatTime() + 240.0f;
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

	// Scenarios after the walk test: the bot is placed, then driven by these
	// fields every frame, and what the checks need is recorded.
	void PlaceBot( const Vector &vecOrigin, const QAngle &angles, const Vector &vecVelocity )
	{
		CBasePlayer *pBot = ToBasePlayer( m_hBot );
		if ( !pBot )
			return;
		pBot->SetMoveType( MOVETYPE_WALK );
		pBot->RemoveSolidFlags( FSOLID_NOT_SOLID );
		pBot->Teleport( &vecOrigin, &angles, &vecVelocity );
		pBot->SnapEyeAngles( angles );
		pBot->SetGroundEntity( NULL );
		m_angBot = angles;
		m_flBotForward = 0.0f;
		m_flScenarioClosestF = m_flScenarioClosestW = FLT_MAX;
		m_nSnapsBefore = CHL2MPPortal::CountTeleports( pBot );
	}

	// Player origin standing on the floor below vecPoint.
	Vector FloorBelow( const Vector &vecPoint )
	{
		trace_t tr;
		UTIL_TraceHull( vecPoint + Vector( 0, 0, 32 ), vecPoint - Vector( 0, 0, 160 ), VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID, m_hBot, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );
		return tr.endpos;
	}

	int BotSnaps()
	{
		return CHL2MPPortal::CountTeleports( m_hBot ) - m_nSnapsBefore;
	}

	void DriveScenario( CBasePlayer *pBot )
	{
		CUserCmd cmd;
		cmd.command_number = ++m_nBotCommand;
		cmd.tick_count = gpGlobals->tickcount;
		cmd.viewangles = m_angBot;
		cmd.forwardmove = m_flBotForward;
		cmd.buttons = ( m_flBotForward > 0.0f ) ? IN_FORWARD : 0;
		if ( gpGlobals->curtime < m_flAttackUntil )
			cmd.buttons |= IN_ATTACK;
		pBot->ProcessUsercmds( &cmd, 1, 1, 0, false );

		Vector vecCenter = pBot->WorldSpaceCenter();
		if ( m_hFloor )
			m_flScenarioClosestF = MIN( m_flScenarioClosestF, ( vecCenter - m_hFloor->GetAbsOrigin() ).Length() );
		if ( m_hWall )
			m_flScenarioClosestW = MIN( m_flScenarioClosestW, ( vecCenter - m_hWall->GetAbsOrigin() ).Length() );

		CHL2MPPortal *pExitTrack = m_hExitTrack ? m_hExitTrack.Get() : m_hFloor.Get();
		if ( m_bTrackExitSpeed && m_flExitSpeed < 0.0f && pExitTrack && ( vecCenter - pExitTrack->GetAbsOrigin() ).Length() < 100.0f )
		{
			m_flExitSpeed = pBot->GetAbsVelocity().Length();
		}

		if ( gpGlobals->curtime < m_flTickLogUntil )
		{
			Vector p = pBot->GetAbsOrigin(), v = pBot->GetAbsVelocity();
			Msg( "PORTALTEST tick t=%.3f bot (%.1f %.1f %.1f) vel (%.0f %.0f %.0f) ground=%s\n", gpGlobals->curtime, p.x, p.y, p.z, v.x, v.y, v.z,
				pBot->GetGroundEntity() ? pBot->GetGroundEntity()->GetClassname() : "none" );
		}

		if ( gpGlobals->curtime < m_flHeldLogUntil && gpGlobals->curtime >= m_flNextHeldLog && m_hHeldBox && m_hFloor )
		{
			m_flNextHeldLog = gpGlobals->curtime + 0.25f;
			extern bool PhysCannon_GetHeldObjectTarget( CBaseEntity *pObject, Vector *pTarget );
			Vector vecTarget( 0, 0, 0 );
			bool bTarget = PhysCannon_GetHeldObjectTarget( m_hHeldBox, &vecTarget );
			Vector b = m_hHeldBox->WorldSpaceCenter();
			Vector vecNormal = m_hFloor->GetSurfaceNormal();
			Msg( "PORTALTEST info held box at (%.0f %.0f %.0f) %.0f off the floor portal, target %d (%.0f %.0f %.0f) %.0f off it, should_teleport=%d\n",
				b.x, b.y, b.z, DotProduct( b - m_hFloor->GetAbsOrigin(), vecNormal ), bTarget, vecTarget.x, vecTarget.y, vecTarget.z,
				DotProduct( vecTarget - m_hFloor->GetAbsOrigin(), vecNormal ), m_hFloor->ShouldTeleport( m_hHeldBox ) );
		}

		if ( m_flLoopStart > 0.0f )
		{
			float flZ = pBot->GetAbsOrigin().z;
			float flVz = pBot->GetAbsVelocity().z;
			if ( m_flLastVz > 0.0f && flVz <= 0.0f )
			{
				Msg( "PORTALTEST info loop apex t=%.2f z=%.1f\n", gpGlobals->curtime - m_flLoopStart, flZ - m_hFloor->GetAbsOrigin().z );
			}
			// Height change velocity doesn't explain (a nudge, not a fall).
			float flDt = gpGlobals->curtime - m_flLastTime;
			if ( m_flLastTime > 0.0f && flDt > 0.0f )
			{
				float flErr = ( flZ - m_flLastZ ) - 0.5f * ( flVz + m_flLastVz ) * flDt;
				if ( fabs( flErr ) > 1.0f )
				{
					Msg( "PORTALTEST info loop jump t=%.3f z %.1f -> %.1f vz %.0f -> %.0f (unexplained %.1f)\n",
						gpGlobals->curtime, m_flLastZ, flZ, m_flLastVz, flVz, flErr );
				}
			}
			m_flLastZ = flZ;
			m_flLastTime = gpGlobals->curtime;
			m_flLastVz = flVz;
			// Skip the first 3 s (it speeds up to the minimum exit speed), then
			// compare the next 3 s with the 3 after: a loop must not gain height.
			float flT = gpGlobals->curtime - m_flLoopStart;
			if ( flT >= 3.0f && flT < 6.0f )
				m_flPeakEarly = MAX( m_flPeakEarly, flZ );
			else if ( flT >= 6.0f )
				m_flPeakLate = MAX( m_flPeakLate, flZ );
		}
	}

	// Walk the bot forward along m_angBot every frame and track how close it
	// gets to the exit portal.
	void DriveBot()
	{
		CBasePlayer *pBot = ToBasePlayer( m_hBot );
		if ( !pBot )
			return;

		if ( m_bScenario )
		{
			DriveScenario( pBot );
			return;
		}

		CUserCmd cmd;
		cmd.command_number = ++m_nBotCommand;
		cmd.tick_count = gpGlobals->tickcount;
		cmd.viewangles = m_angBot;
		cmd.forwardmove = m_bBotWalk ? 200.0f : 0.0f;
		cmd.buttons = m_bBotWalk ? IN_FORWARD : 0;

		// Until it's through, steer onto the wall portal's center line (like a
		// player aiming for it), so the test measures centered walk-ins.
		if ( m_bBotWalk && m_flTransitTime < 0.0f && m_hWall )
		{
			Vector vecRight;
			AngleVectors( m_angBot, NULL, &vecRight, NULL );
			float flOffset = DotProduct( pBot->GetAbsOrigin() - m_hWall->GetAbsOrigin(), vecRight );
			cmd.sidemove = clamp( -flOffset * 20.0f, -200.0f, 200.0f );
			if ( cmd.sidemove != 0.0f )
				cmd.buttons |= ( cmd.sidemove > 0.0f ) ? IN_MOVERIGHT : IN_MOVELEFT;
		}
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
			Msg( "PORTALTEST info wall portal at (%.1f %.1f %.1f) bottom z %.1f, bot feet z %.1f\n", m_hWall->GetAbsOrigin().x, m_hWall->GetAbsOrigin().y, m_hWall->GetAbsOrigin().z, vecBottom.z, vecStart.z );

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
			CHL2_Player *pBot = static_cast<CHL2_Player *>( ToBasePlayer( m_hBot ) );
			pBot->ForceDropOfCarriedPhysObjects( NULL );
			if ( m_hHeldBox )
				UTIL_Remove( m_hHeldBox );

			// Bug reports: snapping when coming in at an angle / off-center.
			// Start beside the hole's center line (more than the hull's room
			// in it) and walk in turned a bit further away: the funnel has to
			// line it up, and it must go through without a server snap.
			m_bScenario = true;
			Vector vecRight;
			m_hWall->GetPortalVectors( NULL, &vecRight, NULL );
			Vector vecStart = FloorBelow( m_hWall->GetAbsOrigin() + m_vecWallNormal * 96.0f + vecRight * 22.0f );
			QAngle angIn;
			VectorAngles( -m_vecWallNormal - vecRight * 0.27f, angIn );		// ~15 degrees away from the center
			angIn.x = 0;
			PlaceBot( vecStart, angIn, vec3_origin );
			m_flBotForward = 200.0f;
			Wait( 2.5f );
			return;
		}

		case 6:
		{
			Result( "angled_offcenter_walk_in_no_snap", m_flScenarioClosestF < 80.0f && BotSnaps() == 0,
				"closest_to_exit=%.0f snaps=%d", m_flScenarioClosestF, BotSnaps() );

			// Bug report: speed lost going through fast. Fling into the wall
			// portal at 900 u/s; it must come out of the floor portal still fast.
			Vector vecStart = m_hWall->GetAbsOrigin() + m_vecWallNormal * 120.0f - Vector( 0, 0, 36 );
			QAngle angIn;
			VectorAngles( -m_vecWallNormal, angIn );
			PlaceBot( vecStart, angIn, -m_vecWallNormal * 900.0f );
			m_flExitSpeed = -1.0f;
			m_bTrackExitSpeed = true;
			Wait( 1.5f );
			return;
		}

		case 7:
		{
			m_bTrackExitSpeed = false;
			Result( "high_speed_keeps_speed", m_flExitSpeed >= 700.0f && BotSnaps() == 0, "exit_speed=%.0f snaps=%d", m_flExitSpeed, BotSnaps() );

			// Bug report: a held prop near a portal snaps back and forth. Stand
			// so the hold point is just past the plane, hold still for 3 s.
			QAngle angIn;
			VectorAngles( -m_vecWallNormal, angIn );
			PlaceBot( FloorBelow( m_hWall->GetAbsOrigin() + m_vecWallNormal * 58.0f ), angIn, vec3_origin );
			CBasePlayer *pBot = ToBasePlayer( m_hBot );
			CBaseEntity *pBox = CreateEntityByName( "prop_physics" );
			pBox->SetAbsOrigin( pBot->EyePosition() - m_vecWallNormal * 30.0f );
			pBox->KeyValue( "model", "models/props_junk/cardboard_box001a.mdl" );
			pBox->Precache();
			DispatchSpawn( pBox );
			pBox->Activate();
			PlayerPickupObject( pBot, pBox );
			m_hHeldBox = pBox;
			m_nBoxTeleportsBefore = CHL2MPPortal::CountTeleports( pBox );
			Wait( 3.0f );
			return;
		}

		case 8:
		{
			CHL2_Player *pBot = static_cast<CHL2_Player *>( ToBasePlayer( m_hBot ) );
			int nBoxTeleports = m_hHeldBox ? CHL2MPPortal::CountTeleports( m_hHeldBox ) - m_nBoxTeleportsBefore : -1;
			bool bHeld = m_hHeldBox && pBot->IsHoldingEntity( m_hHeldBox );
			Result( "held_prop_no_flip_flop", bHeld && nBoxTeleports >= 0 && nBoxTeleports <= 1, "held=%d box_teleports=%d", bHeld, nBoxTeleports );

			// Bug report: hard to bring a held object back through. Turn well
			// away from the portal: the box, held through it, has to come back.
			m_nBoxTeleportsBefore = m_hHeldBox ? CHL2MPPortal::CountTeleports( m_hHeldBox ) : 0;
			m_angBot.y += 75.0f;
			ToBasePlayer( m_hBot )->SnapEyeAngles( m_angBot );	// a bot's usercmd angles don't override a snap
			m_flHeldLogUntil = gpGlobals->curtime + 3.0f;
			Wait( 3.0f );
			return;
		}

		case 9:
		{
			CHL2_Player *pBot = static_cast<CHL2_Player *>( ToBasePlayer( m_hBot ) );
			int nBack = m_hHeldBox ? CHL2MPPortal::CountTeleports( m_hHeldBox ) - m_nBoxTeleportsBefore : -1;
			bool bHeld = m_hHeldBox && pBot->IsHoldingEntity( m_hHeldBox );
			float flDist = m_hHeldBox ? ( m_hHeldBox->WorldSpaceCenter() - pBot->EyePosition() ).Length() : -1.0f;
			Result( "held_prop_comes_back", bHeld && nBack >= 1 && flDist < 120.0f, "held=%d teleports=%d dist=%.0f", bHeld, nBack, flDist );
			pBot->ForceDropOfCarriedPhysObjects( NULL );
			if ( m_hHeldBox )
				UTIL_Remove( m_hHeldBox );

			// Bug report: the stunstick doesn't hit through a portal. Swing a
			// crowbar into the wall portal at a crate held still above the
			// floor portal (on the other side).
			CBaseCombatWeapon *pCrowbar = pBot->Weapon_OwnsThisType( "weapon_crowbar" );
			if ( !pCrowbar )
				pCrowbar = static_cast<CBaseCombatWeapon *>( pBot->GiveNamedItem( "weapon_crowbar" ) );
			if ( pCrowbar )
				pBot->Weapon_Switch( pCrowbar );

			QAngle angIn;
			VectorAngles( -m_vecWallNormal, angIn );
			PlaceBot( FloorBelow( m_hWall->GetAbsOrigin() + m_vecWallNormal * 30.0f ), angIn, vec3_origin );

			CBaseEntity *pCrate = CreateEntityByName( "prop_physics" );
			pCrate->SetAbsOrigin( m_hFloor->GetAbsOrigin() + Vector( 0, 0, 30 ) );
			pCrate->KeyValue( "model", "models/props_junk/wood_crate001a.mdl" );
			pCrate->KeyValue( "health", "100" );
			pCrate->KeyValue( "spawnflags", "8" );	// motion disabled: stays over the portal
			pCrate->Precache();
			DispatchSpawn( pCrate );
			pCrate->Activate();
			m_hCrate = pCrate;
			m_iCrateHealth = pCrate->GetHealth();
			Wait( 1.5f );	// let the crowbar finish deploying
			return;
		}

		case 10:
		{
			CBasePlayer *pBot = ToBasePlayer( m_hBot );
			Msg( "PORTALTEST info swinging %s\n", pBot->GetActiveWeapon() ? pBot->GetActiveWeapon()->GetClassname() : "nothing" );
			m_flAttackUntil = gpGlobals->curtime + 1.0f;
			Wait( 1.5f );
			return;
		}

		case 11:
		{
			int iHealth = m_hCrate ? m_hCrate->GetHealth() : -1;
			Result( "melee_through_portal", !m_hCrate || iHealth < m_iCrateHealth, "crate_health %d -> %d", m_iCrateHealth, iHealth );
			if ( m_hCrate )
				UTIL_Remove( m_hCrate );

			// Bug report: standing on the edge of a floor portal snaps you back
			// and forth. Center well outside the hole, part of the hull over it:
			// must just stand there.
			Vector vecRight;
			m_hFloor->GetPortalVectors( NULL, &vecRight, NULL );
			PlaceBot( m_hFloor->GetAbsOrigin() + vecRight * 46.0f + Vector( 0, 0, 1 ), m_angBot, vec3_origin );
			m_vecEdgeStart = ToBasePlayer( m_hBot )->GetAbsOrigin();
			Wait( 2.0f );
			return;
		}

		case 12:
		{
			float flMoved = ( ToBasePlayer( m_hBot )->GetAbsOrigin() - m_vecEdgeStart ).Length();
			Result( "floor_edge_stands_still", flMoved < 4.0f && BotSnaps() == 0, "moved=%.1f snaps=%d", flMoved, BotSnaps() );

			// Mostly over the hole, a little off: funneled in, through to the
			// wall portal, no snap.
			Vector vecRight;
			m_hFloor->GetPortalVectors( NULL, &vecRight, NULL );
			PlaceBot( m_hFloor->GetAbsOrigin() + vecRight * 24.0f + Vector( 0, 0, 1 ), m_angBot, vec3_origin );
			Wait( 2.0f );
			return;
		}

		case 13:
		{
			Result( "floor_portal_funnels_in", m_flScenarioClosestW < 100.0f && BotSnaps() == 0, "closest_to_other=%.0f snaps=%d", m_flScenarioClosestW, BotSnaps() );

			// Bug report: walking off a floor portal's edge got you stuck (it
			// pulled you back in). Start part over the hole and walk away.
			Vector vecRight;
			m_hFloor->GetPortalVectors( NULL, &vecRight, NULL );
			QAngle angOut;
			VectorAngles( vecRight, angOut );
			PlaceBot( m_hFloor->GetAbsOrigin() + vecRight * 20.0f + Vector( 0, 0, 0.1f ), angOut, vec3_origin );	// on the floor, not dropping onto it
			m_flBotForward = 200.0f;
			// Half a second: walking on in this direction leads into the wall portal.
			Wait( 0.5f );
			return;
		}

		case 14:
		{
			Vector vecFromHole = ToBasePlayer( m_hBot )->GetAbsOrigin() - m_hFloor->GetAbsOrigin();
			vecFromHole.z = 0.0f;
			Result( "floor_edge_walk_off", m_flScenarioClosestW >= 100.0f && vecFromHole.Length() > 50.0f && BotSnaps() == 0,
				"closest_to_other=%.0f now %.0f from the hole snaps=%d", m_flScenarioClosestW, vecFromHole.Length(), BotSnaps() );
			m_flBotForward = 0.0f;
		}
			// Fall through: first drop.
		case 15:
		case 16:
		case 17:
		{
			// Bug report: falling fast sometimes stops you. Drop into the floor
			// portal at 1200 u/s (center, then off-center both ways along the
			// portal's up axis); it comes out of the wall portal, whose bottom is
			// at floor level. It must keep its speed.
			if ( m_iStep > 15 && m_bTrackExitSpeed )
			{
				m_flFastFallMin = MIN( m_flFastFallMin, MAX( m_flExitSpeed, 0.0f ) );
				Vector v = ToBasePlayer( m_hBot )->GetAbsOrigin();
				Msg( "PORTALTEST info fast fall %d exit_speed=%.0f, bot now at (%.0f %.0f %.0f) speed %.0f\n", m_iStep - 15, m_flExitSpeed,
					v.x, v.y, v.z, ToBasePlayer( m_hBot )->GetAbsVelocity().Length() );
			}
			if ( m_iStep <= 17 )
			{
				Vector vecUp;
				m_hFloor->GetPortalVectors( NULL, NULL, &vecUp );
				float flOffsets[3] = { 0.0f, 24.0f, -24.0f };
				Vector vecFloorSpot = m_hFloor->GetAbsOrigin() + vecUp * flOffsets[m_iStep - 15];
				// As high as the room allows (the 1200 u/s start does the rest).
				trace_t trUp;
				UTIL_TraceHull( vecFloorSpot + Vector( 0, 0, 2 ), vecFloorSpot + Vector( 0, 0, 300 ), VEC_HULL_MIN, VEC_HULL_MAX, MASK_PLAYERSOLID, m_hBot, COLLISION_GROUP_PLAYER_MOVEMENT, &trUp );
				float flHeight = MAX( ( trUp.endpos.z - vecFloorSpot.z ) - 4.0f, 2.0f );
				Msg( "PORTALTEST info fast fall %d from %.0f up\n", m_iStep - 14, flHeight );
				PlaceBot( vecFloorSpot + Vector( 0, 0, flHeight ), m_angBot, Vector( 0, 0, -1200 ) );
				m_flTickLogUntil = gpGlobals->curtime + 0.3f;
				ConVarRef( "sv_portal_debug_moves" ).SetValue( 1 );
				m_hExitTrack = m_hWall;
				m_flExitSpeed = -1.0f;
				m_bTrackExitSpeed = true;
				Wait( 1.2f );
				return;
			}
		}
			// Fall through after the last drop.
		{
			m_bTrackExitSpeed = false;
			m_hExitTrack = NULL;
			ConVarRef( "sv_portal_debug_moves" ).SetValue( 0 );
			Result( "fast_fall_keeps_speed", m_flFastFallMin >= 1000.0f && BotSnaps() == 0, "slowest_exit_speed=%.0f snaps=%d", m_flFastFallMin, BotSnaps() );

			// Bug report: walking into a portal whose exit is blocked froze you,
			// even in the air. Block the wall portal's exit with a crate; the
			// floor portal must then be plain floor.
			CBaseEntity *pBlocker = CreateEntityByName( "prop_dynamic_override" );
			pBlocker->SetAbsOrigin( m_hWall->GetAbsOrigin() + m_vecWallNormal * 24.0f );
			pBlocker->KeyValue( "model", "models/props_junk/wood_crate001a.mdl" );
			pBlocker->KeyValue( "solid", "6" );
			pBlocker->Precache();
			DispatchSpawn( pBlocker );
			pBlocker->Activate();
			m_hBlocker = pBlocker;
			PlaceBot( m_hFloor->GetAbsOrigin() + Vector( 0, 0, 40 ), m_angBot, vec3_origin );
			Wait( 2.0f );
			return;
		}

		case 18:
		{
			CBasePlayer *pBot = ToBasePlayer( m_hBot );
			float flAbove = pBot->GetAbsOrigin().z - m_hFloor->GetAbsOrigin().z;
			float flSpeed = pBot->GetAbsVelocity().Length();
			Result( "blocked_exit_acts_solid", fabs( flAbove ) < 3.0f && flSpeed < 5.0f && m_flScenarioClosestW > 60.0f && BotSnaps() == 0,
				"height_over_portal=%.1f speed=%.0f closest_to_exit=%.0f snaps=%d", flAbove, flSpeed, m_flScenarioClosestW, BotSnaps() );
			if ( m_hBlocker )
				UTIL_Remove( m_hBlocker );

			// Bug report: explosions next to a portal don't go through. Blast in
			// front of the wall portal, out of direct reach of a crate over the
			// floor portal (but within reach through the portals).
			ParkBot();
			CBaseEntity *pCrate = CreateEntityByName( "prop_physics" );
			pCrate->SetAbsOrigin( m_hFloor->GetAbsOrigin() + Vector( 0, 0, 40 ) );
			pCrate->KeyValue( "model", "models/props_junk/wood_crate001a.mdl" );
			pCrate->KeyValue( "health", "100" );
			pCrate->KeyValue( "spawnflags", "8" );	// motion disabled: stays over the portal
			pCrate->Precache();
			DispatchSpawn( pCrate );
			pCrate->Activate();
			m_hCrate = pCrate;
			m_iCrateHealth = pCrate->GetHealth();
			Wait( 0.5f );	// a new entity isn't in the spatial partition (blast queries) right away
			return;
		}

		case 19:
		{
			Vector vecBlast = m_hWall->GetAbsOrigin() + m_vecWallNormal * 40.0f;
			if ( m_hCrate )
			{
				Msg( "PORTALTEST info blast %.0f from the crate directly\n", ( vecBlast - m_hCrate->WorldSpaceCenter() ).Length() );
			}
			ConVarRef( "sv_portal_debug_moves" ).SetValue( 1 );
			ExplosionCreate( vecBlast, vec3_angle, NULL, 100, 150, true );
			ConVarRef( "sv_portal_debug_moves" ).SetValue( 0 );
			Wait( 0.5f );
			return;
		}

		case 20:
		{
			int iHealth = m_hCrate ? m_hCrate->GetHealth() : -1;
			Result( "explosion_through_portal", !m_hCrate || iHealth < m_iCrateHealth, "crate_health %d -> %d", m_iCrateHealth, iHealth );
			if ( m_hCrate )
				UTIL_Remove( m_hCrate );

			// Bug report: two floor portals: you go higher and higher. Move the
			// wall portal onto the floor nearby and drop the bot into one.
			Vector vecRight, vecUp;
			m_hFloor->GetPortalVectors( NULL, &vecRight, &vecUp );
			bool bPlaced = false;
			Vector vecDirs[4] = { vecRight, -vecRight, vecUp, -vecUp };
			for ( int i = 0; i < 4 && !bPlaced; i++ )
			{
				for ( float flDist = 160.0f; flDist <= 280.0f && !bPlaced; flDist += 60.0f )
				{
					Vector vecSpot = m_hFloor->GetAbsOrigin() + vecDirs[i] * flDist;
					trace_t tr;
					UTIL_TraceLine( vecSpot + Vector( 0, 0, 64 ), vecSpot - Vector( 0, 0, 64 ), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
					if ( tr.fraction < 1.0f && !tr.startsolid && tr.plane.normal.z > 0.99f && fabs( tr.endpos.z - m_hFloor->GetAbsOrigin().z ) < 16.0f )
					{
						bPlaced = m_hWall->PlaceOnSurface( tr.endpos, Vector( 0, 0, 1 ), Vector( 1, 0, 0 ) );
					}
				}
			}
			if ( !bPlaced )
			{
				Result( "floor_loop_no_height_gain", false, "no second floor spot" );
				Finish();
				return;
			}
			Vector v = m_hWall->GetAbsOrigin();
			Msg( "PORTALTEST info second floor portal at (%.0f %.0f %.0f)\n", v.x, v.y, v.z );

			PlaceBot( m_hFloor->GetAbsOrigin() + Vector( 0, 0, 1 ), m_angBot, vec3_origin );
			m_flLoopStart = gpGlobals->curtime;
			m_flLastVz = m_flLastZ = m_flLastTime = 0.0f;
			ConVarRef( "sv_portal_debug_moves" ).SetValue( 1 );
			m_flPeakEarly = m_flPeakLate = -FLT_MAX;
			Wait( 9.0f );
			return;
		}

		case 21:
		{
			m_flLoopStart = -1.0f;
			ConVarRef( "sv_portal_debug_moves" ).SetValue( 0 );
			float flFloor = m_hFloor->GetAbsOrigin().z;
			Result( "floor_loop_no_height_gain", m_flPeakLate <= m_flPeakEarly + 4.0f && BotSnaps() == 0,
				"peak_3_to_6s=%.1f peak_6_to_9s=%.1f (above floor) snaps=%d", m_flPeakEarly - flFloor, m_flPeakLate - flFloor, BotSnaps() );

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
	EHANDLE	m_hProp, m_hHeldProp, m_hRocket, m_hCan, m_hHeldBox, m_hCrate;

	// Scenarios
	bool	m_bScenario;
	float	m_flBotForward;
	float	m_flAttackUntil;
	float	m_flExitSpeed;
	bool	m_bTrackExitSpeed;
	float	m_flLoopStart;
	float	m_flPeakEarly, m_flPeakLate;
	float	m_flLastVz, m_flLastZ, m_flLastTime;
	float	m_flScenarioClosestF, m_flScenarioClosestW;
	int		m_nSnapsBefore, m_nBoxTeleportsBefore;
	int		m_iCrateHealth;
	Vector	m_vecEdgeStart;
	CHandle<CHL2MPPortal> m_hExitTrack;
	float	m_flHeldLogUntil, m_flNextHeldLog;
	float	m_flTickLogUntil;	// exit speed is measured near this portal (default floor)
	float	m_flFastFallMin;
	EHANDLE	m_hBlocker;
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
