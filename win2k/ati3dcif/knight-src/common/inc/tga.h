/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Truevision TARGA (TGA) file handling functionality. Various resolutions    *
 *	are supported.															   *
 *******************************************************************************/
#ifndef TGA_H
#define TGA_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"

// -----------------------------------------------------------------------------

extern BOOL IsTGA(const char* pFilename) ;

extern void TgaRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) ;
extern void TgaSize(int* pWidth, int* pHeight, pixelType* pPixelType, const char* pFilename) ;
extern int  TgaReadPalette(uint8 palette[256][3], const char* pFilename) ;

// -----------------------------------------------------------------------------
#endif
