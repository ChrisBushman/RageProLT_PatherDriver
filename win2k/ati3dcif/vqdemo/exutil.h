/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * exutil.h - Header file for EXUTIL.CPP.                                     *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#ifndef EXUTIL_H
#define EXUTIL_H

#include <windows.h>
#include <ddraw.h>
#include "ati3dcif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAMECOUNTINTERVAL 2
#define NEXT(x) (1 - x)
#define PREVIOUS(x) (x)

typedef struct {
    LPDIRECTDRAWSURFACE lpDDSTex;
    DDSURFACEDESC       ddsd;
    C3D_HTX             hTX;
} TEXTURE, *PTEXTURE;

typedef struct {
    LPDIRECTDRAWSURFACE lpDDSTex;
    DDSURFACEDESC       ddsd;
    C3D_HTX             hTX;
    C3D_HTXPAL          hTXPal;
    C3D_PALETTENTRY     peTexturePalette[256];
} PALTEXTURE, *PPALTEXTURE;

typedef struct {
    C3D_ERSID           eRSID;
    C3D_UINT32          u32RSData;
} RENDERCONTEXTSTATE, *PRENDERCONTEXTSTATE;

BOOL InitDirectDraw (HWND hwnd, DWORD dwWidth, DWORD dwHeight, DWORD dwBpp);
BOOL LoadBackgroundBitmap (const char *lpszFilename);
BOOL LoadTexture (const char *lpszTexFilename, PTEXTURE pTex);
BOOL UnloadTexture (PTEXTURE pTex);
BOOL LoadCI8Texture (const char* lpszTexFilename, PPALTEXTURE pTex);
BOOL LoadCI4Texture (const char* lpszTexFilename, PPALTEXTURE pPalTex, BOOL bLowNibble);
BOOL UnloadCITexture (PPALTEXTURE pTex);
BOOL CreateZBuffer (DWORD dwWidth, DWORD dwHeight, C3D_EZCMP zCompFnc, C3D_EZMODE zMode);
BOOL InitATI3DCIF (void);
BOOL StartFrameCounter (void);
BOOL DisplayFrameCounter (LPDIRECTDRAWSURFACE lpDDS);
BOOL ReadFrameCounter (float *lpfr);
BOOL PageFlip (LPDIRECTDRAWSURFACE lpDDS);
BOOL EndFrameCounter (void);
void CloseDirectDraw (void);
void CloseATI3DCIF (void);
char *ErrorMsgDirectDraw (HRESULT);

#ifdef __cplusplus
}
#endif

#endif
