/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Simple text string rendering utilities. Functionality to load a font,      *
 *	create a surface to hold a character, blit a character, and blit a string. *
 *******************************************************************************/
#include "stdwin.h"
#include "font.h"
#include "directdraw.h"
#include "watchers.h"

// -----------------------------------------------------------------------------

FontSurface::~FontSurface(void)
{
	m_pSurface->Release();
}

FontSurface::FontSurface(IDirectDrawSurface& rSurface, int charWidth, int charHeight, int charsPerRow, char firstChar, char lastChar, char errChar) /*  */
{
	BOOL success = FALSE;

	DDSurfaceDesc desc;
	LockSurface(desc, rSurface);
	UnlockSurface(rSurface, desc.lpSurface);

	m_pSurface = &rSurface;
	m_charWidth = charWidth;
	m_charHeight = charHeight;
	m_charsPerRow = charsPerRow;

	m_firstChar = firstChar;
	m_lastChar = lastChar;
	m_errChar = errChar;

	m_pSurface->AddRef();

	success = TRUE;
}

// -----------------------------------------------------------------------------

void FontSurface::BltChar(IDirectDrawSurface* pDst, int x, int y, char c) 
{
	if((c<m_firstChar) || (c>m_lastChar))
		c = m_errChar;

	c -= m_firstChar;

	RECT dst;
	SetRect(&dst, x, y, x+CharWidth(), y+CharHeight());
	
	RECT src;
	src.left = (int)(c%m_charsPerRow)*CharWidth();
	src.top = (int)(c/m_charsPerRow)*CharHeight();
	src.right = src.left + CharWidth();
	src.bottom = src.top + CharHeight();

	pDst->Blt(&dst, m_pSurface, &src, DDBLT_WAIT, NULL);
}

// -----------------------------------------------------------------------------

void FontSurface::BltString(IDirectDrawSurface* pDst, int x, int y, char* p) 
{
	while(*p)
	{
		BltChar(pDst, x, y, *p);
		p++;
		x+=CharWidth();
	}
}

// -----------------------------------------------------------------------------

FontSurface* FontSurface::LoadFont(IDirectDraw& rDD, char* pFilename, int charWidth, int charHeight, int charsPerRow, char firstChar, char lastChar, char errChar, pixelType texFmtOverride) 
{
	IDirectDrawSurface* pSurface = LoadSurface(rDD, pFilename, FALSE, texFmtOverride);
	FontSurface* pFont = new FontSurface(*pSurface, charWidth, charHeight, charsPerRow, firstChar, lastChar, errChar);
	pSurface->Release();
	return pFont;
}

// -----------------------------------------------------------------------------
