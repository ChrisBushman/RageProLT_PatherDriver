#ifndef _VQLIB_H
#define _VQLIB_H

#define VQ_VERSION_STRING "0tqv"

// VQ codebook
typedef struct {
    C3D_UINT16 ul;
    C3D_UINT16 ur;
    C3D_UINT16 ll;
    C3D_UINT16 lr;
} C3D_CODEBOOK, *C3D_PCODEBOOK;

typedef struct {
	char cFormat[4];			// Format of the VQ Compression
	DWORD dwHeight;				// Height of the VQ Image
	DWORD dwWidth;				// Width of the VQ Image
	DWORD bfMipMask : 12;		// Mask of mip levels present in data - Bit 0 is highest, Bit 1 is width/2, height/2, etc
	DWORD bfBits : 4;			// set to 8 because the index array is a BYTE
	DWORD bfChroma0 : 1;		// Is color 0 used as a chroma value?
	DWORD bfReserved0 : 15;		// Currently unused - must be 0
	DWORD dwReserved1;			// Currently unused - must be 0
} VQTHEADER, *PVQTHEADER;

typedef struct {
	C3D_CODEBOOK cbCodeBook[256];
	LPDIRECTDRAWSURFACE lpDDSTex[12];
	DDSURFACEDESC ddsd[12];
	C3D_HTX hTX;
	C3D_HTXPAL hTXPal;
} VQTEXTURE, *PVQTEXTURE;

BOOL LoadVQTexture(const char *lpszTexFileName, PVQTEXTURE pVQTex);
BOOL UnloadVQTexture(PVQTEXTURE pVQTex);

#endif