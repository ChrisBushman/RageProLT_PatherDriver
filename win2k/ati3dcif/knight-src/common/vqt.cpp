/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Vector Quantization (VQT) file handling functionality. 					   *
 *******************************************************************************/
#include "stdwin.h"
#include "vqt.h"
#include "types.h"
#include "util.h"

// -----------------------------------------------------------------------------

#define SWIZZLE(x) (((x)>>24)|(((x)>>8)&0xFF00)|(((x)<<8)&0xFF0000)|((x)<<24))

#define VQT_TAG_0	SWIZZLE('vqt0')

struct VqtHeader {
	DWORD	dwTag;
	DWORD	dwHeight;
	DWORD	dwWidth;
	DWORD	dwReserved[2];
};

// -----------------------------------------------------------------------------

BOOL IsVQT(fileHandle& fh) 
{
	fh.seek(0, SEEK_SET);
	DWORD dwFormat = fh.readUint32();
	return (dwFormat == VQT_TAG_0);
}

BOOL IsVQT(const char* pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");
	return IsVQT(fh);
}

// -----------------------------------------------------------------------------

void VqtRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) 
{
	if(dstPixelType != PIXELTYPE_VQ) THROW_EXCEPTION();

	ASSERT(pDst);
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");

	VqtHeader header;
	fh.read(&header, sizeof(header));

	if(header.dwTag != VQT_TAG_0) THROW_EXCEPTION();

	fh.seek(sizeof(VqtCodebook), SEEK_CUR);

	memset(pDst, 0, dstWidth * dstHeight);

	for(int j=0; j<dstHeight; j++)
	{
		if((DWORD)dstWidth < header.dwWidth)
		{
			fh.read(pDst, dstWidth);
			fh.seek(header.dwWidth-dstWidth, SEEK_CUR);
		}
		else
			fh.read(pDst, header.dwWidth);

		pDst = (void*)((BYTE*)pDst + dstBytesPerRow);
	}
}

// -----------------------------------------------------------------------------

void VqtSize(int *pWidth, int *pHeight, pixelType *pPixelType, const char *pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");

	VqtHeader header;
	fh.read(&header, sizeof(header));

	if(header.dwTag != VQT_TAG_0) THROW_EXCEPTION();

	if(pWidth) (*pWidth) = header.dwWidth;
	if(pHeight) (*pHeight) = header.dwHeight;
	if(pPixelType) (*pPixelType) = PIXELTYPE_VQ;
}

// -----------------------------------------------------------------------------

int VqtReadPalette(uint8 palette[256][3], const char* pFilename) 
{
	THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	return 0;
}

// -----------------------------------------------------------------------------

void VqtReadCodebook(VqtCodebook* pCodebook, const char* pFilename) 
{
	ASSERT(pCodebook);
	ASSERT(pFilename);

	fileHandle fh(pFilename, "rb");

	VqtHeader header;
	fh.read(&header, sizeof(header));

	if(header.dwTag != VQT_TAG_0) THROW_EXCEPTION();

	fh.read(pCodebook, sizeof(VqtCodebook));
}

// -----------------------------------------------------------------------------
