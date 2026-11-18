/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *******************************************************************************/
#include "stdwin.h"
#include "Util.h"
#include "Watchers.h"
#include "DirectDraw.h"

#include "AtiDemo.h"
#include "AtiDemoWnd.h"
#include "physics.h"
#include "a3d.h"
#include "ajm.h"

#include "normalmode.h"
#include "dragon.h"
#include "multimedia.h"
#include "shadow.h"

#include "filenames.h"

// -----------------------------------------------------------------------------

extern BOOL g_blockPaint;

// -----------------------------------------------------------------------------

#define DRAGON_MOTION_FILENAME			BASE_PATH "cycle.ajm"
#define DRAGON_TGA_TEXTURE_FILENAME		BASE_PATH "dragon.tga"
#define DRAGON_VQT_TEXTURE_FILENAME		BASE_PATH "dragon.vqt"
#define DRAGON_OBJECT_FILENAME			BASE_PATH "dragon.a3d"

// -----------------------------------------------------------------------------

static uint32			s_currentTick;
static void				(*s_pOldKnightDrawFn)(Clipper& rClipper);
static JointMotion*		s_pDragonMotion;
static Ati3dTexture*	s_pDragonTexture[2];
static Object*			s_pDragon;
static Matrix			s_rotation = Matrix(ROTATION, M_PI/2, 0, 0);
static Vector			s_position = Vector(0, -38, 0);

static float			s_cameraAngle = 0;
static float			s_cameraHeight = 0;
static float			s_cameraDistance = 11*12;

static BOOL				s_doMotion = TRUE;
static BOOL				s_doFade = TRUE;

extern int g_mode;

// -----------------------------------------------------------------------------

void Dragon::Cleanup(void) 
{
	delete s_pDragonMotion;
	s_pDragonMotion = NULL;

	delete s_pDragonTexture[1];
	s_pDragonTexture[1] = NULL;

	delete s_pDragonTexture[0];
	s_pDragonTexture[0] = NULL;

	delete s_pDragon;
	s_pDragon = NULL;
}

void Dragon::Initialize(void) 
{
	BOOL success = FALSE;

	A3DRead(&s_pDragon, DRAGON_OBJECT_FILENAME);
	DECLARE_POINTER_WATCHER(Object*, s_pDragon, success, dragon);

	s_pDragonTexture[0] = ATI3DCIF::LoadTexture(*g_pDD, DRAGON_TGA_TEXTURE_FILENAME, C3D_ETF_RGB1555);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, s_pDragonTexture[0], success, dragon0);

	s_pDragonTexture[1] = ATI3DCIF::LoadTexture(*g_pDD, DRAGON_VQT_TEXTURE_FILENAME, C3D_ETF_VQ);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, s_pDragonTexture[1], success, dragon1);

	s_pDragonMotion = JointMotion::Read(DRAGON_MOTION_FILENAME, s_pDragon);
	DECLARE_POINTER_WATCHER(JointMotion*, s_pDragonMotion, success, s_pDragonMotion);

	success = TRUE;
}

// -----------------------------------------------------------------------------

void Dragon::SetMode(void)
{
	TRACE("Dragon::SetMode()\n");

	EnterCriticalSection(&g_physicsMutex);

	NormalMode::SetMode();

	g_physics.SetPhysicsMode(Physics);

	NormalMode::m_dragonPosition = Vector(0, 0, 0);
	NormalMode::m_dragonRotation = Matrix(IDENTITY);

	s_currentTick = 0;

	LeaveCriticalSection(&g_physicsMutex);
}

// -----------------------------------------------------------------------------

void Dragon::Draw(Clipper& rClipper)
{
	g_clipper.Context()->SetZMode(C3D_EZMODE_TESTON_WRITEZ, C3D_EZCMP_LEQUAL);

	switch(g_mode)
	{
	case 1: g_clipper.Context()->SetTexture(s_pDragonTexture[0]); break;
	case 2: g_clipper.Context()->SetTexture(s_pDragonTexture[1]); break;
	}

	g_gfxStack.TransformObject(s_rotation, s_position);

	s_pDragon->Draw(g_clipper, VERTEXTYPE_TC);

	g_gfxStack.TransformDone();
}



void Dragon::DrawShadow(Clipper& rClipper)
{
	Normal nSurface = Normal(0, -1, 0, 0);
	Normal nObject = UntransformBy(nSurface, s_rotation, s_position);

	g_gfxStack.TransformObject(s_rotation, s_position);

	Shadow::Drop(*s_pDragon, nObject);

	g_gfxStack.TransformDone();
}

// -----------------------------------------------------------------------------

void Dragon::Physics(PhysicsType type) throw()
{
	if(type == PHYSICS_COPY)
	{
		s_pDragonMotion->SetTick(s_currentTick);
		s_pDragon->Latch();

		NormalMode::m_cameraRotation = Matrix(ROTATION, 0, -s_cameraAngle, 0) * Matrix(ROTATION, -s_cameraHeight, 0, 0);
		NormalMode::m_cameraPosition = NormalMode::m_cameraRotation * Vector(0, 0, -s_cameraDistance) + Vector(0, -72, 0);

		if(NormalMode::m_cameraPosition.Y() > -1) NormalMode::m_cameraPosition.Y() = -1;
	}
	else
	{
		s_currentTick++;
		if(s_currentTick >= s_pDragonMotion->TickCount())
			s_currentTick = 0;

		s_cameraAngle += Multimedia::JoyTranslation(0).X() / 10.0;
		s_cameraHeight -= Multimedia::JoyTranslation(0).Y() / 10.0;

		if(Multimedia::JoyButtons(0) & 1) s_cameraDistance -= 2;
		if(Multimedia::JoyButtons(0) & 2) s_cameraDistance += 2;

		if(s_cameraDistance < 2*12) s_cameraDistance = 2*12;
		if(s_cameraHeight > 1.57) s_cameraHeight = 1.57;
		if(s_cameraHeight < -1) s_cameraHeight = -1;
	}
}

// -----------------------------------------------------------------------------
