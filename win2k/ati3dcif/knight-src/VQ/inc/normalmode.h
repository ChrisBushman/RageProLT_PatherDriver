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
 *	movements. Castle, landscape, sky, sun and knight are considered		   *
 *	defaults. Drawing code also includes shadowing for all default objects.	   *
 *******************************************************************************/
#ifndef NORMALMODE_H
#define NORMALMODE_H
// -----------------------------------------------------------------------------

#include "font.h"
#include "physics.h"

// -----------------------------------------------------------------------------

class NormalMode {
public:
	static void Cleanup(void) ;
	static void Initialize(void) ;

	static void Paint(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer) ;

	static BOOL		m_drawSky;
	static BOOL		m_drawLand;
	static BOOL		m_drawDragon;
	static BOOL		m_depthCue;
	static BOOL		m_lighting;

	static Vector	m_dragonPosition;
	static Matrix	m_dragonRotation;
	static Vector	m_cameraPosition;
	static Matrix	m_cameraRotation;

	static void SetMode(void);

	static Ati3dContext* Context(void) { return m_pContext; }

protected:
private:
	static Ati3dContext* m_pContext;

	static void DrawScene(void) ;

	static void DrawLandscape(Clipper& rClipper) ;

	static Poly* m_pSun;
	static void InitSun(void);
	static void DrawSun(Clipper& rClipper) ;
};

// -----------------------------------------------------------------------------

extern Vector g_sunPosition;

// -----------------------------------------------------------------------------
#endif
