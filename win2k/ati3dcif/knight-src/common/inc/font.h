/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	Simple text string rendering utilities. Functionality to load a font,      *
 *	create a surface to hold a character, blit a character, and blit a string. *
 *******************************************************************************/
#ifndef FONT_H
#define FONT_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"
#include "Ati3dCIFx.h"

// -----------------------------------------------------------------------------

class FontSurface {
public:
	~FontSurface(void);
	FontSurface(IDirectDrawSurface& rSurface, int charWidth, int charHeight, int charsPerRow, char firstChar, char lastChar, char errChar) /*  */;

	int CharWidth(void) const { return m_charWidth; }
	int CharHeight(void) const { return m_charHeight; }

	void BltChar(IDirectDrawSurface* pDst, int x, int y, char c) ;
	void BltString(IDirectDrawSurface* pDst, int x, int y, char* p) ;

	static FontSurface* LoadFont(IDirectDraw& rDD, char* pFilename, int charWidth, int charHeight, int charsPerRow, char firstChar, char lastChar, char errChar, pixelType texFmtOverride = PIXELTYPE_UNKNOWN) ;

protected:
private:
	IDirectDrawSurface*	m_pSurface;
	int					m_charWidth;
	int					m_charHeight;
	int					m_charsPerRow;
	char				m_firstChar;
	char				m_lastChar;
	char				m_errChar;

	FontSurface(const FontSurface&);	// not allowed
	FontSurface& operator=(const FontSurface&);	// not allowed
};

// -----------------------------------------------------------------------------
#endif
