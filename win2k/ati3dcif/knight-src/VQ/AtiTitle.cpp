/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	Title screen functionality. Includes initialization, surface creation,     *
 *	image loading, blitting and cleanup.									   *
 *******************************************************************************/
#include "stdwin.h"
#include "Util.h"
#include "Watchers.h"
#include "DirectDraw.h"

#include "AtiDemoWnd.h"
#include "physics.h"
#include "AtiTitle.h"
#include "image.h"

#include "filenames.h"

// -----------------------------------------------------------------------------

#define ATI_SCREEN_FILENAME				BASE_PATH "ATI.pcx"

// -----------------------------------------------------------------------------

static IDirectDrawSurface*	s_pAtiScreen;

// -----------------------------------------------------------------------------

void AtiTitle::Cleanup(void) 
{
	if(s_pAtiScreen)
	{
		s_pAtiScreen->Release();
		s_pAtiScreen = NULL;
	}
}

void AtiTitle::Initialize(void) 
{
	BOOL success = FALSE;

	DDSurfaceDesc desc;
	desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
	desc.dwWidth = 640;
	desc.dwHeight = 480;
	desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
	desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
	desc.ddpfPixelFormat.dwRGBBitCount = 16;
	desc.ddpfPixelFormat.dwRBitMask = 0x00007C00;
	desc.ddpfPixelFormat.dwGBitMask = 0x000003E0;
	desc.ddpfPixelFormat.dwBBitMask = 0x0000001F;
	desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0x00000000;
	HRESULT result = g_pDD->CreateSurface(&desc, &s_pAtiScreen, NULL);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
	DECLARE_MEMBER_WATCHER_WINAPI(IDirectDrawSurface, ULONG, *s_pAtiScreen, Release, success, s_pAtiScreen);

	LockSurface(desc, *s_pAtiScreen);
	ImageRead(desc.lpSurface, 640, 480, PIXELTYPE_1555, desc.lPitch, ATI_SCREEN_FILENAME);
	UnlockSurface(*s_pAtiScreen, desc.lpSurface);

	g_pWindow->SetPaintMode(Paint);
	g_pWindow->FrontBuffer()->Blt(NULL, s_pAtiScreen, NULL, DDBLT_WAIT, NULL);
	g_pWindow->BackBuffer()->Blt(NULL, s_pAtiScreen, NULL, DDBLT_WAIT, NULL);

	success = TRUE;
}

// -----------------------------------------------------------------------------

void AtiTitle::Paint(IDirectDrawSurface* pSurface, IDirectDrawSurface*) 
{
	pSurface->Blt(NULL, s_pAtiScreen, NULL, DDBLT_WAIT, NULL);
}

// -----------------------------------------------------------------------------
