//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2MP client render targets: the base set plus the portal views.
//
//=============================================================================//

#include "cbase.h"
#include "baseclientrendertargets.h"
#include "materialsystem/imaterialsystem.h"
#include "hl2mp_portal.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CHL2MPRenderTargets : public CBaseClientRenderTargets
{
	DECLARE_CLASS_GAMEROOT( CHL2MPRenderTargets, CBaseClientRenderTargets );
public:
	virtual void InitClientRenderTargets( IMaterialSystem* pMaterialSystem, IMaterialSystemHardwareConfig* pHardwareConfig );
	virtual void ShutdownClientRenderTargets();

private:
	// One full-screen-sized target per portal view rendered each frame.
	CTextureReference m_PortalViewTextures[HL2MP_PORTAL_MAX_VIEWS];
};

void CHL2MPRenderTargets::InitClientRenderTargets( IMaterialSystem* pMaterialSystem, IMaterialSystemHardwareConfig* pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );

	for ( int i = 0; i < HL2MP_PORTAL_MAX_VIEWS; i++ )
	{
		m_PortalViewTextures[i].Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2(
			HL2MPPortal_GetRenderTargetName( i ),
			1, 1, RT_SIZE_FULL_FRAME_BUFFER,
			pMaterialSystem->GetBackBufferFormat(),
			MATERIAL_RT_DEPTH_SHARED,
			TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
			CREATERENDERTARGETFLAGS_HDR ) );
	}
}

void CHL2MPRenderTargets::ShutdownClientRenderTargets()
{
	BaseClass::ShutdownClientRenderTargets();

	for ( int i = 0; i < HL2MP_PORTAL_MAX_VIEWS; i++ )
	{
		m_PortalViewTextures[i].Shutdown();
	}
}

static CHL2MPRenderTargets g_HL2MPRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHL2MPRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_HL2MPRenderTargets );
