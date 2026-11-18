/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  Very simple text formatting functionality.								   *
 *******************************************************************************/
#include "stdwin.h"
#include <stdarg.h>
#include <ddraw.h>

#include "Ati3dCIF.h"
#include "Ati3dCIFx.h"
#include "DirectDraw.h"

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // warning C4290: C++ Exception Specification ignored

// -----------------------------------------------------------------------------

static char* s_characters[256][8] = {
#include "font.inc"
};

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Write a pixel at location x,y in the destination surface given that the    *
 *	pixel lies on the surface (valid x,y).									   *
 *******************************************************************************/
void Ati3dContext::RenderPixel(int x, int y, WORD color)
{
	if((x>=0) && (x<m_width) &&
	   (y>=0) && (y<m_height))
		((WORD *)m_pDst)[y * m_pixelsPerRow + x] = color;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Render an 8x8 character to the destination buffer starting at pixel 	   *
 *	location x,y. Characters are stored in 8x8 array representing the 		   *
 *	character box, such that an 'X' represents a pixel in the foreground 	   *
 *	of the character (i.e, part of the glyph), otherwise the array 			   *
 *	location represents the background. Source character arrays can 		   *
 *	be found in FONT.INC.													   *
 *******************************************************************************/
void Ati3dContext::RenderChar(int x, int y, char c, int fg, int bg)
{
	char** src = s_characters[c];

	for(int j=0; j<8; j++)
		for(int i=0; i<8; i++)
			if(src[j][i]=='X')
			{
				if(fg>=0)
					RenderPixel(x+i, y+j, (WORD)fg);
			}
			else
			{
				if(bg>=0)
					RenderPixel(x+i, y+j, (WORD)bg);
			}
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Write a string of characters to the destination surface starting at 	   *	   
 *	location x,y using the specified foreground and background colors.		   *
 *******************************************************************************/
void Ati3dContext::RenderText(int x, int y, char* string, int fg, int bg)
{
	while(*string)
	{
		RenderChar(x, y, *string, fg, bg);
		string++;
		x+=8;
	}
}

// -----------------------------------------------------------------------------
