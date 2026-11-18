/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	Knight Demo main window, window procedure, timing and surface creation.	   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include "Util.h"
#include "Watchers.h"
#include "DirectDraw.h"

#include "Ati3dCIFx.h"
#include "Matrix.h"
#include "Multimedia.h"

#include "AtiDemoWnd.h"
#include "AtiDemo.h"
#include "Polygon.h"
#include "physics.h"
#include "normalmode.h"

// -----------------------------------------------------------------------------

int		g_frameCount = 0;
int		g_framesPerSecond = 0;

#ifdef ALLOW_LOCKSTEP
BOOL	s_drawDone = FALSE;
#endif

BOOL	g_blockPaint = FALSE;
BOOL	g_blockPhysics = FALSE;
BOOL	g_stepPhysics = FALSE;
BOOL	g_screenShot = FALSE;
int		g_mode = 0;

BOOL	g_toggleCifDirectMode = FALSE;

static const char* WndClassName = "AtiDemoWndClass";

extern int  g_agpVidMemState;

extern Vector g_keyboardFlightPosition;
extern Matrix g_keyboardFlightRotation;

extern BOOL g_lookatMode;

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Timing handling. LOCKSTEP will wait for rendering of a frame to complete   *
 *	before processing a physics tick, so that graphics ticks alternate		   *
 *	with physics ticks.														   *
 *******************************************************************************/
void CALLBACK TickHandler(UINT, UINT, DWORD, DWORD, DWORD)
{
	Multimedia::PollJoysticks();

#ifdef ALLOW_LOCKSTEP
	if((!g_lockStep) || s_drawDone)
	{
#endif
		g_physics.Tick();
#ifdef ALLOW_LOCKSTEP
		s_drawDone = FALSE;
	}
#endif
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Clean up and release all buffers, set the original display mode and		   *
 *	reset the cooperative level so that we are sharing resources with		   *
 *	Windows properly (like the GDI), and then destroy the window.			   *
 *******************************************************************************/
AtiDemoWnd::~AtiDemoWnd()
{
	m_pZBuffer->Release();
	m_pZBuffer = NULL;

	m_pBackBuffer->Release();
	m_pBackBuffer = NULL;

	m_pFrontBuffer->Release();
	m_pFrontBuffer = NULL;

	m_pDD->RestoreDisplayMode();
	m_pDD->SetCooperativeLevel(NULL, DDSCL_NORMAL);
	DestroyWindow(m_hWnd);

	m_pDD->Release();
}


/*******************************************************************************
 *	Create a window and surfaces. The surfaces necessary are primary, back	   *
 *	buffer and Z-buffer. 													   *
 *******************************************************************************/
AtiDemoWnd::AtiDemoWnd(IDirectDraw& rDD)
{
	BOOL success = FALSE;

	m_pDD = &rDD;
	m_pDD->AddRef();

	WNDCLASS wndclass;
	wndclass.style = CS_NOCLOSE;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = sizeof(AtiDemoWnd*);
	wndclass.hInstance = g_hInstance;
	wndclass.hIcon = NULL;
	wndclass.hCursor = NULL;
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = WndClassName;
	if(RegisterClass(&wndclass) == NULL) THROW_EXCEPTION();

	m_pPaintFn = PaintBlack;

	m_hWnd = CreateWindow(WndClassName,
						  "AtiDemo",
						  WS_POPUP,
						  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
						  NULL,
						  NULL,
						  g_hInstance,
						  (void*)this);
	if(m_hWnd == NULL) THROW_EXCEPTION();
	DECLARE_FUNCTION_WATCHER_WINAPI(HWND, BOOL, m_hWnd, DestroyWindow, success, m_hWnd);

	//Set cooperative level to give us full screen access and exclusive control
	//of Windows resources
	HRESULT result = m_pDD->SetCooperativeLevel(m_hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);

	//Set display mode to 640x480, 16-bit color
	result = m_pDD->SetDisplayMode(640, 480, 16);
	if(result != DD_OK)
	{
		MessageBox(NULL, "Could not set 640x480x16", "Error", MB_OK | MB_ICONERROR | MB_TASKMODAL);
		THROW_DD_EXCEPTION(result);
	}

	//Fill out a surface description that gives us a primary buffer (automatically
	//sized to the current screen resolution), 1 back buffer, and one Z buffer. 
	//Buffers are always created in video memory. If AGP is enabled, we direct the
	//surfaces to be created using on-board video memory.
	DDSurfaceDesc desc;
	desc.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
	desc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY | DDAgp(DDSCAPS_LOCALVIDMEM) | DDSCAPS_COMPLEX | DDSCAPS_FLIP;
	desc.dwBackBufferCount = 1;
	result = m_pDD->CreateSurface(&desc, &m_pFrontBuffer, NULL);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
	DECLARE_MEMBER_WATCHER_WINAPI(IDirectDrawSurface, ULONG, *m_pFrontBuffer, Release, success, m_pFrontBuffer);

	DDSCAPS ddscaps;
	ddscaps.dwCaps = DDSCAPS_BACKBUFFER;
	result = m_pFrontBuffer->GetAttachedSurface(&ddscaps, &m_pBackBuffer);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
	DECLARE_MEMBER_WATCHER_WINAPI(IDirectDrawSurface, ULONG, *m_pBackBuffer, Release, success, m_pBackBuffer);

	memset(&desc, 0, sizeof(desc));
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DDSD_CAPS | DDSD_ZBUFFERBITDEPTH | DDSD_WIDTH | DDSD_HEIGHT;
	desc.dwZBufferBitDepth = 16;
	desc.dwWidth = 640;
	desc.dwHeight = 480;
	desc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY | DDAgp(DDSCAPS_LOCALVIDMEM) | DDSCAPS_ZBUFFER;
	result = m_pDD->CreateSurface(&desc, &m_pZBuffer, NULL);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
	DECLARE_MEMBER_WATCHER_WINAPI(IDirectDrawSurface, ULONG, *m_pZBuffer, Release, success, m_pZBuffer);

	success = TRUE;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	A more sensible name that actually implies that we wish to repaint the 	   *
 *	window.	InvalidateRect posta a WM_PAINT message so that the whole window   *
 *	gets repainted.															   *
 *******************************************************************************/
void AtiDemoWnd::Redraw(void)
{
	InvalidateRect(m_hWnd, NULL, FALSE); 
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Numeric key handling controlling which objects are to be rendered.		   *	
 *******************************************************************************/
void AtiDemoWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	switch(nChar)
	{
	case '1': NormalMode::m_drawSky = !NormalMode::m_drawSky; break;
	case '2': NormalMode::m_drawLand = !NormalMode::m_drawLand; break;
	case '4': NormalMode::m_drawDragon = !NormalMode::m_drawDragon; break;
	case '6': NormalMode::m_depthCue = !NormalMode::m_depthCue; break;
	case '7': NormalMode::m_lighting = !NormalMode::m_lighting; break;
	case '0': ATI3DCIF::m_reallyDraw = !ATI3DCIF::m_reallyDraw; break;
	case ' ': if((g_mode==1)||(g_mode==2)) g_mode = 3-g_mode; break;
	case 'S': if(g_mode==0) g_mode = 1; else g_mode = 0; break;
	case 'K':
		g_keyboardFlight = !g_keyboardFlight;
		if(g_keyboardFlight)
		{
			g_keyboardFlightPosition = NormalMode::m_cameraPosition;
			g_keyboardFlightRotation = NormalMode::m_cameraRotation;
		}
		break;
	case VK_ESCAPE: SendMessage(m_hWnd, WM_CLOSE, 0, 0); break;

	default:
		if(m_pKeyPressFn) (*m_pKeyPressFn)(nChar, nRepCnt, nFlags);
		return;
	}
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Calls a user-definable paint function (that can be registered through	   *
 *	SetPaintMode), sets a status flag indicating that rendering that frame	   *
 *	is complete, performs the page flip and updates the frame counter.		   *
 *	For debugging purposes, the paint and flip functions can be disabled.	   *
 *******************************************************************************/
void AtiDemoWnd::OnPaint()
{
	ValidateRect(m_hWnd, NULL);

	try
	{
		g_physics.Copy();
		if(!g_blockPaint)
		{
			(*m_pPaintFn)(BackBuffer(), ZBuffer());
			m_pFrontBuffer->Flip(NULL, DDFLIP_WAIT);
		}
#ifdef ALLOW_LOCKSTEP
		s_drawDone = TRUE;
#endif
		g_frameCount++;
	}
	catch(HRESULT err)
	{
		ATI3DCIF::ErrorCleanup();
		ErrorBox("DirectDraw exception: %s\n", DirectDraw::ErrString(err));
		PostQuitMessage(0);
	}
	catch(C3D_EC err)
	{
		ATI3DCIF::ErrorCleanup();
		ErrorBox("ATI3DCIF exception: %s\n", ATI3DCIF::ErrString(err));
		PostQuitMessage(0);
	}
	catch(...)
	{
		ATI3DCIF::ErrorCleanup();
		ErrorBox("Unknown exception\n");
		PostQuitMessage(0);
	}

#ifdef ALLOW_LOCKSTEP
	if(g_lockStep)
		TickHandler(0, 0, 0, 0, 0);
#endif
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Standard window exit 												       *
 *******************************************************************************/
void AtiDemoWnd::OnClose() 
{
	PostMessage(m_hWnd, WM_QUIT, 0, 0);
//	CFrameWnd::OnClose();
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Make sure mouse movement doesnt generate a mouse cursor 				   *
 *******************************************************************************/
void AtiDemoWnd::OnMouseMove(UINT nFlags, POINT point)
{
	SetCursor(NULL);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Fill the specified surface with black.									   *
 *******************************************************************************/
void AtiDemoWnd::PaintBlack(IDirectDrawSurface* pSurface, IDirectDrawSurface*)
{
	DDBLTFX bltfx;
	memset(&bltfx, 0, sizeof(bltfx));
	bltfx.dwSize = sizeof(bltfx);
	bltfx.dwFillColor = 0;
	pSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &bltfx);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set up the paint function to be called whenever a WM_PAINT message is 	   *
 *	processed (OnPaint).													   *	
 *******************************************************************************/
void AtiDemoWnd::SetPaintMode(void (*pPaintFn)(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer))
{
	ASSERT(pPaintFn);
	m_pPaintFn = pPaintFn;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Simple window procedure for handling keyboard and mouse events and 		   *
 *	WM_PAINT and WM_QUIT messages.											   *
 *******************************************************************************/
LRESULT CALLBACK AtiDemoWnd::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if(uMsg == WM_CREATE)
		SetWindowLong(hWnd, 0, (LONG)(((CREATESTRUCT*)lParam)->lpCreateParams));

	AtiDemoWnd* pThis = (AtiDemoWnd*)GetWindowLong(hWnd, 0);

	switch(uMsg)
	{
	case WM_KEYDOWN: pThis->OnKeyDown(wParam, LOWORD(lParam), HIWORD(lParam)); return 0;
	case WM_PAINT: pThis->OnPaint(); return 0;
	case WM_CLOSE: pThis->OnClose(); return 0;
	case WM_MOUSEMOVE:
		{
			POINT pt;
			pt.x=LOWORD(lParam);
			pt.y=HIWORD(lParam);
			pThis->OnMouseMove(wParam, pt);
			return 0;
		}
	case WM_SYSCOMMAND:
		switch (wParam & 0xFFF0)
		{
		case SC_SCREENSAVE:     //disable Screen Saver
		case SC_MONITORPOWER:   //disable Monitor Power Down
			return 0;
		}
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// -----------------------------------------------------------------------------
