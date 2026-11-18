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
#ifndef ATI_DEMOWND_H
#define ATI_DEMOWND_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"
#include "Ati3dCIFx.h"
#include "Vertex.h"
#include "Polygon.h"

// -----------------------------------------------------------------------------

class AtiDemoWnd {
public:
	virtual ~AtiDemoWnd(void);
	AtiDemoWnd(IDirectDraw& dd);

	void Redraw(void);

	void SetPaintMode(void (*pPaintFn)(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer));
	void SetKeyPressHandler(void (*pKeyFn)(UINT nChar, UINT nRepCnt, UINT nFlags)) { m_pKeyPressFn = pKeyFn; }

	static void PaintBlack(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer);

	HWND	m_hWnd;

	IDirectDrawSurface* FrontBuffer(void) const { return m_pFrontBuffer; }
	IDirectDrawSurface* BackBuffer(void) const { return m_pBackBuffer; }
	IDirectDrawSurface*	ZBuffer(void) const { return m_pZBuffer; }

protected:
	virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	virtual void OnPaint();
	virtual void OnClose();
	virtual void OnMouseMove(UINT nFlags, POINT point);

private:
	IDirectDraw*		m_pDD;
	IDirectDrawSurface*	m_pFrontBuffer;
	IDirectDrawSurface*	m_pBackBuffer;
	IDirectDrawSurface*	m_pZBuffer;

	void	(*m_pPaintFn)(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer);
	void	(*m_pKeyPressFn)(UINT nChar, UINT nRepCnt, UINT nFlags);

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// -----------------------------------------------------------------------------

extern int		g_frameCount;
extern int		g_framesPerSecond;

// -----------------------------------------------------------------------------
#endif // ATI_DEMOWND_H

