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
#include "stdwin.h"
#include <stdio.h>

#include "pcx.h"
#include "DirectDraw.h"
#include "watchers.h"

// -----------------------------------------------------------------------------

struct PcxHeader {
	BYTE	manufacturer;
	BYTE	version;
	BYTE	encoding;
	BYTE	bitsPerPixel;
	WORD	xMin, yMin, xMax, yMax;
	WORD	hDPI, vDPI;
	BYTE	colormap[16][3];
	BYTE	reserved;
	BYTE	nPlanes;
	WORD	bytesPerLine;
	WORD	paletteInfo;
	WORD	hScreenSize, vScreenSize;
	BYTE	filler[54];
};

// -----------------------------------------------------------------------------

static void readHeader(PcxHeader *pHeader, fileHandle& fh) 
{
	ASSERT(pHeader);

	fh.rewind();
	fh.read(pHeader, sizeof(PcxHeader));
}

// -----------------------------------------------------------------------------

void copy_8_to_8(BYTE* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	int width1 = header.xMax - header.xMin + 1;

	while(height--)
	{
		int width = width1;

		while(width--)
			*pDst++ = *pSrc++;

		pDst += dstPixelsPerRow - width1;
	}
}

void decode_8_to_8(BYTE* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	int width1 = header.xMax - header.xMin + 1;

	while(height--)
	{
		int width = width1;

		while(width)
		{
			BYTE code = *pSrc++;

			if((code & 0xC0) == 0xC0)
			{
				code &= 0x3F;
				ASSERT(code <= width);
				BYTE data = *pSrc++;
				width -= code;
				while(code--)
					*pDst++ = data;
			}
			else
			{
				*pDst++ = code;
				width--;
			}
		}

		pDst += dstPixelsPerRow - width1;
	}
}

// -----------------------------------------------------------------------------

void copy_24_to_332(BYTE* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		for(int x=0; x<width; x++)
			pDst[x] = (*pSrc++) & 0xE0;
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= ((*pSrc++) >> 3) & 0x1C;
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= ((*pSrc++) >> 6) & 0x03;
		pSrc += header.bytesPerLine - width;

		pDst += dstPixelsPerRow;
	}
}



void decode_24_to_332(BYTE* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		BYTE code = 0;
		WORD data;

		for(int x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (*pSrc++) & 0xE0;
					code &= 0x3F;
				}
				else
				{
					data = (code) & 0xE0;
					code = 1;
				}
			}

			pDst[x] = data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = ((*pSrc++) >> 3) & 0x1C;
					code &= 0x3F;
				}
				else
				{
					data = ((code) >> 3) & 0x1C;
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = ((*pSrc++) >> 6) & 0x03;
					code &= 0x3F;
				}
				else
				{
					data = ((code) >> 6) & 0x03;
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		ASSERT(code == 0);

		pDst += dstPixelsPerRow;
	}
}

// -----------------------------------------------------------------------------

void copy_24_to_1555(WORD* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		for(int x=0; x<width; x++)
			pDst[x] = (WORD)((((DWORD)(*pSrc++)) << 7) & 0x7C00);
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= (WORD)((((DWORD)(*pSrc++)) << 2) & 0x03E0);
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= (WORD)((((DWORD)(*pSrc++)) >> 3) & 0x001F);
		pSrc += header.bytesPerLine - width;

		pDst += dstPixelsPerRow;
	}
}



void decode_24_to_1555(WORD* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		BYTE code = 0;
		WORD data;

		for(int x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) << 7) & 0x7C00) | 0x8000;
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) << 7) & 0x7C00) | 0x8000;
					code = 1;
				}
			}

			pDst[x] = data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) << 2) & 0x03E0);
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) << 2) & 0x03E0);
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) >> 3) & 0x001F);
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) >> 3) & 0x001F);
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		ASSERT(code == 0);

		pDst += dstPixelsPerRow;
	}
}

// -----------------------------------------------------------------------------

void copy_24_to_565(WORD* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		for(int x=0; x<width; x++)
			pDst[x] = (WORD)((((DWORD)(*pSrc++)) << 8) & 0xF800);
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= (WORD)((((DWORD)(*pSrc++)) << 3) & 0x07E0);
		pSrc += header.bytesPerLine - width;

		for(x=0; x<width; x++)
			pDst[x] |= (WORD)((((DWORD)(*pSrc++)) >> 3) & 0x001F);
		pSrc += header.bytesPerLine - width;

		pDst += dstPixelsPerRow;
	}
}



void decode_24_to_565(WORD* pDst, int dstPixelsPerRow, BYTE* pSrc, const PcxHeader& header)
{
	int height = header.yMax - header.yMin + 1;
	while(height--)
	{
		int width = header.xMax - header.xMin + 1;

		BYTE code = 0;
		WORD data;

		for(int x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) << 8) & 0xF800);
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) << 8) & 0xF800);
					code = 1;
				}
			}

			pDst[x] = data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) << 3) & 0x07E0);
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) << 3) & 0x07E0);
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		data >>= 5;

		for(x=0; x<width; x++)
		{
			if(code == 0)
			{
				code = *pSrc++;
				if((code & 0xC0) == 0xC0)
				{
					data = (WORD)((((DWORD)(*pSrc++)) >> 3) & 0x001F);
					code &= 0x3F;
				}
				else
				{
					data = (WORD)((((DWORD)(code)) >> 3) & 0x001F);
					code = 1;
				}
			}

			pDst[x] |= data;
			code--;
		}

		ASSERT(code == 0);

		pDst += dstPixelsPerRow;
	}
}

// -----------------------------------------------------------------------------

void PcxRead(void*			pDst,
			 int			dstWidth, 
			 int			dstHeight, 
			 pixelType		dstPixelType, 
			 int			dstBytesPerRow, 
			 const char*	pFilename) 
{
	ASSERT(pDst);
	ASSERT(pFilename);

	HANDLE hFile = CreateFile(pFilename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(hFile == INVALID_HANDLE_VALUE) THROW_FH_EXCEPTION(FHERROR_CANNOTOPEN);
	DECLARE_FUNCTION_DOER_WINAPI(HANDLE, BOOL, hFile, CloseHandle, hFile);

	DWORD length = GetFileSize(hFile, NULL);
	if(length == 0xFFFFFFFF) THROW_FH_EXCEPTION(FHERROR_CANNOTSEEK);

	HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if(hMapping == NULL) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	DECLARE_FUNCTION_DOER_WINAPI(HANDLE, BOOL, hMapping, CloseHandle, hMapping);

	LPCVOID pSrc = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);	// const: matches UnmapViewOfFile doer T. CWB
	if(pSrc == NULL) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	DECLARE_FUNCTION_DOER_WINAPI(LPCVOID, BOOL, pSrc, UnmapViewOfFile, pSrc);

	PcxHeader* pHeader = (PcxHeader*)pSrc;
	if(pHeader->manufacturer != 0x0A) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	if(pHeader->version != 5) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	int width = pHeader->xMax - pHeader->xMin + 1;
	int height = pHeader->yMax - pHeader->yMin + 1;

	if(dstWidth < width) THROW_FH_EXCEPTION(FHERROR_UNKNOWN);
	if(dstHeight < height) THROW_FH_EXCEPTION(FHERROR_UNKNOWN);

	if((dstPixelType != PIXELTYPE_1555) &&
		(dstPixelType != PIXELTYPE_565) &&
		(dstPixelType != PIXELTYPE_332) &&
		(dstPixelType != PIXELTYPE_8)) THROW_FH_EXCEPTION(FHERROR_UNKNOWN);
	if(pHeader->bitsPerPixel!=8) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
	if((pHeader->nPlanes!=3) && (pHeader->nPlanes!=1)) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	if((pHeader->nPlanes == 1) && (dstPixelType != PIXELTYPE_8)) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	if(dstPixelType == PIXELTYPE_8)
	{
		if(pHeader->encoding == 1)
			decode_8_to_8((BYTE*)pDst, dstBytesPerRow, (BYTE*)(pHeader + 1), *pHeader);
		else
			copy_8_to_8((BYTE*)pDst, dstBytesPerRow, (BYTE*)(pHeader + 1), *pHeader);
	}
	else if(dstPixelType == PIXELTYPE_332)
	{
		if(pHeader->encoding == 1)
			decode_24_to_332((BYTE*)pDst, dstBytesPerRow, (BYTE*)(pHeader + 1), *pHeader);
		else
			copy_24_to_332((BYTE*)pDst, dstBytesPerRow, (BYTE*)(pHeader + 1), *pHeader);
	}
	else if(dstPixelType == PIXELTYPE_1555)
	{
		if(pHeader->encoding == 1)
			decode_24_to_1555((WORD*)pDst, dstBytesPerRow/2, (BYTE*)(pHeader + 1), *pHeader);
		else
			copy_24_to_1555((WORD*)pDst, dstBytesPerRow/2, (BYTE*)(pHeader + 1), *pHeader);
	}
	else if(dstPixelType == PIXELTYPE_565)
	{
		if(pHeader->encoding == 1)
			decode_24_to_565((WORD*)pDst, dstBytesPerRow/2, (BYTE*)(pHeader + 1), *pHeader);
		else
			copy_24_to_565((WORD*)pDst, dstBytesPerRow/2, (BYTE*)(pHeader + 1), *pHeader);
	}
}

// -----------------------------------------------------------------------------

void PcxSize(int* pWidth, int* pHeight, pixelType* pPixelType, fileHandle& fh) 
{
	ASSERT(pWidth);
	ASSERT(pHeight);
	ASSERT(pPixelType);

	PcxHeader header;
	readHeader(&header, fh);
	(*pWidth) = header.xMax - header.xMin + 1;
	(*pHeight) = header.yMax - header.yMin + 1;
	switch(header.bitsPerPixel * header.nPlanes)
	{
	case 8: (*pPixelType) = PIXELTYPE_8; break;
	case 16: (*pPixelType) = PIXELTYPE_1555; break;
	case 24: (*pPixelType) = PIXELTYPE_1555; break;
	case 32: (*pPixelType) = PIXELTYPE_8888; break;
	default: THROW_FH_EXCEPTION(FHERROR_CANNOTOPEN); break;
	}
}



void PcxSize(int *pWidth, int *pHeight, pixelType* pPixelType, const char *pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");
	PcxSize(pWidth, pHeight, pPixelType, fh);
}

// -----------------------------------------------------------------------------

int PcxReadPalette(uint8 palette[256][3], const char* pFilename) 
{
	ASSERT(pFilename);

	fileHandle fh(pFilename, "rb");

	PcxHeader header;
	fh.read(&header, sizeof(header));

	if(header.manufacturer != 0x0A) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);

	for(int i=0; i<16; i++)
	{
		for(int j=0; j<16; j++)
		{
			palette[j*16+i][0] = header.colormap[i][0];
			palette[j*16+i][1] = header.colormap[i][1];
			palette[j*16+i][2] = header.colormap[i][2];
		}
	}

	fh.seek(-769, SEEK_END);
	if(fh.readUint8() == 0x0C)
		for(i=0; i<256; i++)
		{
			palette[i][0] = fh.readUint8();
			palette[i][1] = fh.readUint8();
			palette[i][2] = fh.readUint8();
		}

	return 256;
}

// -----------------------------------------------------------------------------

BOOL IsPCX(const char* pFilename) 
{
	ASSERT(pFilename);
	fileHandle fh(pFilename, "rb");
	if(fh.readUint8() == 0x0A)
		return TRUE;
	else
		return FALSE;
}

// -----------------------------------------------------------------------------
