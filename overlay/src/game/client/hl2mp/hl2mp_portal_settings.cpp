//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: "Portal Settings" window (main menu entry, or the portal_settings
//			console command): portal quality and related options, each with a
//			plain explanation of what it does.
//
//=============================================================================//

#include "cbase.h"
#include "ienginevgui.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <KeyValues.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

struct PortalSettingChoice_t
{
	const char *pszLabel;
	const char *pszValue;
	const char *pszHelp;
};

static const PortalSettingChoice_t s_QualityChoices[] =
{
	{ "Auto (recommended)", "4", "Changes the quality by itself to keep your frame rate at the target below: a step lower when the game gets slow, a step higher when there's room." },
	{ "High", "3", "Up to 2 portals show a live view at full resolution, and portals seen inside a portal are live too. Slowest." },
	{ "Medium", "2", "Up to 2 portals show a live view at 75% resolution. Portals seen inside a portal, and portals smaller than 6% of the screen, are solid color." },
	{ "Low", "1", "Only the closest portal shows a live view, at 50% resolution, and only while it fills at least 12% of the screen. All others are solid color." },
	{ "Off (fastest)", "0", "No live views: portals are solid blue and orange. Portals cost almost nothing." },
};

static const PortalSettingChoice_t s_FpsChoices[] =
{
	{ "30 fps", "30", NULL }, { "45 fps", "45", NULL }, { "60 fps", "60", NULL },
	{ "75 fps", "75", NULL }, { "120 fps", "120", NULL }, { "144 fps", "144", NULL },
};

static const PortalSettingChoice_t s_ResolutionChoices[] =
{
	{ "100% (sharpest)", "1", NULL }, { "75%", "0.75", NULL }, { "50%", "0.5", NULL }, { "25% (fastest)", "0.25", NULL },
};

class CHL2MPPortalSettings : public Frame
{
	DECLARE_CLASS_SIMPLE( CHL2MPPortalSettings, Frame );

public:
	CHL2MPPortalSettings( VPANEL parent ) : BaseClass( NULL, "HL2MPPortalSettings" )
	{
		SetParent( parent );
		SetScheme( scheme()->LoadSchemeFromFile( "resource/SourceScheme.res", "SourceScheme" ) );
		SetTitle( "Portal Settings", true );
		SetSizeable( false );
		SetMoveable( true );
		SetDeleteSelfOnClose( false );

		int y = 36;
		m_pQuality = AddCombo( "Portal quality", s_QualityChoices, ARRAYSIZE( s_QualityChoices ), y );
		m_pQualityHelp = AddHelp( "", y, 48 );

		m_pAutoFps = AddCombo( "Auto: frame rate to keep", s_FpsChoices, ARRAYSIZE( s_FpsChoices ), y );
		AddHelp( "Only used by Auto. If the game is below this even with portals off, Auto stays at Off: pick a lower number, or a fixed quality.", y, 34 );

		m_pResolution = AddCombo( "Portal view resolution (max)", s_ResolutionChoices, ARRAYSIZE( s_ResolutionChoices ), y );
		AddHelp( "Portal views are drawn at this share of your screen resolution, then stretched to fit. Lower = blurrier portals, more fps on a weak graphics card. A limit on top of the quality above.", y, 48 );

		m_pDrawSelf = new CheckButton( this, "DrawSelf", "See yourself in portals" );
		Place( m_pDrawSelf, 20, y, 400, 24 ); y += 24;
		AddHelp( "Draws your own player model in portal views (small cost).", y, 20 );

		m_pDebug = new CheckButton( this, "Debug", "Show Auto's changes in the console" );
		Place( m_pDebug, 20, y, 400, 24 ); y += 24;
		AddHelp( "Prints a line every time Auto changes the quality, with your frame rate.", y, 20 );

		Button *pPotato = new Button( this, "Potato", "Low graphics for the whole game", this, "potato" );
		Place( pPotato, 20, y + 4, 280, 26 ); y += 34;
		AddHelp( "Runs cfg/potato.cfg: lowest textures, models, shadows, water and effects, and Auto portals at 45 fps. Saved; undo in Options > Video.", y, 34 );

		Button *pOK = new Button( this, "OK", "OK", this, "ok" );
		Button *pCancel = new Button( this, "Cancel", "Cancel", this, "Close" );
		Button *pApply = new Button( this, "Apply", "Apply", this, "apply" );
		Place( pOK, WINDOW_WIDE - 3 * 90 - 20, WINDOW_TALL - 40, 80, 26 );
		Place( pCancel, WINDOW_WIDE - 2 * 90 - 20, WINDOW_TALL - 40, 80, 26 );
		Place( pApply, WINDOW_WIDE - 90 - 20, WINDOW_TALL - 40, 80, 26 );
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		FOR_EACH_VEC( m_Layout, i )
		{
			const PanelLayout_t &layout = m_Layout[i];
			layout.pPanel->SetBounds( Scale( layout.x ), Scale( layout.y ), Scale( layout.wide ), Scale( layout.tall ) );
		}
	}

	void Open()
	{
		SelectValue( m_pQuality, s_QualityChoices, ARRAYSIZE( s_QualityChoices ), "cl_portal_quality" );
		SelectValue( m_pAutoFps, s_FpsChoices, ARRAYSIZE( s_FpsChoices ), "cl_portal_auto_fps" );
		SelectValue( m_pResolution, s_ResolutionChoices, ARRAYSIZE( s_ResolutionChoices ), "cl_portal_resolution" );
		m_pDrawSelf->SetSelected( ConVarRef( "cl_portal_draw_local_player" ).GetBool() );
		m_pDebug->SetSelected( ConVarRef( "cl_portal_quality_debug" ).GetBool() );
		UpdateQualityHelp();

		SetSize( Scale( WINDOW_WIDE ), Scale( WINDOW_TALL ) );
		MoveToCenterOfScreen();
		Activate();

		// The window is a child of the main menu panel: bring that up too, or
		// a portal_settings typed from a bind or the console is invisible in game.
		if ( !enginevgui->IsGameUIVisible() )
		{
			engine->ClientCmd_Unrestricted( "gameui_activate\n" );
		}
	}

	virtual void OnCommand( const char *pszCommand )
	{
		if ( !Q_stricmp( pszCommand, "apply" ) || !Q_stricmp( pszCommand, "ok" ) )
		{
			ApplySetting( m_pQuality, "cl_portal_quality" );
			ApplySetting( m_pAutoFps, "cl_portal_auto_fps" );
			ApplySetting( m_pResolution, "cl_portal_resolution" );
			ConVarRef( "cl_portal_draw_local_player" ).SetValue( m_pDrawSelf->IsSelected() ? 1 : 0 );
			ConVarRef( "cl_portal_quality_debug" ).SetValue( m_pDebug->IsSelected() ? 1 : 0 );
			engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
			if ( !Q_stricmp( pszCommand, "ok" ) )
			{
				Close();
			}
			return;
		}
		if ( !Q_stricmp( pszCommand, "potato" ) )
		{
			// The command buffer runs after this returns, so reopening right
			// away would still show the old values: reopen from the buffer.
			engine->ClientCmd_Unrestricted( "exec potato\nportal_settings\nhost_writeconfig\n" );
			return;
		}
		BaseClass::OnCommand( pszCommand );
	}

private:
	// Laid out in pixels at 1024x768. The window inherits proportional mode
	// from the main menu, where fonts grow with the screen height, so every
	// position and size has to grow with them.
	enum { WINDOW_WIDE = 620, WINDOW_TALL = 520 };

	struct PanelLayout_t
	{
		Panel	*pPanel;
		int		x, y, wide, tall;
	};

	void Place( Panel *pPanel, int x, int y, int wide, int tall )
	{
		PanelLayout_t layout = { pPanel, x, y, wide, tall };
		m_Layout.AddToTail( layout );
	}

	int Scale( int nValue )
	{
		if ( !IsProportional() )
			return nValue;
		// Proportional values are for a 480-tall screen.
		return scheme()->GetProportionalScaledValueEx( GetScheme(), nValue * 480 / 768 );
	}

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel )
	{
		if ( panel == m_pQuality )
		{
			UpdateQualityHelp();
		}
	}

	ComboBox *AddCombo( const char *pszTitle, const PortalSettingChoice_t *pChoices, int nChoices, int &y )
	{
		Label *pLabel = new Label( this, NULL, pszTitle );
		Place( pLabel, 20, y, 250, 24 );

		ComboBox *pCombo = new ComboBox( this, NULL, nChoices, false );
		Place( pCombo, 280, y, 300, 24 );
		for ( int i = 0; i < nChoices; i++ )
		{
			KeyValues *pData = new KeyValues( "data", "value", pChoices[i].pszValue );
			pCombo->AddItem( pChoices[i].pszLabel, pData );
			pData->deleteThis();
		}
		y += 28;
		return pCombo;
	}

	Label *AddHelp( const char *pszText, int &y, int nHeight )
	{
		Label *pHelp = new Label( this, NULL, pszText );
		Place( pHelp, 36, y, 560, nHeight );
		pHelp->SetWrap( true );
		pHelp->SetContentAlignment( Label::a_northwest );
		y += nHeight + 8;
		return pHelp;
	}

	void SelectValue( ComboBox *pCombo, const PortalSettingChoice_t *pChoices, int nChoices, const char *pszConVar )
	{
		float flValue = ConVarRef( pszConVar ).GetFloat();
		int iBest = 0;
		float flBest = FLT_MAX;
		for ( int i = 0; i < nChoices; i++ )
		{
			float flDiff = fabs( atof( pChoices[i].pszValue ) - flValue );
			if ( flDiff < flBest )
			{
				flBest = flDiff;
				iBest = i;
			}
		}
		pCombo->ActivateItemByRow( iBest );
	}

	void ApplySetting( ComboBox *pCombo, const char *pszConVar )
	{
		KeyValues *pData = pCombo->GetActiveItemUserData();
		if ( pData )
		{
			ConVarRef( pszConVar ).SetValue( pData->GetString( "value" ) );
		}
	}

	void UpdateQualityHelp()
	{
		KeyValues *pData = m_pQuality->GetActiveItemUserData();
		const char *pszValue = pData ? pData->GetString( "value" ) : "";
		for ( int i = 0; i < ARRAYSIZE( s_QualityChoices ); i++ )
		{
			if ( !Q_strcmp( s_QualityChoices[i].pszValue, pszValue ) )
			{
				m_pQualityHelp->SetText( s_QualityChoices[i].pszHelp );
			}
		}
	}

	ComboBox	*m_pQuality;
	Label		*m_pQualityHelp;
	ComboBox	*m_pAutoFps;
	ComboBox	*m_pResolution;
	CheckButton	*m_pDrawSelf;
	CheckButton	*m_pDebug;
	CUtlVector<PanelLayout_t> m_Layout;
};

static DHANDLE<CHL2MPPortalSettings> s_hPortalSettings;

CON_COMMAND( portal_settings, "Open the Portal Settings window." )
{
	if ( !s_hPortalSettings.Get() )
	{
		s_hPortalSettings = new CHL2MPPortalSettings( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
	}
	s_hPortalSettings->Open();
}
