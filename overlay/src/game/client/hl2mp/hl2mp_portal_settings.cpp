//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: "Portal Settings" window (main menu entry, or the portal_settings
//			console command): portal quality and related options, each with a
//			plain explanation of what it does. Changes apply right away, and a
//			status line shows what's being drawn and the frame rate.
//
//=============================================================================//

#include "cbase.h"
#include "ienginevgui.h"
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/Button.h>
#include <KeyValues.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

extern void HL2MPPortal_GetRenderStatus( const char **ppszLevel, int *pnViews, float *pflResolution );

struct PortalSettingChoice_t
{
	const char *pszLabel;
	const char *pszValue;
	const char *pszHelp;
};

static const PortalSettingChoice_t s_QualityChoices[] =
{
	{ "Automatic - keeps your FPS up (recommended)", "4", "Picks the best look your PC can draw at the FPS below: a step down when the game gets slow, a step up when there's room." },
	{ "Best - sharpest, slowest", "3", "Two portals show a live view at full sharpness, and portals seen inside a portal are live too." },
	{ "Balanced", "2", "Two portals show a live view at 75% sharpness. Portals inside a portal, and far away ones, are solid color." },
	{ "Fast", "1", "Only the closest portal shows a live view, at 50% sharpness, and only when it's big on screen. Others are solid color." },
	{ "Off - solid colors, fastest", "0", "No live views: portals are plain blue and orange. Costs almost nothing." },
};

static const PortalSettingChoice_t s_FpsChoices[] =
{
	{ "30", "30", NULL }, { "45", "45", NULL }, { "60", "60", NULL },
	{ "75", "75", NULL }, { "120", "120", NULL }, { "144", "144", NULL },
};

static const PortalSettingChoice_t s_ResolutionChoices[] =
{
	{ "Full (100%)", "1", NULL }, { "High (75%)", "0.75", NULL }, { "Medium (50%)", "0.5", NULL }, { "Low (25%)", "0.25", NULL },
};

// Settings the window owns, saved on open so Cancel can put them back.
static const char *s_pszPortalSettingConVars[] =
{
	"cl_portal_quality", "cl_portal_auto_fps", "cl_portal_resolution", "cl_portal_draw_local_player",
	"cl_physcannon_predict_held", "cl_portal_quality_debug",
	// Console-only overrides that would quietly undo the choices above.
	"cl_portal_render_views", "cl_portal_max_views", "cl_portal_recursion",
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
		m_bLoading = false;
		m_flAvgFrameTime = 0.0f;

		m_pStatus = AddHelp( "" );

		m_pQuality = AddCombo( "How portals look", s_QualityChoices, ARRAYSIZE( s_QualityChoices ) );
		m_pQualityHelp = AddHelp( "" );

		m_pAutoFps = AddCombo( "FPS to keep (Automatic only)", s_FpsChoices, ARRAYSIZE( s_FpsChoices ) );
		AddHelp( "If your FPS is below this even with portals off, Automatic stays at Off: pick a lower number." );

		m_pResolution = AddCombo( "Portal view sharpness (max)", s_ResolutionChoices, ARRAYSIZE( s_ResolutionChoices ) );
		AddHelp( "Live views are drawn smaller than your screen and stretched to fit. Lower is blurrier and faster on a weak graphics card." );

		m_pDrawSelf = AddCheck( "Show my own body in portals" );
		AddHelp( "Small cost." );

		m_pPredictHeld = AddCheck( "Snappier gravity gun hold (can shake)" );
		AddHelp( "Off: what you hold follows the server's smooth position, a moment behind. On: your PC moves it too (stock Half-Life 2), which can make it shake." );

		m_pDebug = AddCheck( "Print Automatic's changes in the console" );

		Button *pPotato = new Button( this, "Potato", "Lowest graphics for the whole game", this, "potato" );
		AddRow( ROW_BUTTON, NULL, pPotato );
		AddHelp( "Runs cfg/potato.cfg: lowest textures, models, shadows, water and effects, and Automatic portals at 45 FPS. Undo in Options > Video." );

		m_pOK = new Button( this, "OK", "OK", this, "ok" );
		m_pCancel = new Button( this, "Cancel", "Cancel", this, "cancel" );

		ivgui()->AddTickSignal( GetVPanel(), 250 );
	}

	void Open()
	{
		for ( int i = 0; i < ARRAYSIZE( s_pszPortalSettingConVars ); i++ )
		{
			ConVarRef var( s_pszPortalSettingConVars[i] );
			m_SavedValues[i] = var.IsValid() ? var.GetString() : "";
		}

		m_bLoading = true;
		SelectValue( m_pQuality, s_QualityChoices, ARRAYSIZE( s_QualityChoices ), "cl_portal_quality" );
		SelectValue( m_pAutoFps, s_FpsChoices, ARRAYSIZE( s_FpsChoices ), "cl_portal_auto_fps" );
		SelectValue( m_pResolution, s_ResolutionChoices, ARRAYSIZE( s_ResolutionChoices ), "cl_portal_resolution" );
		m_pDrawSelf->SetSelected( ConVarRef( "cl_portal_draw_local_player" ).GetBool() );
		m_pPredictHeld->SetSelected( ConVarRef( "cl_physcannon_predict_held" ).GetBool() );
		m_pDebug->SetSelected( ConVarRef( "cl_portal_quality_debug" ).GetBool() );
		m_bLoading = false;
		UpdateQualityHelp();
		UpdateStatus();

		InvalidateLayout( true );
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
		if ( !Q_stricmp( pszCommand, "ok" ) )
		{
			Close();
			return;
		}
		if ( !Q_stricmp( pszCommand, "cancel" ) )
		{
			for ( int i = 0; i < ARRAYSIZE( s_pszPortalSettingConVars ); i++ )
			{
				ConVarRef var( s_pszPortalSettingConVars[i] );
				if ( var.IsValid() )
				{
					var.SetValue( m_SavedValues[i].String() );
				}
			}
			BaseClass::OnClose();
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

	// Closing (OK or the X) keeps what's applied and saves it.
	virtual void OnClose()
	{
		engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
		BaseClass::OnClose();
	}

	virtual void OnTick()
	{
		BaseClass::OnTick();
		float flFrameTime = MIN( gpGlobals->absoluteframetime, 0.25f );
		m_flAvgFrameTime = ( m_flAvgFrameTime > 0.0f ) ? Lerp( 0.3f, m_flAvgFrameTime, flFrameTime ) : flFrameTime;
		if ( IsVisible() )
		{
			UpdateStatus();
		}
	}

	// Sized from the font, not fixed pixels: the main menu's fonts grow with
	// the screen (proportional) or step up by resolution, so every row, wrap
	// and the window itself are measured in lines and characters of the font
	// actually in use.
	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		IScheme *pScheme = scheme()->GetIScheme( GetScheme() );
		HFont hFont = pScheme ? pScheme->GetFont( "Default", IsProportional() ) : INVALID_FONT;
		int nLine = ( hFont != INVALID_FONT ) ? surface()->GetFontTall( hFont ) : 14;
		nLine = MAX( nLine, 10 );
		int nChar = ( hFont != INVALID_FONT ) ? surface()->GetCharacterWidth( hFont, 'n' ) : 7;
		nChar = MAX( nChar, 5 );

		int nScreenWide, nScreenTall;
		surface()->GetScreenSize( nScreenWide, nScreenTall );

		int nMargin = nLine;
		int nWide = MIN( nChar * 78, nScreenWide * 9 / 10 );
		int nInner = nWide - 2 * nMargin;
		int nRow = nLine * 3 / 2 + 4;
		int nIndent = nLine;

		int x, y, wide, tall;
		GetClientArea( x, y, wide, tall );
		y += nLine / 2;

		FOR_EACH_VEC( m_Rows, i )
		{
			const Row_t &row = m_Rows[i];
			switch ( row.eType )
			{
			case ROW_COMBO:
				row.pLabel->SetBounds( nMargin, y, nInner * 2 / 5, nRow );
				row.pControl->SetBounds( nMargin + nInner * 2 / 5, y, nInner * 3 / 5, nRow );
				y += nRow + 2;
				break;
			case ROW_CHECK:
				row.pControl->SetBounds( nMargin, y, nInner, nRow );
				y += nRow;
				break;
			case ROW_BUTTON:
				row.pControl->SetBounds( nMargin, y + 4, MIN( nInner, nChar * 40 ), nRow );
				y += nRow + 6;
				break;
			case ROW_HELP:
			{
				int nTextWide = nInner - nIndent;
				int nLines = CountLines( static_cast<Label *>( row.pControl ), hFont, nTextWide );
				int nHeight = nLines * nLine + 2;
				row.pControl->SetBounds( nMargin + nIndent, y, nTextWide, nHeight );
				y += nHeight + nLine / 2;
				break;
			}
			}
		}

		int nButtonWide = nChar * 12;
		y += nLine / 2;
		m_pOK->SetBounds( nWide - nMargin - 2 * nButtonWide - nLine / 2, y, nButtonWide, nRow );
		m_pCancel->SetBounds( nWide - nMargin - nButtonWide, y, nButtonWide, nRow );
		y += nRow + nMargin;

		int nTall = MIN( y, nScreenTall );
		if ( GetWide() != nWide || GetTall() != nTall )
		{
			SetSize( nWide, nTall );
			MoveToCenterOfScreen();
		}
	}

private:
	enum RowType_t { ROW_COMBO, ROW_CHECK, ROW_BUTTON, ROW_HELP };

	struct Row_t
	{
		RowType_t	eType;
		Label		*pLabel;
		Panel		*pControl;
	};

	void AddRow( RowType_t eType, Label *pLabel, Panel *pControl )
	{
		Row_t row = { eType, pLabel, pControl };
		m_Rows.AddToTail( row );
	}

	// Lines a word-wrapped label needs at this width (measured word by word).
	int CountLines( Label *pLabel, HFont hFont, int nWide )
	{
		wchar_t wszText[1024];
		pLabel->GetText( wszText, sizeof( wszText ) );
		if ( !wszText[0] || hFont == INVALID_FONT )
			return 1;

		int nSpace = surface()->GetCharacterWidth( hFont, L' ' );
		int nLines = 1, nX = 0;
		const wchar_t *pWord = wszText;
		while ( *pWord )
		{
			const wchar_t *pEnd = pWord;
			int nWord = 0;
			while ( *pEnd && *pEnd != L' ' )
			{
				nWord += surface()->GetCharacterWidth( hFont, *pEnd );
				pEnd++;
			}
			if ( nX > 0 && nX + nSpace + nWord > nWide )
			{
				nLines++;
				nX = nWord;
			}
			else
			{
				nX += ( nX > 0 ? nSpace : 0 ) + nWord;
			}
			pWord = *pEnd ? pEnd + 1 : pEnd;
		}
		return nLines;
	}

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel )
	{
		if ( panel == m_pQuality )
		{
			UpdateQualityHelp();
		}
		ApplyAll();
	}

	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel )
	{
		ApplyAll();
	}

	ComboBox *AddCombo( const char *pszTitle, const PortalSettingChoice_t *pChoices, int nChoices )
	{
		Label *pLabel = new Label( this, NULL, pszTitle );
		ComboBox *pCombo = new ComboBox( this, NULL, nChoices, false );
		for ( int i = 0; i < nChoices; i++ )
		{
			KeyValues *pData = new KeyValues( "data", "value", pChoices[i].pszValue );
			pCombo->AddItem( pChoices[i].pszLabel, pData );
			pData->deleteThis();
		}
		AddRow( ROW_COMBO, pLabel, pCombo );
		return pCombo;
	}

	CheckButton *AddCheck( const char *pszText )
	{
		CheckButton *pCheck = new CheckButton( this, NULL, pszText );
		AddRow( ROW_CHECK, NULL, pCheck );
		return pCheck;
	}

	Label *AddHelp( const char *pszText )
	{
		Label *pHelp = new Label( this, NULL, pszText );
		pHelp->SetWrap( true );
		pHelp->SetContentAlignment( Label::a_northwest );
		AddRow( ROW_HELP, NULL, pHelp );
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

	void ApplyAll()
	{
		if ( m_bLoading )
			return;
		ApplySetting( m_pQuality, "cl_portal_quality" );
		ApplySetting( m_pAutoFps, "cl_portal_auto_fps" );
		ApplySetting( m_pResolution, "cl_portal_resolution" );
		ConVarRef( "cl_portal_draw_local_player" ).SetValue( m_pDrawSelf->IsSelected() ? 1 : 0 );
		ConVarRef( "cl_physcannon_predict_held" ).SetValue( m_pPredictHeld->IsSelected() ? 1 : 0 );
		ConVarRef( "cl_portal_quality_debug" ).SetValue( m_pDebug->IsSelected() ? 1 : 0 );
		// The quality above decides these; console leftovers would override it.
		ConVarRef( "cl_portal_render_views" ).SetValue( 1 );
		ConVarRef( "cl_portal_max_views" ).SetValue( 2 );
		ConVarRef( "cl_portal_recursion" ).SetValue( 1 );
		UpdateStatus();
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
		InvalidateLayout();
	}

	void UpdateStatus()
	{
		const char *pszLevel;
		int nViews;
		float flResolution;
		HL2MPPortal_GetRenderStatus( &pszLevel, &nViews, &flResolution );
		int nFps = ( m_flAvgFrameTime > 0.0f ) ? (int)( 1.0f / m_flAvgFrameTime + 0.5f ) : 0;

		char szStatus[256];
		if ( !engine->IsInGame() )
		{
			Q_snprintf( szStatus, sizeof( szStatus ), "Right now: not in a game. Join or start one to see what portals cost." );
		}
		else if ( nViews > 0 )
		{
			Q_snprintf( szStatus, sizeof( szStatus ), "Right now: up to %d live portal view%s at %d%% sharpness (%s), %d FPS.",
				nViews, nViews == 1 ? "" : "s", (int)( flResolution * 100.0f + 0.5f ), pszLevel, nFps );
		}
		else
		{
			Q_snprintf( szStatus, sizeof( szStatus ), "Right now: portals drawn as solid colors (%s), %d FPS.", pszLevel, nFps );
		}
		m_pStatus->SetText( szStatus );
	}

	ComboBox	*m_pQuality;
	Label		*m_pQualityHelp;
	ComboBox	*m_pAutoFps;
	ComboBox	*m_pResolution;
	CheckButton	*m_pDrawSelf;
	CheckButton	*m_pPredictHeld;
	CheckButton	*m_pDebug;
	Label		*m_pStatus;
	Button		*m_pOK;
	Button		*m_pCancel;
	CUtlVector<Row_t> m_Rows;
	CUtlString	m_SavedValues[ARRAYSIZE( s_pszPortalSettingConVars )];
	bool		m_bLoading;
	float		m_flAvgFrameTime;
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
