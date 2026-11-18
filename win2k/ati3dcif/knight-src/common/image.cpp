/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	PCX and TGA data handling functionality. 							       *
 *******************************************************************************/
#include "stdwin.h"
#include "image.h"
#include "util.h"

// -----------------------------------------------------------------------------

void ImageRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) 
{
	if(IsPCX(pFilename))
		PcxRead(pDst, dstWidth, dstHeight, dstPixelType, dstBytesPerRow, pFilename);
	else if(IsTGA(pFilename))
		TgaRead(pDst, dstWidth, dstHeight, dstPixelType, dstBytesPerRow, pFilename);
	else if(IsVQT(pFilename))
		VqtRead(pDst, dstWidth, dstHeight, dstPixelType, dstBytesPerRow, pFilename);
	else
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
}

void ImageSize(int *pWidth, int *pHeight, pixelType *pPixelType, const char *pFilename) 
{
	if(IsPCX(pFilename))
		PcxSize(pWidth, pHeight, pPixelType, pFilename);
	else if(IsTGA(pFilename))
		TgaSize(pWidth, pHeight, pPixelType, pFilename);
	else if(IsVQT(pFilename))
		VqtSize(pWidth, pHeight, pPixelType, pFilename);
	else
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
}

int ImageReadPalette(uint8 palette[256][3], const char* pFilename) 
{
	if(IsPCX(pFilename))
		return PcxReadPalette(palette, pFilename);
	else if(IsTGA(pFilename))
		return TgaReadPalette(palette, pFilename);
	else if(IsVQT(pFilename))
		return VqtReadPalette(palette, pFilename);
	else
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	return 0;
}

// -----------------------------------------------------------------------------
