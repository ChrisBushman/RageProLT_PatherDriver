/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 * 	PCX and TGA data handling functionality. 							       *
 *******************************************************************************/
#ifndef IMAGE_H
#define IMAGE_H
// -----------------------------------------------------------------------------

#include "DirectDraw.h"
#include "pcx.h"
#include "tga.h"
#include "vqt.h"

// -----------------------------------------------------------------------------

void ImageRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) ;
void ImageSize(int *pWidth, int *pHeight, pixelType *pPixelType, const char *pFilename) ;
int ImageReadPalette(uint8 palette[256][3], const char* pFilename) ;

// -----------------------------------------------------------------------------
#endif
