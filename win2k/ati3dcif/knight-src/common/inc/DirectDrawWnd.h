/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	C++ wrappers for the standard DirectDraw calls							   *
 *******************************************************************************/
#ifndef ATI_DIRECTDRAWWND_H
#define ATI_DIRECTDRAWWND_H
// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#include "DirectDraw.h"

// -----------------------------------------------------------------------------

class DirectDrawWnd
{
public:
	virtual ~DirectDrawWnd();
	DirectDrawWnd();

	virtual void Destroy(void);
	void Create(DirectDraw &dd, LPCTSTR lpszWindowName, int x, int y, int nWidth, int nHeight, BOOL zBuffer) ;

	DirectDraw* GetDDDevice(void) { ASSERT(m_pDD); return m_pDD; }

	DDSurface* FrontBuffer(void) { return m_pFront; }
	DDSurface* BackBuffer(void) { return m_pBack; }
	DDSurface* ZBuffer(void) { return m_pZBuffer; }

	void Flip(void) ;

	int Width(void) const { return m_width; }
	int Height(void) const { return m_height; }

	virtual void Fullscreen(BOOL fFullscreen) ;
	BOOL IsFullscreen(void) const { return m_fullscreen; }

protected:
private:
	DirectDraw			*m_pDD;

	BOOL				 m_fullscreen;

	DDPrimarySurface	*m_pFront;
	DDSurface			*m_pBack;		// This can be either an offscreen or attached surface
	DDZSurface			*m_pZBuffer;

	DDClipper			*m_pClipper;

	BOOL				 m_zBuffer;
	int					 m_width;
	int					 m_height;

	void CreateWindowedSurfaces(void) ;
	void CreateFullscreenSurfaces(void) ;
	void DestroySurfaces(void);

	virtual BOOL DestroyWindow();

	void SetCooperativeMode(void) ;
};

// -----------------------------------------------------------------------------
#endif // ATI_DIRECTDRAW_WND_H
