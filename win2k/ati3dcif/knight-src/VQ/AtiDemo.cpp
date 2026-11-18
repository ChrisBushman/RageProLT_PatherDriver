/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include "mmsystem.h"

#include "DirectDraw.h"

#include "Ati3dCIFx.h"

#include "AtiDemoWnd.h"
#include "AtiDemo.h"
#include "Watchers.h"
#include "A3d.h"
#include "Multimedia.h"
#include "font.h"

#include "Vertex.h"
#include "Clipper.h"

#include "normalmode.h"
#include "atititle.h"
#include "dragon.h"

#include "filenames.h"

// -----------------------------------------------------------------------------

#define SIMPLE_FONT_FILENAME			BASE_PATH "font.pcx"

// -----------------------------------------------------------------------------

HINSTANCE			g_hInstance;
CRITICAL_SECTION	g_physicsMutex;
CRITICAL_SECTION*	g_pPhysicsMutex;
IDirectDraw*		g_pDD;
XFormStack			g_gfxStack;
AtiDemoWnd*			g_pWindow;
Clipper				g_clipper;

extern BOOL g_doMipMap;
extern int  g_agpVidMemState;

#ifdef ALLOW_LOCKSTEP
BOOL g_lockStep = FALSE;
#endif

// -----------------------------------------------------------------------------

MMRESULT			s_timerID;

// -----------------------------------------------------------------------------

#ifdef _DEBUG

static int DebugHookFunction(int mode, char *msg, int *retVal)
{
	if((mode == _CRT_ASSERT) || (mode == _CRT_ERROR))
	{
		ATI3DCIF_Term();
#ifdef _WIN32
	    OutputDebugString(msg);
#else  /* _WIN32 */
		_CrtOutputDebugString(msg);
#endif  /* _WIN32 */
		(*retVal) = 1;
		return TRUE;
	}
	else
		return FALSE;
}

#endif

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	WinMain controls the three sequences that we continually repeat. The 	   *
 *	sequences are: (a)knight statue on pedestal morphs to living knight, jumps *
 *	off pedestal, and runs under the castle archway, (b)knight dies a horrible *
 *	death as the earth opens up and swallows him like a giant sphincter, and   *
 *	(c)knight threatens dragon and dragon flies off.						   *
 *******************************************************************************/
int WINAPI WinMain(HINSTANCE hInstance,	HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	WPARAM wParam = 0;

	//Disable MessageBox() on errors.
	_CrtSetReportHook(DebugHookFunction);
	_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);

	//Support for MIPmapping.
	char buf[256];
	GetProfileString("AtiDemo", "MipMap", "FALSE", buf, 256);
	if(!stricmp(buf, "TRUE")) g_doMipMap = TRUE;
	if(!stricmp(buf, "FALSE")) g_doMipMap = FALSE;

	//AGP support.
	GetProfileString("AtiDemo", "AGP", "DRIVER", buf, 256);
	if(!stricmp(buf, "DRIVER")) g_agpVidMemState = 0;
	if(!stricmp(buf, "LOCAL")) g_agpVidMemState = 1;
	if(!stricmp(buf, "NONLOCAL")) g_agpVidMemState = 2;

#ifdef ALLOW_LOCKSTEP
	//LOCKSTEP mode
	GetProfileString("AtiDemo", "LockStep", "OFF", buf, 256);
	if(!stricmp(buf, "OFF")) g_lockStep = FALSE;
	if(!stricmp(buf, "ON")) g_lockStep = TRUE;
#endif

	try
	{
		g_hInstance = hInstance;

		g_pPhysicsMutex = &g_physicsMutex;
		InitializeCriticalSection(g_pPhysicsMutex);
		DECLARE_FUNCTION_DOER_WINAPI(CRITICAL_SECTION*, void, g_pPhysicsMutex, DeleteCriticalSection, g_physicsMutex);

		//DirectDraw initialization and target system capabilities check.
		HRESULT result = DirectDrawCreate(NULL, &g_pDD, NULL);
		if(result != DD_OK) THROW_DD_EXCEPTION(result);		
		DECLARE_MEMBER_DOER_WINAPI(IDirectDraw, ULONG, *g_pDD, Release, g_pDD);

#if 0
		DDCAPS halCaps, helCaps;
		halCaps.dwSize = sizeof(halCaps);
		helCaps.dwSize = sizeof(helCaps);
		result = g_pDD->GetCaps(&halCaps, &helCaps);
		if(result != DD_OK) THROW_DD_EXCEPTION(result);
		
		TRACE("Before load:\n");
		TRACE("HAL Free memory: %d out of %d\n", halCaps.dwVidMemFree, halCaps.dwVidMemTotal);
		TRACE("HEL Free memory: %d out of %d\n", helCaps.dwVidMemFree, helCaps.dwVidMemTotal);
#endif

		//Tell Windows how much control we want of Windows resources; this will be reset
		//later to full-screen, exclusive so that we get complete control of the drawing
		//surfaces.
		result = g_pDD->SetCooperativeLevel(NULL, DDSCL_NORMAL);
		if(result != DD_OK) THROW_DD_EXCEPTION(result);

		//Initialize and load the ATI3DCIF module. Verify that we are running on a RagePRO.
		ATI3DCIF::Begin();
		DECLARE_FUNCTION_DOER_NODATA(void, ATI3DCIF::End, end);

		// Check that we're on the right chip or better... (Should really be checking the caps)
		C3D_3DCIFINFO info;
		info.u32Size = sizeof(info);
		ATI3DCIF::GetInfo(&info);
		if(info.u32ASICID < 0x401)
		{
			MessageBox(NULL, "ERROR: Must be run on a RagePRO", "AtiDemo", MB_OK | MB_ICONERROR | MB_APPLMODAL);
			THROW_EXCEPTION();
		}

		//Initialize i/o.
		Multimedia::Begin();
		DECLARE_FUNCTION_DOER_NODATA(void, Multimedia::End, mm_end);

		//AtiDemoWnd is the class upon which the Knight Demo is built. It handles
		//window creation, surface allocation, image and object loading, etc.
		if((g_pWindow = new AtiDemoWnd(*g_pDD)) == NULL) THROW_EXCEPTION();
		DECLARE_POINTER_DOER(AtiDemoWnd*, g_pWindow, g_pWindow);

		//Load our main title splash screen and render it.
		AtiTitle::Initialize();
		DECLARE_FUNCTION_DOER_NODATA(void, AtiTitle::Cleanup, atiTitle_cleanup);

		ShowWindow(g_pWindow->m_hWnd, SW_SHOW);
		SetForegroundWindow(g_pWindow->m_hWnd);
		g_pWindow->Redraw();

		//Normal mode handles default object creation and initial placement. Default 
		//objects include the castle, landscape, sky, sun, water and knight.
		NormalMode::Initialize();
		DECLARE_FUNCTION_DOER_NODATA(void, NormalMode::Cleanup, normalMode_cleanup);

		Dragon::Initialize();
		DECLARE_FUNCTION_DOER_NODATA(void, Dragon::Cleanup, knight_cleanup);

#ifdef ALLOW_LOCKSTEP
		if(!g_lockStep)
#endif
		{
			s_timerID = timeSetEvent(33, 0, TickHandler, 0, TIME_PERIODIC);
			if(s_timerID == 0) THROW_EXCEPTION();
		}
		DECLARE_FUNCTION_DOER_WINAPI(MMRESULT, MMRESULT, s_timerID, timeKillEvent, s_timerID);

		//Setup for the three sequences that we will be demonstrating: 
		//		initial stone to living knight texture composite,
		//		the sphincter death sequence, and 
		//		the dragon fly-by
		g_gfxStack.SetWindow(0, 0, 640, 480, 70.0 * M_PI / 180.0);
		g_gfxStack.SetFarClipPlane(200 * 3 * 12); // 200 yards

		SetCursor(NULL);

//		Morph::Initialize();
//		DECLARE_FUNCTION_DOER_NODATA(void, Morph::Cleanup, pedestal_cleanup);

//		Sphincter::Initialize();
//		DECLARE_FUNCTION_DOER_NODATA(void, Sphincter::Cleanup, sphincter_cleanup);

//		Dragon::Initialize();
//		DECLARE_FUNCTION_DOER_NODATA(void, Dragon::Cleanup, dragon_cleanup);

//		Girl::Initialize();
//		DECLARE_FUNCTION_DOER_NODATA(void, Girl::Cleanup, girl_cleanup);

#if 0
#ifdef _DEBUG
		halCaps.dwSize = sizeof(halCaps);
		helCaps.dwSize = sizeof(helCaps);
		result = g_pDD->GetCaps(&halCaps, &helCaps);
		if(result != DD_OK) THROW_DD_EXCEPTION(result);
#endif
		
		TRACE("After load:\n");
		TRACE("HAL Free memory: %d out of %d\n", halCaps.dwVidMemFree, halCaps.dwVidMemTotal);
		TRACE("HEL Free memory: %d out of %d\n", helCaps.dwVidMemFree, helCaps.dwVidMemTotal);
#endif

		//Show the title shot for a set amount of time
//		AtiTitle::ReadyToContinue();
		Dragon::SetMode();

		//Standard Windows main event loop
		while(1)
		{
			MSG msg;
			int result = GetMessage(&msg, NULL, 0, 0);
			if(result != TRUE)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		EnterCriticalSection(&g_physicsMutex);
	}
	catch(...)
	{
	}

	Sleep(500);

	return wParam;
}

// -----------------------------------------------------------------------------
