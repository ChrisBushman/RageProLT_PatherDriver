/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  PCX file handling functionality. Various resolutions are supported.		   *
 *******************************************************************************/
#ifndef PCX_H
#define PCX_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"

// -----------------------------------------------------------------------------

extern BOOL IsPCX(const char* pFilename) ;

extern void PcxRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) ;
extern void PcxSize(int* pWidth, int* pHeight, pixelType* pPixelType, const char* pFilename) ;
extern int  PcxReadPalette(uint8 palette[256][3], const char* pFilename) ;

// -----------------------------------------------------------------------------
#endif
