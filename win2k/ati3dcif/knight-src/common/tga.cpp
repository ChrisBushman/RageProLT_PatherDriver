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
#include "stdwin.h"
#include "tga.h"
#include "types.h"
#include "util.h"

// -----------------------------------------------------------------------------

#define TGA_IMAGETYPE_NODATA		0x00
#define TGA_IMAGETYPE_COLORMASK		0x03
#define TGA_IMAGETYPE_COLORMAPPED	0x01
#define TGA_IMAGETYPE_TRUECOLOR		0x02
#define TGA_IMAGETYPE_BLACKWHITE	0x03
#define TGA_IMAGETYPE_COMPRESSED	0x08

// -----------------------------------------------------------------------------

struct TgaInfo {
	uint8 idLength;
	uint8 cmapType;
	uint8 imageType;
	uint8 cmapEntrySize;
	uint16 imageWidth;
	uint16 imageHeight;
	uint8  imageDepth;
	uint8  alphaDepth;
	uint8  imageDescriptor;
};

// -----------------------------------------------------------------------------

BOOL IsTGA(fileHandle& fh) 
{
	char tag[17];
	fh.seek(-18, SEEK_END);
	fh.read(tag, 16);
	tag[16] = 0;
	if(!strcmp(tag, "TRUEVISION-XFILE"))
		return TRUE;
	else
		return FALSE;
}

BOOL IsTGA(const char* pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");
	return IsTGA(fh);
}

// -----------------------------------------------------------------------------

void ReadInfo(TgaInfo& rInfo, fileHandle& fh) 
{
	fh.seek(0, SEEK_SET);

	rInfo.idLength = fh.readUint8();
	rInfo.cmapType = fh.readUint8();
	if(rInfo.cmapType != 0) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	rInfo.imageType = fh.readUint8();
	if(rInfo.imageType >= 128) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	if((rInfo.imageType & TGA_IMAGETYPE_COLORMASK) != TGA_IMAGETYPE_TRUECOLOR) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	fh.seek(3+4, SEEK_SET);		// colormap specification
	rInfo.cmapEntrySize = fh.readUint8();

	fh.seek(8+4, SEEK_SET);		// image specification
	rInfo.imageWidth = fh.readUint16();
	rInfo.imageHeight = fh.readUint16();
	rInfo.imageDepth = fh.readUint8();
	rInfo.imageDescriptor = fh.readUint8();

	rInfo.alphaDepth = rInfo.imageDescriptor & 0xF;
}

// -----------------------------------------------------------------------------

static void Read32_to_332(void* pDstPtr, int dstWidth, int dstHeight, int dstBytesPerRow, fileHandle& fh, const TgaInfo& info) 
{
	if(info.imageType & TGA_IMAGETYPE_COMPRESSED)
	{
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	}
	else
	{
		uint8* pDst = (uint8*)pDstPtr;
		int height = info.imageHeight;
		while(height--)
		{
			int width = info.imageWidth;
			while(width--)
			{
				uint8 b = fh.readUint8();
				uint8 g = fh.readUint8();
				uint8 r = fh.readUint8();
				uint8 a = fh.readUint8();
				*pDst++ = (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
			}

			pDst = (uint8*)((uint32)pDst + dstBytesPerRow) - info.imageWidth;
		}
	}
}

static void Read32_to_4444(void* pDstPtr, int dstWidth, int dstHeight, int dstBytesPerRow, fileHandle& fh, const TgaInfo& info) 
{
	if(info.imageType & TGA_IMAGETYPE_COMPRESSED)
	{
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	}
	else
	{
		uint16* pDst = (uint16*)pDstPtr;
		int height = info.imageHeight;
		while(height--)
		{
			int width = info.imageWidth;
			while(width--)
			{
				uint32 b = fh.readUint8();
				uint32 g = fh.readUint8();
				uint32 r = fh.readUint8();
				uint32 a = fh.readUint8();
				*pDst++ = ((a & 0xF0) << 8) | ((r & 0xF0) << 4) | (g & 0xF0) | (b >> 4);
			}

			pDst = (uint16*)((uint32)pDst + dstBytesPerRow) - info.imageWidth;
		}
	}
}


static void Read32_to_1555(void* pDstPtr, int dstWidth, int dstHeight, int dstBytesPerRow, fileHandle& fh, const TgaInfo& info) 
{
	if(info.imageType & TGA_IMAGETYPE_COMPRESSED)
	{
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	}
	else
	{
		uint16* pDst = (uint16*)pDstPtr;
		int height = info.imageHeight;
		while(height--)
		{
			int width = info.imageWidth;
			while(width--)
			{
				uint32 b = fh.readUint8();
				uint32 g = fh.readUint8();
				uint32 r = fh.readUint8();
				uint32 a = fh.readUint8();
				*pDst++ = ((a & 0x80) << 8) | ((r & 0xF8) << 7) | ((g & 0xF8) << 2) | (b >> 3);
			}

			pDst = (uint16*)((uint32)pDst + dstBytesPerRow) - info.imageWidth;
		}
	}
}

// -----------------------------------------------------------------------------

void TgaRead(void* pDst, int dstWidth, int dstHeight, pixelType dstPixelType, int dstBytesPerRow, const char* pFilename) 
{
	ASSERT(pDst);
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");

	TgaInfo info;
	ReadInfo(info, fh);

	fh.seek(18 + info.idLength, SEEK_SET);

	if(info.imageDescriptor & 0x10) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	if(!(info.imageDescriptor & 0x20))
	{
		pDst = (void*) ((uint32)pDst + (dstHeight-1) * dstBytesPerRow);
		dstBytesPerRow = -dstBytesPerRow;
	}

	switch(info.imageDepth)
	{
	case 16:
		switch(dstPixelType)
		{
		case PIXELTYPE_332:
		case PIXELTYPE_1555:
		case PIXELTYPE_565:
		case PIXELTYPE_4444:
		case PIXELTYPE_8888:
		default:
			THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
			break;
		}
		break;

	case 24:
		switch(dstPixelType)
		{
		case PIXELTYPE_332:
		case PIXELTYPE_1555:
		case PIXELTYPE_565:
		case PIXELTYPE_4444:
		case PIXELTYPE_8888:
		default:
			THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
			break;
		}
		break;

	case 32:
		switch(dstPixelType)
		{
		case PIXELTYPE_332: Read32_to_332(pDst, dstWidth, dstHeight, dstBytesPerRow, fh, info); break;
		case PIXELTYPE_4444: Read32_to_4444(pDst, dstWidth, dstHeight, dstBytesPerRow, fh, info); break;
		case PIXELTYPE_1555: Read32_to_1555(pDst, dstWidth, dstHeight, dstBytesPerRow, fh, info); break;

		case PIXELTYPE_565:
		case PIXELTYPE_8888:
		default:
			THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
			break;
		}
		break;

	default: THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	}
}

// -----------------------------------------------------------------------------

void TgaSize(int *pWidth, int *pHeight, pixelType *pPixelType, const char *pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");

	TgaInfo info;
	ReadInfo(info, fh);

	if(pWidth) *pWidth = info.imageWidth;
	if(pHeight) *pHeight = info.imageHeight;

	switch(info.imageDepth)
	{
	case 16:
		if(info.alphaDepth == 4)
			if(pPixelType) *pPixelType = PIXELTYPE_4444; else ;
		else
			if(pPixelType) *pPixelType = PIXELTYPE_1555; else ;
		break;

	case 24:
		if(info.alphaDepth == 0)
			if(pPixelType) *pPixelType = PIXELTYPE_565; else ;
		else
			THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
		break;

	case 32:
		if(info.alphaDepth == 8)
			if(pPixelType) *pPixelType = PIXELTYPE_4444; else ;
		else if(info.alphaDepth == 0)
			if(pPixelType) *pPixelType = PIXELTYPE_565; else ;
		else
			THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
		break;

	default:
		THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	}
}

// -----------------------------------------------------------------------------

int TgaReadPalette(uint8 palette[256][3], const char* pFilename) 
{
	THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	return 0;
}

// -----------------------------------------------------------------------------
