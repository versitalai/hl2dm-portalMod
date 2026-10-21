//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Portal gun. Primary fires the blue portal, secondary the orange
//			one, reload closes both. Each gun owns one linked pair.
//
//=============================================================================//

#include "cbase.h"
#include "in_buttons.h"

#ifdef CLIENT_DLL
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
	#include "IEffects.h"
#endif

#include "weapon_hl2mpbasehlmpcombatweapon.h"
#include "hl2mp_portal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CWeaponPortalGun C_WeaponPortalGun
#endif

#define PORTALGUN_FIRE_DELAY	0.5f
#define PORTALGUN_RANGE			MAX_TRACE_LENGTH

class CWeaponPortalGun : public CBaseHL2MPCombatWeapon
{
	DECLARE_CLASS( CWeaponPortalGun, CBaseHL2MPCombatWeapon );
public:
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CWeaponPortalGun( void );

	virtual void	ItemPostFrame( void );
	virtual void	PrimaryAttack( void );
	virtual void	SecondaryAttack( void );
	virtual bool	Reload( void );
	virtual bool	HasAnyAmmo( void ) { return true; }
	virtual bool	CanBeSelected( void ) { return true; }

#ifndef CLIENT_DLL
	virtual void	UpdateOnRemove( void );
	DECLARE_ACTTABLE();
#endif

private:
	void			FirePortal( int iPortalType );

#ifndef CLIENT_DLL
	CHL2MPPortal	*GetPortal( int iPortalType );
	CHandle<CHL2MPPortal> m_hPortals[HL2MP_PORTAL_COUNT];
#endif

	CWeaponPortalGun( const CWeaponPortalGun & );
};

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponPortalGun, DT_WeaponPortalGun )

BEGIN_NETWORK_TABLE( CWeaponPortalGun, DT_WeaponPortalGun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CWeaponPortalGun )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_portalgun, CWeaponPortalGun );
PRECACHE_WEAPON_REGISTER( weapon_portalgun );

#ifndef CLIENT_DLL
acttable_t CWeaponPortalGun::m_acttable[] =
{
	{ ACT_HL2MP_IDLE,					ACT_HL2MP_IDLE_PHYSGUN,					false },
	{ ACT_HL2MP_RUN,					ACT_HL2MP_RUN_PHYSGUN,					false },
	{ ACT_HL2MP_IDLE_CROUCH,			ACT_HL2MP_IDLE_CROUCH_PHYSGUN,			false },
	{ ACT_HL2MP_WALK_CROUCH,			ACT_HL2MP_WALK_CROUCH_PHYSGUN,			false },
	{ ACT_HL2MP_GESTURE_RANGE_ATTACK,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PHYSGUN,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_HL2MP_GESTURE_RELOAD_PHYSGUN,		false },
	{ ACT_HL2MP_JUMP,					ACT_HL2MP_JUMP_PHYSGUN,					false },
};

IMPLEMENT_ACTTABLE( CWeaponPortalGun );
#endif

CWeaponPortalGun::CWeaponPortalGun( void )
{
	m_bReloadsSingly = false;
	m_bFiresUnderwater = true;
}

//-----------------------------------------------------------------------------
// Purpose: The gun has no ammo. The stock ItemPostFrame treats that as empty
//			(primary goes to HandleFireOnEmpty) and only reloads clip weapons,
//			so handle the buttons here, like the gravity gun does.
//-----------------------------------------------------------------------------
void CWeaponPortalGun::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	if ( ( pOwner->m_nButtons & IN_ATTACK ) && m_flNextPrimaryAttack <= gpGlobals->curtime )
	{
		PrimaryAttack();
	}
	else if ( ( pOwner->m_nButtons & IN_ATTACK2 ) && m_flNextSecondaryAttack <= gpGlobals->curtime )
	{
		SecondaryAttack();
	}
	else if ( pOwner->m_afButtonPressed & IN_RELOAD )
	{
		Reload();
	}
	else if ( !( pOwner->m_nButtons & ( IN_ATTACK | IN_ATTACK2 | IN_RELOAD ) ) )
	{
		WeaponIdle();
	}
}

void CWeaponPortalGun::PrimaryAttack( void )
{
	FirePortal( HL2MP_PORTAL_BLUE );
}

void CWeaponPortalGun::SecondaryAttack( void )
{
	FirePortal( HL2MP_PORTAL_ORANGE );
}

bool CWeaponPortalGun::Reload( void )
{
	if ( m_flNextPrimaryAttack > gpGlobals->curtime )
		return false;

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + PORTALGUN_FIRE_DELAY;

#ifndef CLIENT_DLL
	bool bAny = false;
	for ( int i = 0; i < HL2MP_PORTAL_COUNT; i++ )
	{
		if ( m_hPortals[i] && m_hPortals[i]->IsActivated() )
		{
			m_hPortals[i]->Fizzle();
			bAny = true;
		}
	}

	if ( bAny )
	{
		WeaponSound( SPECIAL1 );
	}
#endif

	return false;
}

void CWeaponPortalGun::FirePortal( int iPortalType )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + PORTALGUN_FIRE_DELAY;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );
	pPlayer->ViewPunch( QAngle( -2, random->RandomFloat( -1, 1 ), 0 ) );

#ifndef CLIENT_DLL
	CHL2MPPortal *pPortal = GetPortal( iPortalType );
	if ( !pPortal )
		return;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAim;
	pPlayer->EyeVectors( &vecAim );

	// Portal shots go through existing portals, except the one being moved.
	trace_t tr;
	Vector vecFinalDir;
	CTraceFilterSimple filter( pPlayer, COLLISION_GROUP_NONE );
	int nPortals = UTIL_HL2MPPortal_TraceRay( vecSrc, vecAim, PORTALGUN_RANGE, MASK_SHOT_PORTAL, &filter, &tr, &vecFinalDir, NULL, NULL, pPortal );

	bool bPlaced = false;
	if ( tr.fraction < 1.0f && !tr.startsolid && tr.DidHitWorld() && !( tr.surface.flags & ( SURF_SKY | SURF_SKY2D ) ) )
	{
		bPlaced = pPortal->PlaceOnSurface( tr.endpos, tr.plane.normal, vecFinalDir );
	}

	// Tracer along the final segment (from the muzzle if it didn't pass through a portal).
	if ( nPortals > 0 )
	{
		UTIL_Tracer( tr.startpos, tr.endpos, 0, TRACER_DONT_USE_ATTACHMENT, 6000.0f, false, "AR2Tracer" );
	}
	else
	{
		UTIL_Tracer( vecSrc, tr.endpos, entindex(), 1, 6000.0f, false, "AR2Tracer" );
	}

	if ( bPlaced )
	{
		WeaponSound( SINGLE );
		g_pEffects->Sparks( tr.endpos, 2, 2, &tr.plane.normal );
	}
	else
	{
		WeaponSound( EMPTY );
	}
#endif
}

#ifndef CLIENT_DLL

CHL2MPPortal *CWeaponPortalGun::GetPortal( int iPortalType )
{
	if ( !m_hPortals[iPortalType] )
	{
		CHL2MPPortal *pPortal = static_cast<CHL2MPPortal *>( CreateEntityByName( "prop_hl2mp_portal" ) );
		if ( !pPortal )
			return NULL;

		pPortal->SetPortalType( iPortalType );
		DispatchSpawn( pPortal );
		m_hPortals[iPortalType] = pPortal;

		// Link the pair as soon as both exist.
		CHL2MPPortal *pOther = m_hPortals[!iPortalType];
		if ( pOther )
		{
			pPortal->SetLinkedPortal( pOther );
			pOther->SetLinkedPortal( pPortal );
		}
	}

	return m_hPortals[iPortalType];
}

void CWeaponPortalGun::UpdateOnRemove( void )
{
	for ( int i = 0; i < HL2MP_PORTAL_COUNT; i++ )
	{
		if ( m_hPortals[i] )
		{
			UTIL_Remove( m_hPortals[i] );
			m_hPortals[i] = NULL;
		}
	}

	BaseClass::UpdateOnRemove();
}

#endif
