/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	Physics timing control. 												   *
 *******************************************************************************/
#include "stdwin.h"
#include "physics.h"
#include <math.h>
#include <float.h>
#include "multimedia.h"
#include "atidemo.h"
#include "normalmode.h"

// -----------------------------------------------------------------------------

#ifdef KEYBOARD_FLIGHT
BOOL g_keyboardFlight = FALSE;
#endif

// -----------------------------------------------------------------------------

Physics g_physics;
extern BOOL g_blockPhysics;
extern BOOL g_stepPhysics;

// -----------------------------------------------------------------------------

#ifdef KEYBOARD_FLIGHT

Vector	g_keyboardFlightPosition(0, -72, -72);
Matrix	g_keyboardFlightRotation(ROTATION, 0, 0, 0);
static float	s_scale = 1.0;

BOOL g_lookatMode = FALSE;

#endif

// -----------------------------------------------------------------------------

Physics::Physics(void)
{
	tickNumber = 0;

	m_pMode = PhysicsModeNoop;

	ready = FALSE;
}

// -----------------------------------------------------------------------------

void Physics::Copy(void)
{
	EnterCriticalSection(&g_physicsMutex);

//	if(!g_blockPhysics)
		(*m_pMode)(PHYSICS_COPY);

#ifdef KEYBOARD_FLIGHT
	if(g_keyboardFlight)
	{
		NormalMode::m_cameraPosition = g_keyboardFlightPosition;
		NormalMode::m_cameraRotation = g_keyboardFlightRotation;
	}
#endif

	LeaveCriticalSection(&g_physicsMutex);
}

// -----------------------------------------------------------------------------

void Physics::SetPhysicsMode(void (*pMode)(PhysicsType type))
{
	ASSERT(pMode);

	EnterCriticalSection(&g_physicsMutex);

	m_pMode = pMode;

	LeaveCriticalSection(&g_physicsMutex);
}

// -----------------------------------------------------------------------------

void Physics::Tick(void)
{
	EnterCriticalSection(&g_physicsMutex);

	if(g_stepPhysics && g_blockPhysics)
		g_blockPhysics = FALSE;
	else
		g_stepPhysics = FALSE;

	if(!g_blockPhysics)
		(*m_pMode)(PHYSICS_RUN);

#ifdef KEYBOARD_FLIGHT
	if(g_keyboardFlight)
	{
		static FlightData		s_flightData;
		static float			s_scale = 1.0;
		static DWORD oldButtons;
		DWORD buttons = Multimedia::JoyButtons(0);
		DWORD pressed = (oldButtons ^ buttons) & buttons;
		oldButtons = buttons;
		if(pressed & (1 << 2)) s_scale *= 2;
		if(pressed & (1 << 3)) s_scale /= 2;
		Multimedia::FlightControl(s_flightData, &g_keyboardFlightRotation, &g_keyboardFlightPosition, s_scale);
		if(GetAsyncKeyState(VK_NUMPAD5) & 0x8000) g_keyboardFlightRotation = Matrix(IDENTITY);
	}
#endif

	LeaveCriticalSection(&g_physicsMutex);

	tickNumber++;
	if((tickNumber % 30) == 0)
	{
		g_framesPerSecond = g_frameCount;
		g_frameCount = 0;
	}

	if(g_stepPhysics)
	{
		g_blockPhysics = TRUE;
		g_stepPhysics = FALSE;
	}

	g_pWindow->Redraw();
}

// -----------------------------------------------------------------------------

void Physics::PhysicsModeNoop(PhysicsType type)
{
	(void)type;
}

// -----------------------------------------------------------------------------
