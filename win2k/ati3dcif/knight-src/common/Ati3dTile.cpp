/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  C++ texture address tiling code.                                           *
 *******************************************************************************/

#include "stdwin.h"
#include "ati3dcifx.h"

// -----------------------------------------------------------------------------

#define TEX_LOG_MAX 10
#define TEX_LIN_MAX (1<<TEX_LOG_MAX)
#define BIT10_MASK 0x400
#define BIT12_MASK 0x1000

// -----------------------------------------------------------------------------

// These lookup tables are generated once for each texel size of 8, 16, 32
// at load time and so generation speed is not an issue 
static DWORD gdwS8Lookup [ TEX_LIN_MAX ];
static DWORD gdwT8Lookup [ TEX_LIN_MAX ];
static DWORD gdwS16Lookup[ TEX_LIN_MAX ];
static DWORD gdwT16Lookup[ TEX_LIN_MAX ];
static DWORD gdwS32Lookup[ TEX_LIN_MAX ];
static DWORD gdwT32Lookup[ TEX_LIN_MAX ];
static DWORD gdwT8Mask[TEX_LOG_MAX + 1];
static DWORD gdwT16Mask[TEX_LOG_MAX + 1];
static DWORD gdwT32Mask[TEX_LOG_MAX + 1];

static class _AutoInit {
public:
	_AutoInit(void);
} __autoinit;

// -----------------------------------------------------------------------------

static inline DWORD Log2(DWORD value)
{
	DWORD result = 0;
	do
	{
		result++;
		value >>= 1;
	}
	while(value > 1);
	return result;
}

// -----------------------------------------------------------------------------

// SCoord = Sn-1,Sn-2,...,S1,S0
// TCoord = Tn-1,Tn-2,...,T1,T0

// dwReadOff  = Tn-1,Sn-1,Tn-2,Sn-2,...,T1,S1,T0,S0
// dwWriteOff = Tn-1,Tn-2,...,T1,T0,Sn-1,Sn-2,...,S1,S0

// Lets have four lookup tables, one for S and one for T and OR together the results.
// Then, to handle the extra bits on top for non-square maps, we have one for masking
// the original address and one to shift those masked bits into position.

// dwWriteOff = (TCoord << n) | SCoord;
// dwReadOff  = TLookup[TCoord] | SLookup[SCoord] | ((dwWriteOff & Mask[MinCoord]) << Shift[MinCoord]);

// For the masking and shifting, there is a table for S and a table for T.  We use the table
// based on the coordinate with the larger extent, and index into the table with the
// coordinate with the smaller extent.

// -----------------------------------------------------------------------------

// How to generate the lookup tables. 

// To generate the lookup tables for the different pix widths, use yet another
// set of smaller lookup tables which have the S and T bit locations in them 
// for the different texel sizes:

// These arrays for 8 bit texels represent the following interleave:
// T9 S9 T8 S8 T7 S7 T6 S6 T5 S5 T4 S4 T3 S3 T2 T1 S2 T0 S1 S0
static DWORD gdwS8Bits[11] = {
    0x00001,
    0x00002,
    0x00008,
    0x00040, // deliberate stutter step
    0x00100,
    0x00400,
    0x01000,
    0x04000,
    0x10000,
    0x40000,
	0x00000
};
static DWORD gdwT8Bits[10] = {
    0x00004,
    0x00010, // deliberate stutter step
    0x00020,
    0x00080, 
    0x00200,
    0x00800,
    0x02000,
    0x08000,
    0x20000,
    0x80000
};

static DWORD gdwS8Mask[11] = {0xFFF, 0xFFC, 0xFF8, 0xFF8, 0xFF0, 0xFE0, 0xFC0, 0xF80, 0xF00, 0xE00, 0xC00};

// These arrays for 16 bit texels represent the following interleave:
// T9 S9 T8 S8 T7 S7 T6 S6 T5 S5 T4 S4 T3 S3 T2 S2 T1 T0 S1 S0
static DWORD gdwS16Bits[] = {
    0x00001,
    0x00002,
    0x00010, // deliberate stutter step
    0x00040, 
    0x00100,
    0x00400,
    0x01000,
    0x04000,
    0x10000,
    0x40000,
	0x00000
};
static DWORD gdwT16Bits[] = {
    0x00004,
    0x00008, 
    0x00020, // deliberate stutter step
    0x00080, 
    0x00200,
    0x00800,
    0x02000,
    0x08000,
    0x20000,
    0x80000
};
    
static DWORD gdwS16Mask[] = {0xFFF, 0xFFC, 0xFFC, 0xFF8, 0xFF0, 0xFE0, 0xFC0, 0xF80, 0xF00, 0xE00, 0xC00, 0x800};

// These arrays for 32 bit texels represent the following interleave:
// T9 S9 T8 S8 T7 S7 T6 S6 T5 S5 T4 S4 T3 S3 T2 S2 T1 S1 T0 S0
static DWORD gdwS32Bits[] = {
    0x00001,
    0x00004,
    0x00010,
    0x00040, 
    0x00100,
    0x00400,
    0x01000,
    0x04000,
    0x10000,
    0x40000,
	0x00000
};
static DWORD gdwT32Bits[] = {
    0x00002,
    0x00008, 
    0x00020,
    0x00080, 
    0x00200,
    0x00800,
    0x02000,
    0x08000,
    0x20000,
    0x80000,
};

static DWORD gdwS32Mask[] = {0xFFF, 0xFFE, 0xFFC, 0xFF8, 0xFF0, 0xFE0, 0xFC0, 0xF80, 0xF00, 0xE00, 0xC00, 0x800};


// Function to preload lookup tables for tiling
// - returns TRUE on success, FALSE on fail
// - adwLookup is the output array to be filled
// - adwBits is an array of bit positions to be used in the filling algorithm
static BOOL LoadTileLookUpTable(DWORD* adwLookup, DWORD* adwBits)
{
    if(adwLookup == NULL) return FALSE;
    if(adwBits   == NULL) return FALSE;

    for(DWORD i=0; i<TEX_LIN_MAX; i++)
    {
        adwLookup[i]=0;
        DWORD bitsel = 1;
        for(int j=0; j<TEX_LOG_MAX; j++)
        {
            if ( i & bitsel )
            {
                adwLookup[i] |= adwBits[j];
            }
            bitsel <<= 1;
        }
    }

    return TRUE;
}



_AutoInit::_AutoInit(void)
{
	if(!LoadTileLookUpTable(gdwS8Lookup,  gdwS8Bits)) throw 0;
    if(!LoadTileLookUpTable(gdwT8Lookup,  gdwT8Bits)) throw 0;
    if(!LoadTileLookUpTable(gdwS16Lookup, gdwS16Bits)) throw 0;
    if(!LoadTileLookUpTable(gdwT16Lookup, gdwT16Bits)) throw 0;
    if(!LoadTileLookUpTable(gdwS32Lookup, gdwS32Bits)) throw 0;
    if(!LoadTileLookUpTable(gdwT32Lookup, gdwT32Bits)) throw 0;

	for(int i=0; i<11; i++)
	{
		gdwT8Mask[i] = ~(gdwS8Bits[i]-1);
		gdwT16Mask[i] = ~(gdwS16Bits[i]-1);
		gdwT32Mask[i] = ~(gdwS32Bits[i]-1);
	}
}

// -----------------------------------------------------------------------------

void Ati3dTexture::Tile(void* pvDstMap,
						void* pvSrcMap,
						C3D_ETEXFMT eFmt,
						DWORD dwWidth, 
						DWORD dwHeight) 
{
	ASSERT(pvDstMap);
	ASSERT(pvSrcMap);
	ASSERT(dwWidth <= TEX_LIN_MAX);
	ASSERT(dwHeight <= TEX_LIN_MAX);

	DWORD	dwLogMapWidth = Log2(dwWidth);
	DWORD	dwLogMapHeight = Log2(dwHeight);
	DWORD	dwLogMapSide = min(dwLogMapWidth, dwLogMapHeight);
    DWORD	dwLinMapWidth = 1 << dwLogMapWidth;
    DWORD	dwLinMapHeight = 1 << dwLogMapHeight;
	DWORD	dwLinMapSide = 1 << dwLogMapSide;
    DWORD	dwTotalTexels = dwLinMapWidth * dwLinMapHeight;
    DWORD	dwSBitMask = dwLinMapWidth - 1;	// create a mask for the S bits in a texel address
	DWORD	dwXBitMask = dwLinMapSide - 1;
    DWORD*	pdwSrc = (DWORD *) pvSrcMap;
    DWORD*	pdwDstBase = (DWORD *) pvDstMap;
	DWORD	dwHighEndShift = (dwLogMapWidth>=dwLogMapHeight)?dwLogMapSide:0;

    // Set up for different texel sizes
    DWORD	dwAddrShift;
    DWORD*	adwSLookup;
	DWORD*	adwTLookup;
    DWORD	dwTexelsPerDword;
	DWORD	dwHighEndMask;
	BOOL	bGreyCode;
	DWORD	dwTotalBytes;
	switch(eFmt)
	{
    case C3D_ETF_CI4:		// 4BPP Pseudocolor (not supported for SCALING)
    case C3D_ETF_CI8:		// 8BPP Pseudocolor 
    case C3D_ETF_VQ:		// VQ compressed texture
    case C3D_ETF_RGB332:	// 0b Alpha, 3b Red, 3b Green, 2b Blue (08)     
    case C3D_ETF_Y8:		// 8b Y                                (08) 
    case C3D_ETF_YUV422:	// YUV 422 Packed (YUYV) MS FOURCC_UYVY(16)
        dwAddrShift = 2;
        dwTexelsPerDword = 4;
        adwSLookup = gdwS8Lookup;
        adwTLookup = gdwT8Lookup;
		bGreyCode = ((dwLogMapWidth > 6) && (dwLogMapHeight > 5));
		dwHighEndMask = (dwLogMapWidth>=dwLogMapHeight)?gdwS8Mask[dwLogMapSide] & dwSBitMask : gdwT8Mask[dwLogMapSide];
		dwTotalBytes = dwTotalTexels * 1;
		break;

    case C3D_ETF_RGB1555:	// 1b Alpha, 5b Red, 5b Green, 5b Blue (16)
    case C3D_ETF_RGB565:	// 0b Alpha, 5b Red, 6b Green, 5b Blue (16) 
    case C3D_ETF_RGB4444:	// 4b Alpha, 4b Red, 4b Green, 4b Blue (16) 
        dwAddrShift = 1;
        dwTexelsPerDword = 2;
        adwSLookup = gdwS16Lookup;
        adwTLookup = gdwT16Lookup;
		bGreyCode = ((dwLogMapWidth > 5) && (dwLogMapHeight > 5));
		dwHighEndMask = (dwLogMapWidth>=dwLogMapHeight)?gdwS16Mask[dwLogMapSide] & dwSBitMask : gdwT16Mask[dwLogMapSide];
		dwTotalBytes = dwTotalTexels * 2;
		break;

    case C3D_ETF_RGB8888:	// 8b Alpha, 8b Red, 8b Green, 8b Blue (32) 
        dwAddrShift = 0;
        dwTexelsPerDword = 1;
        adwSLookup = gdwS32Lookup;
        adwTLookup = gdwT32Lookup;
		bGreyCode = ((dwLogMapWidth > 5) && (dwLogMapHeight > 4));
		dwHighEndMask = (dwLogMapWidth>=dwLogMapHeight)?gdwS32Mask[dwLogMapSide] & dwSBitMask : gdwT32Mask[dwLogMapSide];
		dwTotalBytes = dwTotalTexels * 4;
		break;

	default:
		THROW_EXCEPTION();
		break;
	}

    // tile copy is done by DWORD ....
    // ... possible since interleave is never finer than a DWORD
    // one iteration handles one 32 bit texel
    // or two 16 bit texels or four 8 bit texels
    for (DWORD i=0; i<dwTotalTexels; i+=dwTexelsPerDword)
    {
		DWORD addr = adwSLookup[(i & dwSBitMask) & dwXBitMask] | adwTLookup[(i >> dwLogMapWidth) & dwXBitMask];
		addr |= (i & dwHighEndMask) << dwHighEndShift;

        // adjust for DWORD pointer, based on size of texel
        // this shift can be moved into the lookup table later for speed
        addr >>= dwAddrShift;

		if(bGreyCode)
		{
			// Now xor bit 10 into bit 9 to make grey code of bits 11 and 12
			// once padded out to an address of a 32 bit word
			DWORD k  = (addr & BIT10_MASK)>>1;
			addr ^= k;
		}

        // make sure we dont wander out of range
        ASSERT( addr < dwTotalBytes/4 );
        
        // copy data a DWORD at a time 
        *(pdwDstBase + addr) = *pdwSrc++;
    }

    return;
}

// -----------------------------------------------------------------------------
