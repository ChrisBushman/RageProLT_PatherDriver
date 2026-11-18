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
#ifndef VQT_H
#define VQT_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"

// -----------------------------------------------------------------------------

struct VqtCodebookEntry {
	WORD	t0, t1;
	WORD	t2, t3;
};

struct VqtCodebook {
	VqtCodebookEntry entry[256];
};

// -----------------------------------------------------------------------------

extern BOOL IsVQT(const char* pFilename) ;

extern void VqtRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) ;
extern void VqtSize(int* pWidth, int* pHeight, pixelType* pPixelType, const char* pFilename) ;
extern int	VqtReadPalette(uint8 palette[256][3], const char* pFilename) ;
extern void	VqtReadCodebook(VqtCodebook* pCodebook, const char* pFilename) ;

// -----------------------------------------------------------------------------
#endif
