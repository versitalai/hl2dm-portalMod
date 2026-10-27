//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Linked portal pair for HL2MP (the "portal mod").
//
//			A portal is a flat ellipse lying on a world surface. When its
//			linked partner is also placed, anything that moves into it
//			(players, physics props, projectiles, NPCs) is moved out of the
//			partner with its position, orientation and velocity transformed.
//			Bullets are bent through portals in CBaseEntity::FireBullets, and
//			the client renders the view out of the partner into the portal.
//
//=============================================================================//

#ifndef HL2MP_PORTAL_H
#define HL2MP_PORTAL_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vmatrix.h"
#include "utlvector.h"

#ifdef CLIENT_DLL
#define CHL2MPPortal C_HL2MPPortal
#endif

class CHL2MPPortalWorld;	// hl2mp_portal_world.cpp

// Half extents of the portal ellipse (local right / local up).
#define HL2MP_PORTAL_HALF_WIDTH		32.0f
#define HL2MP_PORTAL_HALF_HEIGHT	56.0f

// Render depths that can show a live view through a portal (main view, and
// one level of portal-inside-portal).
#define HL2MP_PORTAL_MAX_DEPTH		2

// Max number of portals a bullet / portal shot may pass through.
#define HL2MP_PORTAL_MAX_RECURSION	4

enum
{
	HL2MP_PORTAL_BLUE = 0,
	HL2MP_PORTAL_ORANGE,
	HL2MP_PORTAL_COUNT
};

class CHL2MPPortal : public CBaseEntity
{
public:
	DECLARE_CLASS( CHL2MPPortal, CBaseEntity );
	DECLARE_NETWORKCLASS();

	CHL2MPPortal();
	~CHL2MPPortal();

	virtual void	Spawn( void );

	bool			IsActivated() const { return m_bActivated; }
	int				GetPortalType() const { return m_iPortalType; }
	CHL2MPPortal	*GetLinkedPortal() const { return m_hLinkedPortal.Get(); }

	// Both this portal and its partner are placed: things pass through.
	bool			IsOpen() const;

	// Surface frame. Forward is the surface normal (pointing out of the wall).
	void			GetPortalVectors( Vector *pForward, Vector *pRight, Vector *pUp ) const;
	Vector			GetSurfaceNormal() const;

	// Transform from this portal's space into the linked portal's space,
	// rotated 180 degrees around the portal's up axis: whatever goes into
	// the front of this portal comes out of the front of the linked one.
	VMatrix			MatrixThisToLinked() const;

	// Portals whose normal lies along a world axis (almost all walls, floors,
	// ceilings) let players move into them: the wall behind stops blocking and
	// the player goes through when their eye crosses the plane. Returns the
	// axis (0-2), the sign of the normal along it and the plane coordinate.
	bool			GetPlaneAxis( int *pAxis, float *pSign, float *pDist ) const;
	bool			CanCarve() const;
	// Does a hull (world center, half size) fit through the hole for movement?
	bool			HullFitsHole( const Vector &vecCenter, const Vector &vecHalf ) const;
	// Shift along the portal plane that puts a hull's footprint inside the hole
	// (zero when it already fits). False if the hull never fits. flSlack grows
	// the hole: 0.75 matches the collision carving (1 unit) for going in;
	// coming out needs the hull strictly inside, or a floor-level wall portal
	// leaves the feet in the floor.
	bool			GetHoleFitShift( const Vector &vecCenter, const Vector &vecHalf, Vector *pShift, float flSlack ) const;
	// Is there room for this hull just out of this portal? A blocked exit makes
	// the linked portal solid for movement instead of trapping the player.
	bool			HasRoomForHull( const Vector &vecMins, const Vector &vecMaxs, IHandleEntity *pIgnore ) const;

	// Is a point (near the portal plane) inside the ellipse? flScale grows/shrinks it.
	bool			IsPointInEllipse( const Vector &vPoint, float flScale = 1.0f ) const;
	// A point behind the portal, moved along the plane into the hole (ellipse
	// scaled by flScale) if it's outside it. Points in front are unchanged.
	Vector			ClampBehindPointToHole( const Vector &vPoint, float flScale ) const;

	// Changes whenever the portal's frame does (placed somewhere else); unique
	// across portals.
	int				GetFrameVersion() const { UpdateFrameCache(); return m_nFrameVersion; }
	// Where the portal's local collision world is kept (hl2mp_portal_world.cpp).
	CHL2MPPortalWorld *&LocalWorldStorage() const { return m_pLocalWorld; }

	// Global list of portals (both DLLs).
	static int				GetPortalCount();
	static CHL2MPPortal		*GetPortal( int i );

	// Find an open portal lying on the surface at vPoint (surface normal vNormal).
	static CHL2MPPortal		*FindPortalAtSurface( const Vector &vPoint, const Vector &vNormal, float flPlaneTolerance, CHL2MPPortal *pIgnore = NULL );

#ifdef GAME_DLL
	virtual int		UpdateTransmitState( void );
	virtual void	UpdateOnRemove( void );
	void			PortalThink( void );

	// Place the portal; returns false if it doesn't fit on the surface.
	bool			PlaceOnSurface( const Vector &vHitPos, const Vector &vNormal, const Vector &vViewForward );
	void			Fizzle( void );
	void			SetLinkedPortal( CHL2MPPortal *pLinked ) { m_hLinkedPortal = pLinked; }
	void			SetPortalType( int iType ) { m_iPortalType = iType; }

	// Should pEntity be moved through this portal this tick?
	bool			ShouldTeleport( CBaseEntity *pEntity );
	// Move an entity in front of this portal out of the linked one. bExact
	// maps it exactly (a prop straddling the plane stays straddling the exit)
	// instead of placing it clear of the exit.
	bool			TeleportEntity( CBaseEntity *pEntity, bool bExact = false );

	// Smooth prop crossing: a physics prop whose cross-section fits the hole
	// collides with the portal's local world instead of the real one (the
	// wall has a hole, the other side is behind it); it's moved to the other
	// side when its center crosses (still straddling, now in that portal's
	// local world), and collides with the real world again once it's clear of
	// the exit. The client draws a copy at the other portal while it's part
	// way through.
	bool			CanCrossSmoothly( CBaseEntity *pEntity );
	void			StartCrossing( CBaseEntity *pEntity );
	static bool		IsCrossing( CBaseEntity *pEntity );
	static bool		IsCrossingThrough( CBaseEntity *pEntity, const CHL2MPPortal *pPortal );
	static int		GetCrossingCount();
	static void		UpdateCrossings();

	// Add the far sides of portals the viewer can see to the PVS.
	static void		AddPortalsToPVS( unsigned char *pvs, int pvssize );

	// Recent teleports, newest last (for sv_portal_selftest and debugging).
	static bool		WasTeleported( CBaseEntity *pEntity );
	static int		CountTeleports( CBaseEntity *pEntity );
#endif

#ifdef CLIENT_DLL
	virtual bool	ShouldDraw( void );
	virtual bool	IsTransparent( void ) { return false; }
	virtual bool	IsTwoPass( void ) { return false; }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_OPAQUE_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );
	virtual bool	ShouldReceiveProjectedTextures( int flags ) { return false; }
	// Portals jump to where they're placed; the local collision world is built
	// from the exact networked frame, never from a position part way there.
	virtual bool	ShouldInterpolate() { return false; }

	// Render target slot holding this portal's view, per render depth
	// (0 = seen from the main view, 1 = seen inside another portal's view).
	// Valid while m_iViewStamp[d] == g_iHL2MPPortalViewStamp[d].
	int				m_iViewSlot[HL2MP_PORTAL_MAX_DEPTH];
	int				m_iViewStamp[HL2MP_PORTAL_MAX_DEPTH];

	// How deep the tunnel behind the surface goes (the wall's thickness, at
	// least deeper than the near plane); measured once per placement.
	float			GetTunnelDepth();

private:
	float			m_flTunnelDepth;
	int				m_nTunnelVersion;
#endif

private:
	// The portal's frame only changes when it's placed somewhere, but it's read
	// constantly (every player movement trace, bullet, held object, rendered
	// frame), so directions, plane axis and the to-linked matrix are cached and
	// rebuilt only when origin or angles change.
	void			UpdateFrameCache() const;
	mutable Vector	m_vecCacheOrigin;
	mutable QAngle	m_angCacheAngles;
	mutable Vector	m_vecCacheForward, m_vecCacheRight, m_vecCacheUp;
	mutable int		m_iCacheAxis;			// world axis of the normal, -1 if none
	mutable float	m_flCacheAxisSign;
	mutable int		m_nFrameVersion;		// unique across portals; 0 = not computed
	mutable VMatrix	m_matCacheToLinked;
	mutable int		m_nMatVersion;			// m_nFrameVersion it was built for
	mutable int		m_nMatLinkedVersion;	// and the linked portal's
	mutable int		m_nRoomTick;			// HasRoomForHull answer, one per tick
	mutable const IHandleEntity *m_pRoomIgnore;
	mutable Vector	m_vecRoomHalf;
	mutable bool	m_bRoom;
	mutable CHL2MPPortalWorld *m_pLocalWorld;

	CNetworkVar( bool, m_bActivated );
	// The portal's angles, sent exactly (the entity's own are quantized): the
	// client builds the same local collision world as the server from them.
	CNetworkQAngle( m_angPortal );
	CNetworkVar( int, m_iPortalType );
	CNetworkHandle( CHL2MPPortal, m_hLinkedPortal );
};

// Trace a ray (or hull), continuing it out of any open portal it hits on a
// world surface. Returns the number of portals passed through. *pTrace holds
// the final segment (startpos is where it left the last portal), *pVecDirOut
// the final direction. *pPath gets each segment as a start/end pair.
// pFilterAfterPortal, if given, replaces pFilter once the ray has gone through
// a portal (bullets can hit their shooter on the way back out). *pPortals
// gets the portals entered, in order.
int UTIL_HL2MPPortal_TraceRay( const Vector &vecStart, const Vector &vecDir, float flDist,
	unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace, Vector *pVecDirOut = NULL,
	const Vector *pHullMins = NULL, const Vector *pHullMaxs = NULL, CHL2MPPortal *pIgnorePortal = NULL,
	CUtlVector<Vector> *pPath = NULL, ITraceFilter *pFilterAfterPortal = NULL, CUtlVector<CHL2MPPortal *> *pPortals = NULL );

// Length of a path from UTIL_HL2MPPortal_TraceRay (sum of its segments).
float UTIL_HL2MPPortal_PathLength( const CUtlVector<Vector> &path );

#ifdef GAME_DLL
// +use through portals: FindUseEntity notes the portals it saw pUsed through,
// and picking pUsed up (in the same +use) holds it through them.
void HL2MPPortal_SetUsePortals( CBaseEntity *pUser, CBaseEntity *pUsed, const CUtlVector<CHL2MPPortal *> &portals );
int HL2MPPortal_GetUsePortals( CBaseEntity *pUser, CBaseEntity *pUsed, CHL2MPPortal **ppPortals, int nMax );
#endif

// Player movement through portals. Shared so client prediction matches the
// server. Trace a player hull that may be partly inside an open portal's hole:
// the wall behind the hole doesn't block, the part through the hole collides
// on the exit side. Returns false if no portal is involved (do a normal trace).
bool UTIL_HL2MPPortal_TracePlayerHull( const Vector &vecStart, const Vector &vecEnd, const Vector &vecMins, const Vector &vecMaxs,
	unsigned int mask, IHandleEntity *pIgnore, int collisionGroup, trace_t &pm );

// Funneling: a player heading into an axis-aligned portal a little off-center
// is moved sideways (this tick's share, from vecOrigin) so the hull lines up
// with the hole instead of catching the rim, like Portal's funneling. Zero if
// no portal needs it.
Vector UTIL_HL2MPPortal_PlayerFunnelMove( const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, const Vector &vecVelocity, float flFrameTime );

// The portal whose plane the eye crossed while moving from vecEyeStart to
// vecEye (with the hull inside its hole), or NULL.
CHL2MPPortal *UTIL_HL2MPPortal_FindPlayerTransition( const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, const Vector &vecEyeStart, const Vector &vecEye );

// The portal a hull partly through it was heading into fast and got stopped
// in (the part through the hole hit something in front of the exit), or NULL.
// Going feet first into a floor portal that comes out of a wall, the feet
// stick out of the wall well before the eye crosses.
CHL2MPPortal *UTIL_HL2MPPortal_FindBlockedEntry( const Vector &vecOrigin, const Vector &vecMins, const Vector &vecMaxs, const Vector &vecVelocityBefore, const Vector &vecVelocityAfter );

// The local collision world (hl2mp_portal_world.cpp). Near a carve-able portal,
// player movement is traced against it: the world around the portal with the
// hole cut out of the wall, and what's in front of the linked portal mapped in
// behind the plane. Built on demand, rebuilt when either portal moves.
extern ConVar sv_portal_local_world;
// Is a swept box (world centers, half size) inside the region it covers?
bool HL2MPPortalWorld_Contains( const CHL2MPPortal *pPortal, const Vector &vecCenterStart, const Vector &vecCenterEnd, const Vector &vecHalf );
// Trace against it (world geometry only; hits report the world entity).
// False if the portal has none.
bool HL2MPPortalWorld_Trace( const CHL2MPPortal *pPortal, const Ray_t &ray, unsigned int mask, trace_t *pTrace );
// Can props slide through (every uncut static prop / displacement in it is
// usable by physics whole)? Otherwise they pop through.
bool HL2MPPortalWorld_PropsCanCross( const CHL2MPPortal *pPortal, int *pUnsafe = NULL );
bool HL2MPPortalWorld_GetInfo( const CHL2MPPortal *pPortal, int *pLocalPieces, int *pRemotePieces, int *pStaticProps, int *pDisplacements, unsigned int *pChecksum );
void HL2MPPortalWorld_Destroy( CHL2MPPortalWorld *pWorld );
#ifdef GAME_DLL
// Make sure the portal's prop collider (the local world as static physics
// objects that only props crossing through it collide with) exists and is
// up to date. Returns its object count, -1 if the portal has no local world.
int HL2MPPortalWorld_SyncCollider( CHL2MPPortal *pPortal );
void HL2MPPortalWorld_RemoveCollider( CHL2MPPortal *pPortal );
#endif

// Release version from the mod folder's version.txt ("dev" without one).
const char *UTIL_HL2MPPortal_GetModVersion();
#ifdef GAME_DLL
// Sets sv_portal_mod_version (replicated) so clients can compare.
void UTIL_HL2MPPortal_PublishModVersion();
#else
// Chat warning when the server runs another release than this client.
void UTIL_HL2MPPortal_CheckServerVersion();
#endif

#ifdef GAME_DLL
// A portal trip can put a player in another part of the map in one tick, and
// the soundscape (ambient loops, announcements) restarts. Hold the current one
// for sv_portal_soundscape_hold seconds after a trip, so going back and forth
// doesn't restart it.
void UTIL_HL2MPPortal_NotePlayerTransit( CBasePlayer *pPlayer );
bool UTIL_HL2MPPortal_HoldSoundscape( CBasePlayer *pPlayer );

class CTakeDamageInfo;
// Explosions reach through portals: for each open portal the blast can see,
// the rest of its radius goes off again just out of the linked portal.
void UTIL_HL2MPPortal_RadiusDamage( const CTakeDamageInfo &info, const Vector &vecSrc, float flRadius, int iClassIgnore, CBaseEntity *pEntityIgnore );
#endif

#ifdef CLIENT_DLL
// Set while a portal view is being rendered into a render target.
extern bool g_bRenderingHL2MPPortalView;

// Depth of the view being rendered: 0 = main view, 1 = a portal's view,
// 2 = a portal's view inside a portal's view (portals there draw flat).
extern int g_iHL2MPPortalRenderDepth;

// Per depth: world->projection of the view at that depth (for mapping
// portal vertices to render target coordinates) and the current stamp.
extern VMatrix g_matHL2MPPortalWorldToProjection[HL2MP_PORTAL_MAX_DEPTH];
extern int g_iHL2MPPortalViewStamp[HL2MP_PORTAL_MAX_DEPTH];
extern float g_flHL2MPPortalUScale;
extern float g_flHL2MPPortalVScale;

// Render-to-texture slots allocated in CHL2MPRenderTargets: the first
// HL2MP_PORTAL_MAX_PRIMARY_VIEWS for portals seen from the main view, the
// rest for portals seen inside those.
#define HL2MP_PORTAL_MAX_PRIMARY_VIEWS	2
#define HL2MP_PORTAL_MAX_NESTED_VIEWS	2
#define HL2MP_PORTAL_MAX_VIEWS			( HL2MP_PORTAL_MAX_PRIMARY_VIEWS + HL2MP_PORTAL_MAX_NESTED_VIEWS )
const char *HL2MPPortal_GetRenderTargetName( int iSlot );
#endif

#endif // HL2MP_PORTAL_H
