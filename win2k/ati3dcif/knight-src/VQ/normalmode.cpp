/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	The base sequence and set up for the Knight Demo world. All other modes    *
 *	(movement and death sequences) will require this. Includes initialization, *
 *	drawing and clean-up code for the default demo features and objects and    *		 
 *	movements. Castle, landscape, sky, sun, water and knight are considered    *
 *	defaults. Drawing code also includes shadowing for all default objects.	   *
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
#include "a3d.h"
#include "ajm.h"
#include "sky.h"
#include "shadow.h"
#include "dragon.h"

#include "filenames.h"

// -----------------------------------------------------------------------------

#define GROUND_FILENAME						BASE_PATH "ground.a3d"
#define LANDSCAPE_PCX_TEXTURE_FILENAME		BASE_PATH "ground1.pcx"
#define LANDSCAPE_VQT_TEXTURE_FILENAME		BASE_PATH "ground1.vqt"
#define SIMPLE_FONT_FILENAME				BASE_PATH "font.pcx"

// -----------------------------------------------------------------------------

extern BOOL g_screenShot;
extern int g_mode;

#define BG_RED		64
#define BG_GREEN	64
#define BG_BLUE		64

// -----------------------------------------------------------------------------

Ati3dContext*	NormalMode::m_pContext = NULL;
Vector			NormalMode::m_dragonPosition(ZERO);
Matrix			NormalMode::m_dragonRotation(IDENTITY);
Vector			NormalMode::m_cameraPosition(ZERO);
Matrix			NormalMode::m_cameraRotation(IDENTITY);
BOOL			NormalMode::m_drawDragon = TRUE;
BOOL			NormalMode::m_drawLand = TRUE;
BOOL			NormalMode::m_drawSky = TRUE;
BOOL			NormalMode::m_depthCue = TRUE;
BOOL			NormalMode::m_lighting = TRUE;
Poly*			NormalMode::m_pSun = NULL;

Object*				g_pLandscape;
Ati3dTexture*		g_pLandscapeTexture[2];
Vector				g_sunPosition = Vector(-4000, -10000, 4000);
FontSurface*		g_pSimpleFont;

extern BOOL g_blockPhysics;

// -----------------------------------------------------------------------------

void NormalMode::Cleanup(void) 
{
	delete m_pSun;
	m_pSun = NULL;

	delete g_pLandscapeTexture[1];
	g_pLandscapeTexture[1] = NULL;

	delete g_pLandscapeTexture[0];
	g_pLandscapeTexture[0] = NULL;

	delete g_pLandscape;
	g_pLandscape = NULL;

	Sky::Cleanup();

	delete m_pContext;
	m_pContext = NULL;

	delete g_pSimpleFont;
	g_pSimpleFont = NULL;
}

// -----------------------------------------------------------------------------

void NormalMode::Initialize(void) 
{
	BOOL success = FALSE;

	g_pSimpleFont = FontSurface::LoadFont(*g_pDD, SIMPLE_FONT_FILENAME, 20, 32, 12, 32, 127, 127);
	DECLARE_POINTER_WATCHER(FontSurface*, g_pSimpleFont, success, g_pSimpleFont);

	m_pContext = new Ati3dContext;
	DECLARE_POINTER_WATCHER(Ati3dContext*, m_pContext, success, m_pContext);

	Sky::Initialize();
	DECLARE_FUNCTION_WATCHER_NODATA(void, Sky::Cleanup, success, sky_cleanup);

	A3DRead(&g_pLandscape, GROUND_FILENAME);
	DECLARE_POINTER_WATCHER(Object*, g_pLandscape, success, g_pLandscape);
	g_pLandscape->Latch();

	g_pLandscapeTexture[0] = ATI3DCIF::LoadTexture(*g_pDD, LANDSCAPE_PCX_TEXTURE_FILENAME, C3D_ETF_RGB1555);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, g_pLandscapeTexture[0], success, land1);

	g_pLandscapeTexture[1] = ATI3DCIF::LoadTexture(*g_pDD, LANDSCAPE_VQT_TEXTURE_FILENAME, C3D_ETF_VQ);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, g_pLandscapeTexture[1], success, land2);

	InitSun();

	success = TRUE;
}

// -----------------------------------------------------------------------------

void NormalMode::SetMode(void) 
{
	TRACE("NormalMode::SetMode()\n");

	EnterCriticalSection(&g_physicsMutex);

	g_pWindow->SetPaintMode(Paint);
	g_pWindow->SetKeyPressHandler(NULL);

	LeaveCriticalSection(&g_physicsMutex);
}

// -----------------------------------------------------------------------------

void NormalMode::DrawScene(void)
{
	g_gfxStack.ObjectLight() = g_sunPosition;

	m_pContext->SetZMode(C3D_EZMODE_OFF, C3D_EZCMP_LEQUAL);
	g_gfxStack.DepthCueOn(FALSE);
	g_gfxStack.LightingOn(FALSE);

	if(m_drawSky)
		Sky::Draw(g_clipper);
	
	DrawSun(g_clipper);

	g_gfxStack.DepthCueOn(m_depthCue);
	g_gfxStack.SetDepthCue(200*12*3);
	g_gfxStack.LightingOn(m_lighting);
	g_gfxStack.SetLighting(0.5, 0.5);

	m_pContext->SetZMode(C3D_EZMODE_TESTON_WRITEZ, C3D_EZCMP_LEQUAL);

	if(m_drawLand)
		DrawLandscape(g_clipper);

	g_gfxStack.TransformObject(m_dragonRotation, m_dragonPosition);

	if(m_drawDragon)
		Dragon::Draw(g_clipper);
	
	g_gfxStack.TransformDone();

	g_gfxStack.DepthCueOn(FALSE);

	if(m_lighting)
	{
		m_pContext->SetSolidColor(0, 0, 0, 128);
		m_pContext->SetAlphaDst(C3D_EADST_INVSRCALPHA);
		m_pContext->SetAlphaSrc(C3D_EASRC_SRCALPHA);
		m_pContext->SetZMode(C3D_EZMODE_TESTON_WRITEZ, C3D_EZCMP_LESS);
		g_gfxStack.ZBias() = -3;
		g_gfxStack.LightingOn(FALSE);

		Dragon::DrawShadow(g_clipper);

		g_gfxStack.ZBias() = 0;
		m_pContext->SetAlphaDst(C3D_EADST_ZERO);
		m_pContext->SetAlphaSrc(C3D_EASRC_ONE);
	}
}



void NormalMode::Paint(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer) 
{
	g_clipper.Stack() = &g_gfxStack;
	g_clipper.Context() = m_pContext;

	DDSurfaceDesc surfaceDesc;
	LockSurface(surfaceDesc, *pSurface);
	UnlockSurface(*pSurface, surfaceDesc.lpSurface);

	DDBLTFX bltfx;
	memset(&bltfx, 0, sizeof(bltfx));
	bltfx.dwSize = sizeof(bltfx);
	bltfx.dwFillColor = RGBColor(surfaceDesc.ddpfPixelFormat, BG_RED, BG_GREEN, BG_BLUE, 255);
	pSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &bltfx);

	memset(&bltfx, 0, sizeof(bltfx));
	bltfx.dwSize = sizeof(bltfx);
	bltfx.dwFillDepth = 0xFFFF;
	pZBuffer->Blt(NULL, NULL, NULL, DDBLT_DEPTHFILL | DDBLT_WAIT, &bltfx);

	m_pContext->SetSurface(*pSurface);
	m_pContext->SetZBuffer(*pZBuffer);

	m_pContext->PerspectiveOn(TRUE);
	m_pContext->SetFilter(C3D_ETFILT_MIN2BY2_MAG2BY2);

	g_trianglesDrawn = 0;

	m_pContext->RenderBegin();

	switch(g_mode)
	{
	case 0:
		g_mode = 1;
		g_gfxStack.SetWindow(0, 120, 320, 240, 70.0 * M_PI / 180.0);
		g_gfxStack.TransformView(m_cameraRotation, m_cameraPosition);
		DrawScene();
		g_gfxStack.TransformDone();

		g_mode = 2;
		g_gfxStack.SetWindow(320, 120, 320, 240, 70.0 * M_PI / 180.0);
		g_gfxStack.TransformView(m_cameraRotation, m_cameraPosition);
		DrawScene();
		g_gfxStack.TransformDone();

		g_mode = 0;
		break;

	case 1:
	case 2:
		g_gfxStack.SetWindow(0, 0, 640, 480, 70.0 * M_PI / 180.0);
		g_gfxStack.TransformView(m_cameraRotation, m_cameraPosition);
		DrawScene();
		g_gfxStack.TransformDone();
		break;
	}

	m_pContext->RenderEnd();

	switch(g_mode)
	{
	case 0:
		g_pSimpleFont->BltString(pSurface, 160-(7*g_pSimpleFont->CharWidth())/2, 360, "RGB1555");
		g_pSimpleFont->BltString(pSurface, 480-(2*g_pSimpleFont->CharWidth())/2, 360, "VQ");
		break;

	case 1:
		g_pSimpleFont->BltString(pSurface, 320-(7*g_pSimpleFont->CharWidth())/2, 480-g_pSimpleFont->CharHeight(), "RGB1555");
		break;

	case 2:
		g_pSimpleFont->BltString(pSurface, 320-(2*g_pSimpleFont->CharWidth())/2, 480-g_pSimpleFont->CharHeight(), "VQ");
		break;
	}

	m_pContext->SetTexture(NULL);
}

// -----------------------------------------------------------------------------

void NormalMode::InitSun(void)
{
	BOOL success = FALSE;

	m_pSun = new Poly(POLYGON_LIST, 8, 12);
	DECLARE_POINTER_WATCHER(Poly*, m_pSun, success, m_pSun);
	
	m_pSun->GetVertex(0) = Vertex(Vector(-120, -120, -120));
	m_pSun->GetVertex(1) = Vertex(Vector(-120, -120,  120));
	m_pSun->GetVertex(2) = Vertex(Vector( 120, -120,  120));
	m_pSun->GetVertex(3) = Vertex(Vector( 120, -120, -120));
	m_pSun->GetVertex(4) = Vertex(Vector(-120,  120, -120));
	m_pSun->GetVertex(5) = Vertex(Vector(-120,  120,  120));
	m_pSun->GetVertex(6) = Vertex(Vector( 120,  120,  120));
	m_pSun->GetVertex(7) = Vertex(Vector( 120,  120, -120));

	m_pSun->GetFace(0).vertices[0] = 0;
	m_pSun->GetFace(0).vertices[1] = 1;
	m_pSun->GetFace(0).vertices[2] = 2;
	m_pSun->GetFace(1).vertices[0] = 0;
	m_pSun->GetFace(1).vertices[1] = 2;
	m_pSun->GetFace(1).vertices[2] = 3;

	m_pSun->GetFace(2).vertices[0] = 6;
	m_pSun->GetFace(2).vertices[1] = 5;
	m_pSun->GetFace(2).vertices[2] = 4;
	m_pSun->GetFace(3).vertices[0] = 7;
	m_pSun->GetFace(3).vertices[1] = 6;
	m_pSun->GetFace(3).vertices[2] = 4;

	m_pSun->GetFace(4).vertices[0] = 0;
	m_pSun->GetFace(4).vertices[1] = 3;
	m_pSun->GetFace(4).vertices[2] = 7;
	m_pSun->GetFace(5).vertices[0] = 0;
	m_pSun->GetFace(5).vertices[1] = 7;
	m_pSun->GetFace(5).vertices[2] = 4;

	m_pSun->GetFace(6).vertices[0] = 6;
	m_pSun->GetFace(6).vertices[1] = 2;
	m_pSun->GetFace(6).vertices[2] = 1;
	m_pSun->GetFace(7).vertices[0] = 5;
	m_pSun->GetFace(7).vertices[1] = 6;
	m_pSun->GetFace(7).vertices[2] = 1;

	m_pSun->GetFace(8).vertices[0] = 3;
	m_pSun->GetFace(8).vertices[1] = 2;
	m_pSun->GetFace(8).vertices[2] = 6;
	m_pSun->GetFace(9).vertices[0] = 3;
	m_pSun->GetFace(9).vertices[1] = 6;
	m_pSun->GetFace(9).vertices[2] = 7;

	m_pSun->GetFace(10).vertices[0] = 5;
	m_pSun->GetFace(10).vertices[1] = 1;
	m_pSun->GetFace(10).vertices[2] = 0;
	m_pSun->GetFace(11).vertices[0] = 4;
	m_pSun->GetFace(11).vertices[1] = 5;
	m_pSun->GetFace(11).vertices[2] = 0;

	for(int i=0; i<12; i++)
	{
		PolyFace* pFace = &m_pSun->GetFace(i);
		pFace->normal = Normal(m_pSun->GetVertex(pFace->vertices[0]).XYZ(),
							   m_pSun->GetVertex(pFace->vertices[1]).XYZ(),
							   m_pSun->GetVertex(pFace->vertices[2]).XYZ());
	}

	success = TRUE;
}

void NormalMode::DrawSun(Clipper& rClipper)
{
	float savedFarClipPlane = rClipper.Stack()->FarClipPlane();
	rClipper.Stack()->SetFarClipPlane(100000);

	g_gfxStack.TransformObject(Matrix(IDENTITY), g_sunPosition);

	m_pContext->SetSolidColor(255, 255, 0);
	rClipper.DrawPolygon(*m_pSun, VERTEXTYPE_1);
	
	g_gfxStack.TransformDone();

	rClipper.Stack()->SetFarClipPlane(savedFarClipPlane);
}

// -----------------------------------------------------------------------------

void NormalMode::DrawLandscape(Clipper& rClipper)
{
	switch(g_mode)
	{
	case 1: rClipper.Context()->SetTexture(g_pLandscapeTexture[0]); break;
	case 2: rClipper.Context()->SetTexture(g_pLandscapeTexture[1]); break;
	}

	g_pLandscape->Draw(rClipper, VERTEXTYPE_TC);
}

// -----------------------------------------------------------------------------
