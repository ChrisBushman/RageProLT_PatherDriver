#include <windows.h>
#include <ddraw.h>
#include <math.h>
#include "ati3dcif.h"
#include "vqlib.h"
#include <glob.h>

// Read the vqt file and throw it into a texture structure...

BOOL LoadVQTexture(const char *lpszTexFileName, PVQTEXTURE pVQTex)
{
	VQTHEADER vqHeader;
	HRESULT ddRetval;
	HANDLE hTexFile;
	DWORD dwBytesRead, dwMipLevels, dwHeight, dwWidth;
	DWORD dwLog2X, dwLog2Y, dwLoops, dwIndexSize;
	C3D_UINT8 *pIndexMap;
	C3D_TMAP TMap;
	C3D_EC ecRetVal;

	// set the surfaces to null
	for (int x = 0; x < 12; x++)
	{
		pVQTex->lpDDSTex[x] = NULL;
	} // for

	// open the file
	hTexFile = CreateFile(lpszTexFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_FLAG_SEQUENTIAL_SCAN, NULL);

	// check for the valid handle
	if (hTexFile == INVALID_HANDLE_VALUE)
	{
		wsprintf(gszErrMsg, "Cound not open %s", lpszTexFileName);
		CloseHandle(hTexFile);
		return FALSE;
	} // if

	// read in the vq's header information
	if ( (!ReadFile(hTexFile, &vqHeader, sizeof(vqHeader), &dwBytesRead, NULL)) ||
			(dwBytesRead != sizeof(vqHeader)) )
	{
		wsprintf(gszErrMsg, "Error reading file header");
		CloseHandle(hTexFile);
		return FALSE;
	} // if

	// check the version..
	if (vqHeader.cFormat == VQ_VERSION_STRING)
	{
		wsprintf(gszErrMsg, "Incompatable VQT file");
		CloseHandle(hTexFile);
		return FALSE;
	} // if

	// read in the codebook
	if ( (!ReadFile(hTexFile, &(pVQTex->cbCodeBook), (sizeof(pVQTex->cbCodeBook[0]) * 256), 
			&dwBytesRead, NULL)) || (dwBytesRead != (sizeof(pVQTex->cbCodeBook[0]) * 256)) )
	{
		wsprintf(gszErrMsg, "Error reading Code book");
		CloseHandle(hTexFile);
		return FALSE;
	} // if

	// register the codebook
	ecRetVal = ATI3DCIF_TexturePaletteCreate(C3D_ECI_TMAP_VQ, pVQTex->cbCodeBook, &pVQTex->hTXPal);
	if (ecRetVal != C3D_EC_OK)
	{
		wsprintf(gszErrMsg, "Failed to create VQ palette");
		CloseHandle(hTexFile);
		return FALSE;
	} // if

	dwMipLevels = vqHeader.bfMipMask;
	
	dwLog2X = dwLog2Y = 0;

	while (dwLog2X <= 11)
	{
		if( (DWORD)(2 << dwLog2X) == (vqHeader.dwHeight) ) break;
		dwLog2X++;
	}
	dwLog2X++;

	while (dwLog2Y <= 11)
	{
		if( (DWORD)(2 << dwLog2Y) == (double) (vqHeader.dwHeight) ) break;
		dwLog2Y++;
	}
	dwLog2Y++;

	dwHeight = vqHeader.dwHeight;
	dwWidth = vqHeader.dwWidth;
	dwLoops = 0;

	// the index length is going to be 1/2 the width and 1/2 the height, the 
	// height and width of the map that is stored in the file is the original
	// size, not the compressed size
	dwIndexSize = (vqHeader.dwWidth/2) * (vqHeader.dwHeight/2) * sizeof(C3D_UINT8);

	// get some memory
	pIndexMap = (C3D_UINT8 *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwIndexSize);
	if (!pIndexMap)
	{
		wsprintf(gszErrMsg, "Could not allocate memory buffer for Image Map");
		CloseHandle(hTexFile);
		return FALSE;
	}

	// zero out the structure and set the size of it
	ZeroMemory(&TMap, sizeof(TMap));
	TMap.u32Size = sizeof(TMap);

	while (dwMipLevels)
	{
		// always shift right one to divide by two, even initially
		// the vq is stored with it's expanded height and width
		dwHeight = dwHeight >> 1;
		dwWidth = dwWidth >> 1;

		// if they're both down to one, then it's time to break..
		if (dwWidth == 1 && dwHeight == 1)
			break;

		if (dwHeight < 2)
			dwHeight = 2;

		if (dwWidth < 2)
			dwWidth = 2;

		dwIndexSize = dwHeight * dwWidth * sizeof(C3D_UINT8);


		// read the index map in from file!!
		if ( (!ReadFile(hTexFile, pIndexMap, dwIndexSize, &dwBytesRead, NULL) ) ||
			(dwIndexSize != dwBytesRead) )
		{
			wsprintf(gszErrMsg, "Could not read the file %s", lpszTexFileName);
			CloseHandle(hTexFile);
			HeapFree(GetProcessHeap(), 0, pIndexMap);
			for (int x = 0; x < 12; x++)
			{
				if( pVQTex->lpDDSTex[x])
					 pVQTex->lpDDSTex[x]->Release();
			}
			return FALSE;
		}

		// create direct draw surface stuff
		ZeroMemory( &(pVQTex->ddsd[dwLoops]), sizeof(pVQTex->ddsd[dwLoops]));
		pVQTex->ddsd[dwLoops].dwSize = sizeof(pVQTex->ddsd[dwLoops]);
		pVQTex->ddsd[dwLoops].dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
		// win2k port fix: ATI's Win2000 DDraw HAL REJECTS an 8bpp palettized
		// surface allocated as DDSCAPS_VIDEOMEMORY|DDSCAPS_TEXTURE ("Could not
		// create surface for ...vqt"). System memory always allocates, and the
		// CIF only needs a host pointer to the VQ index bytes (TMap.apvLevels[]),
		// so put the index surface in system memory instead of video memory.
		pVQTex->ddsd[dwLoops].ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;
		pVQTex->ddsd[dwLoops].dwHeight = dwHeight;
		pVQTex->ddsd[dwLoops].dwWidth = dwWidth;
		pVQTex->ddsd[dwLoops].ddpfPixelFormat.dwFlags = DDPF_RGB;
		pVQTex->ddsd[dwLoops].ddpfPixelFormat.dwRGBBitCount = 8;

		pVQTex->ddsd[dwLoops].ddpfPixelFormat.dwRBitMask = 0xF800;
		pVQTex->ddsd[dwLoops].ddpfPixelFormat.dwGBitMask = 0x07E0;
		pVQTex->ddsd[dwLoops].ddpfPixelFormat.dwBBitMask = 0x001F;

		/* WIN2000 FIX (2026-09-05): ATI's Win2000 DDraw HAL rejects this surface -- it's
		 * an 8bpp DDPF_RGB surface with 16-bit RGB masks (a Win9x quirk the demo used to
		 * hold index bytes).  The CIF only needs a HOST POINTER to the index bytes (it
		 * decodes VQ itself), so allocate plain system memory instead of a DDraw surface. */
		pVQTex->lpDDSTex[dwLoops] = NULL;
		pVQTex->ddsd[dwLoops].lpSurface = HeapAlloc(GetProcessHeap(), 0, dwIndexSize);
		if (!pVQTex->ddsd[dwLoops].lpSurface)
		{
			wsprintf(gszErrMsg, "Could not allocate index buffer for %s, MipLevel %d", lpszTexFileName, dwLoops);
			CloseHandle(hTexFile);
			HeapFree(GetProcessHeap(), 0, pIndexMap);
			return FALSE;
		}
		memcpy(pVQTex->ddsd[dwLoops].lpSurface, pIndexMap, dwIndexSize);

		// save the index-buffer pointer to a texture level
		TMap.apvLevels[dwLoops] = pVQTex->ddsd[dwLoops].lpSurface;

		// knock off a mip level
		dwMipLevels = dwMipLevels >> 1;		
		dwLoops++;		
	}

	HeapFree(GetProcessHeap(), 0, pIndexMap);

	CloseHandle(hTexFile);
	
	// if
	if(vqHeader.bfMipMask > 1)
	{
		TMap.bMipMap = TRUE;
		int MaxLog;

		if( dwLog2Y > dwLog2X)
			MaxLog = dwLog2Y;
		else
			MaxLog = dwLog2X;

		// fill the rest of the avp levels up with the 4x4 map
		for (int x = MaxLog - 2; x < 12; x++)
			TMap.apvLevels[x] = pVQTex->ddsd[dwLoops-2].lpSurface;
	}
	else
		TMap.bMipMap = FALSE;

	// set the max log sizes
	TMap.u32MaxMapXSizeLg2 = dwLog2X;
	TMap.u32MaxMapYSizeLg2 = dwLog2Y;

	// no clamping
	TMap.bClampS = FALSE;
	TMap.bClampT = FALSE;

	// set to a VQ texture map format
	TMap.eTexFormat = C3D_ETF_VQ;

	// set the pointer to the palette to the texture map structure
	TMap.htxpalTexPalette = pVQTex->hTXPal;

	// register the texture map
	ecRetVal = ATI3DCIF_TextureReg(&TMap, &(pVQTex->hTX) );
	if (ecRetVal != C3D_EC_OK)
	{
		wsprintf(gszErrMsg, "Could not register %s as a texture", lpszTexFileName);
		ATI3DCIF_TexturePaletteDestroy(pVQTex->hTXPal);
		for (int x = 0; x < 12; x++)
		{
			if( pVQTex->lpDDSTex[dwLoops])
				 pVQTex->lpDDSTex[dwLoops]->Release();
		}
		return FALSE;
	}

	return TRUE;
}


BOOL UnloadVQTexture(PVQTEXTURE pVQTex)
{
	C3D_EC ecRetVal;

	// unregister the texture
	ecRetVal = ATI3DCIF_TextureUnreg (pVQTex->hTX);
	if (ecRetVal != C3D_EC_OK)
	{
		wsprintf(gszErrMsg, "Unable to unregister the texture");
		// destroy the palette
		ATI3DCIF_TexturePaletteDestroy(pVQTex->hTXPal);
		// release the surfaces
		for (int x = 0; x < 12; x++)
		{
			if( pVQTex->lpDDSTex[x])
				 pVQTex->lpDDSTex[x]->Release();
		}
		return FALSE;
	}

	// destroy the palette
	ecRetVal = ATI3DCIF_TexturePaletteDestroy(pVQTex->hTXPal);
	if (ecRetVal != C3D_EC_OK)
	{
		// release the surfaces
		for (int x = 0; x < 12; x++)
		{
			if( pVQTex->lpDDSTex[x])
				pVQTex->lpDDSTex[x]->Release();
		}
		return FALSE;
	}

	// release the surfaces
	for (int x = 0; x < 12; x++)
	{
		if( pVQTex->lpDDSTex[x])
			pVQTex->lpDDSTex[x]->Release();
	}

	return TRUE;	
}