/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *******************************************************************************/
#ifndef ATI_DEMO_H
#define ATI_DEMO_H
// -----------------------------------------------------------------------------

#include "mmsystem.h"
#include "DirectDraw.h"

#include "XForm.h"
#include "Clipper.h"

#include "AtiDemoWnd.h"

#include "font.h"

// -----------------------------------------------------------------------------

extern HINSTANCE			g_hInstance;
extern CRITICAL_SECTION		g_physicsMutex;
extern IDirectDraw*			g_pDD;
extern XFormStack			g_gfxStack;
extern AtiDemoWnd*			g_pWindow;
extern Clipper				g_clipper;

extern void CALLBACK TickHandler(UINT uTimerID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2);

#ifdef ALLOW_LOCKSTEP
extern BOOL					g_lockStep;
#endif

// -----------------------------------------------------------------------------
#endif // ATI_DEMO_H
