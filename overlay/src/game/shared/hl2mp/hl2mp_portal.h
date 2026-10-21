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

	// Is a point (near the portal plane) inside the ellipse? flScale grows/shrinks it.
	bool			IsPointInEllipse( const Vector &vPoint, float flScale = 1.0f ) const;

	// Global list of portals (both DLLs).
	static int				GetPortalCount();
	static CHL2MPPortal		*GetPortal( int i );

	// Find an open portal lying on the surface at vPoint (surface normal vNormal).
	static CHL2MPPortal		*FindPortalAtSurface( const Vector &vPoint, const Vector &vNormal, float flPlaneTolerance, CHL2MPPortal *pIgnore = NULL );

#ifdef GAME_DLL
	virtual int		UpdateTransmitState( void );
	void			PortalThink( void );

	// Place the portal; returns false if it doesn't fit on the surface.
	bool			PlaceOnSurface( const Vector &vHitPos, const Vector &vNormal, const Vector &vViewForward );
	void			Fizzle( void );
	void			SetLinkedPortal( CHL2MPPortal *pLinked ) { m_hLinkedPortal = pLinked; }
	void			SetPortalType( int iType ) { m_iPortalType = iType; }

	// Should pEntity be moved through this portal this tick?
	bool			ShouldTeleport( CBaseEntity *pEntity );
	// Move an entity in front of this portal out of the linked one.
	bool			TeleportEntity( CBaseEntity *pEntity );

	// Add the far sides of portals the viewer can see to the PVS.
	static void		AddPortalsToPVS( unsigned char *pvs, int pvssize );

	// Recent teleports, newest last (for sv_portal_selftest and debugging).
	static bool		WasTeleported( CBaseEntity *pEntity );
#endif

#ifdef CLIENT_DLL
	virtual bool	ShouldDraw( void );
	virtual bool	IsTransparent( void ) { return false; }
	virtual bool	IsTwoPass( void ) { return false; }
	virtual RenderGroup_t GetRenderGroup( void ) { return RENDER_GROUP_OPAQUE_ENTITY; }
	virtual void	GetRenderBounds( Vector &mins, Vector &maxs );
	virtual int		DrawModel( int flags );
	virtual bool	ShouldReceiveProjectedTextures( int flags ) { return false; }

	// Render target slot holding this portal's view, per render depth
	// (0 = seen from the main view, 1 = seen inside another portal's view).
	// Valid while m_iViewStamp[d] == g_iHL2MPPortalViewStamp[d].
	int				m_iViewSlot[HL2MP_PORTAL_MAX_DEPTH];
	int				m_iViewStamp[HL2MP_PORTAL_MAX_DEPTH];
#endif

private:
	CNetworkVar( bool, m_bActivated );
	CNetworkVar( int, m_iPortalType );
	CNetworkHandle( CHL2MPPortal, m_hLinkedPortal );
};

// Trace a ray (or hull), continuing it out of any open portal it hits on a
// world surface. Returns the number of portals passed through. *pTrace holds
// the final segment (startpos is where it left the last portal), *pVecDirOut
// the final direction. *pPath gets each segment as a start/end pair.
// pFilterAfterPortal, if given, replaces pFilter once the ray has gone through
// a portal (bullets can hit their shooter on the way back out).
int UTIL_HL2MPPortal_TraceRay( const Vector &vecStart, const Vector &vecDir, float flDist,
	unsigned int mask, ITraceFilter *pFilter, trace_t *pTrace, Vector *pVecDirOut = NULL,
	const Vector *pHullMins = NULL, const Vector *pHullMaxs = NULL, CHL2MPPortal *pIgnorePortal = NULL,
	CUtlVector<Vector> *pPath = NULL, ITraceFilter *pFilterAfterPortal = NULL );

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
