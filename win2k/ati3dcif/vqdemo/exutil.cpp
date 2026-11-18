/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * exutil.cpp - This module contains various utility routines.                *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#include <windows.h>
#include <windowsx.h>
#include <ddraw.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include "ati3dcif.h"
#include "glob.h"
#include "exutil.h"
#include "stdio.h"

// Global variables.

LARGE_INTEGER gliTime[2];
LARGE_INTEGER gliFreq;
BOOL          gbCountFrameRate = FALSE;
BOOL          gbFive6Five = FALSE;
int           giFramesSoFar    = 0;
int           giTime           = 0;
float         gfFrameRate      = 0.0f;

// Table of initial rendering context states.

static RENDERCONTEXTSTATE arcsInitialRCStates [] = {
    {C3D_ERS_FG_CLR, 0L},
    {C3D_ERS_VERTEX_TYPE, C3D_EV_VTCF},
    {C3D_ERS_PRIM_TYPE, C3D_EPRIM_TRI},
    {C3D_ERS_SOLID_CLR, 0L},
    {C3D_ERS_SHADE_MODE, C3D_ESH_SMOOTH}, 
    {C3D_ERS_TMAP_EN, FALSE},
    {C3D_ERS_TMAP_SELECT, NULL},
    {C3D_ERS_TMAP_LIGHT, C3D_ETL_NONE},
    {C3D_ERS_TMAP_PERSP_COR, C3D_ETPC_THREE},
    {C3D_ERS_TMAP_FILTER, C3D_ETFILT_MINPNT_MAG2BY2},
    {C3D_ERS_TMAP_TEXOP, C3D_ETEXOP_NONE},
    {C3D_ERS_ALPHA_SRC, C3D_EASRC_ONE},
    {C3D_ERS_ALPHA_DST, C3D_EADST_ZERO},
    {C3D_ERS_SURF_DRAW_PTR, NULL},
    {C3D_ERS_SURF_DRAW_PITCH, 0L},
    {C3D_ERS_SURF_DRAW_PF, C3D_ETF_RGB565},
    {C3D_ERS_SURF_VPORT, NULL},
    {C3D_ERS_FOG_EN, FALSE},
    {C3D_ERS_DITHER_EN, TRUE},
    {C3D_ERS_Z_CMP_FNC, C3D_EZCMP_ALWAYS},
    {C3D_ERS_Z_MODE, C3D_EZMODE_OFF},
    {C3D_ERS_SURF_Z_PTR, NULL},
    {C3D_ERS_SURF_Z_PITCH, 0L},
    {C3D_ERS_SURF_SCISSOR, NULL},
    {C3D_ERS_NUM, 0}
};


/******************************************************************************
 * InitDirectDraw                                                             *
 *  Function: create DirectDraw object and complex flipping structure         *
 *    Inputs: hwnd - window to create DirectDraw surface                      *
 *            dwWidth - width of window in pixels                             *
 *            dwHeight - height of window in pixels                           *
 *            dwBpp - pixel depth of window in bits per pixel                 *
 *   Outputs: TRUE - initialization was successful                            *
 *            FALSE - initialization failed                                   *
 ******************************************************************************/

BOOL InitDirectDraw (HWND hwnd, DWORD dwWidth, DWORD dwHeight, DWORD dwBpp)
{
    DDSURFACEDESC   ddsd;
    DDSCAPS         ddscaps;
    HRESULT         ddretval;
    char            lpsFive6FiveStr[32];

    // Create the main DirectDraw object.

    ddretval = DirectDrawCreate (NULL, &glpDD, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create DirectDraw object");
        return FALSE;
    } // if

    // Set exclusive mode.

    ddretval = glpDD->SetCooperativeLevel (hwnd,
                                           DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not set the cooperative level");
        return FALSE;
    } // if

    // Set the video mode.

    ddretval = glpDD->SetDisplayMode (dwWidth, dwHeight, dwBpp);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not set the display mode");
        return FALSE;
    } // if

    // Create the primary surface with 1 back buffer.

    ZeroMemory (&ddsd, sizeof (ddsd));
    ddsd.dwSize = sizeof (ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP |
                          DDSCAPS_VIDEOMEMORY | DDSCAPS_COMPLEX;
    ddsd.dwBackBufferCount = 1;
    ddretval = glpDD->CreateSurface (&ddsd, &glpDDSPrimary, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create the primary surface");
        return FALSE;
    } // if

    // Get pointer to the back surface.

    ZeroMemory (&ddscaps, sizeof (ddscaps));
    ddscaps.dwCaps = DDSCAPS_BACKBUFFER;
    ddretval = glpDDSPrimary->GetAttachedSurface (&ddscaps, &glpDDSBack);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not get pointer to the back buffer");
        return FALSE;
    } // if

    // Determine if RGB565 or ARGB1555 mode enabled.

    if (dwBpp == 16)
    {
        GetPrivateProfileString ("display", "Five6Five", "0", lpsFive6FiveStr,
                                 sizeof (lpsFive6FiveStr), "system.ini");

        if (strcmp (lpsFive6FiveStr, "1") == 0)
        {
            gbFive6Five = TRUE;
        } // if
    } // if

    return TRUE;
} // InitDirectDraw


/******************************************************************************
 * LoadBackgroundBitmap                                                       *
 *  Function: load the background bitmap into a plain offscreen surface       *
 *    Inputs: lpszFilename - filename of .BMP bitmap file to load             *
 *   Outputs: TRUE - bitmap loaded successfully                               *
 *            FALSE - bitmap failed to load                                   *
 ******************************************************************************/

BOOL LoadBackgroundBitmap (const char* lpszFilename)
{
    DDSURFACEDESC   ddsd;
    HRESULT         ddretval;
    HBITMAP         hbm;
    BITMAP          bm;
    HDC             hdcImage;
    HDC             hdc;

    // Load the static background bitmap into a plain offscreen surface.

    hbm = (HBITMAP) LoadImage (NULL, lpszFilename, IMAGE_BITMAP, 0, 0,
                               LR_LOADFROMFILE | LR_CREATEDIBSECTION);

    if (!hbm)
    {
        wsprintf (gszErrMsg, "Could not load background bitmap");
        return FALSE;
    } // if

    // Get bitmap dimensions.

    GetObject (hbm, sizeof (bm), &bm);

    // Create a plain offscreen surface based on bitmap dimensions.

    ZeroMemory (&ddsd, sizeof (ddsd));
    ddsd.dwSize = sizeof (ddsd);
    ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    ddsd.dwWidth = bm.bmWidth;
    ddsd.dwHeight = bm.bmHeight;
    ddretval = glpDD->CreateSurface (&ddsd, &glpDDSOne, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create offscreen plain surface");
        DeleteObject (hbm);
        return FALSE;
    } // if

    // Init source rectangle for BltFast function.

    grctBkgd.top = 0;
    grctBkgd.left = 0;
    grctBkgd.right = bm.bmWidth;
    grctBkgd.bottom = bm.bmHeight;

    // Copy the bitmap to the offscreen surface.

    hdcImage = CreateCompatibleDC (NULL);
    SelectObject (hdcImage, hbm);

    if (glpDDSOne->GetDC (&hdc) == DD_OK)
    {
        StretchBlt (hdc, 0, 0, ddsd.dwWidth, ddsd.dwHeight, hdcImage, 0, 0,
                    ddsd.dwWidth, ddsd.dwHeight, SRCCOPY);
        glpDDSOne->ReleaseDC (hdc);
    } // if

    // Clean up.

    DeleteDC (hdcImage);
    DeleteObject (hbm);

    return TRUE;
} // LoadBackgroundBitmap


/******************************************************************************
 * InitATI3DCIF                                                               *
 *  Function: create and initialize the ATI 3D rendering context              *
 *    Inputs: none                                                            *
 *   Outputs: TRUE - initialization was successful                            *
 *            FALSE - initialization failed                                   *
 ******************************************************************************/

BOOL InitATI3DCIF (void)
{
    C3D_3DCIFINFO CIFInfo;
    BOOL bZBuffer = FALSE;
    int rcsindex = 0;
    C3D_EC ecrval;

    // Initialize the CIF driver.

    if (ATI3DCIF_Init () != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not initialize 3D driver");
        return FALSE;
    } // if

    // Set flag indicating driver is loaded.

    gbCIFInit = TRUE;

    // Create the rendering context.

    ghRC = ATI3DCIF_ContextCreate ();
    if (!ghRC)
    {
        wsprintf (gszErrMsg, "Could not create 3D rendering context");
        return FALSE;
    } // if

    // Get ATI3DCIF information to determine if Z buffering is supported.

    CIFInfo.u32Size = sizeof (CIFInfo);
    if (ATI3DCIF_GetInfo (&CIFInfo) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not get ATI3DCIF information");
        return FALSE;
    } // if

    // Determine if Z buffering is supported.

    if ((CIFInfo.u32CIFCaps1 & C3D_CAPS1_Z_BUFFER) == C3D_CAPS1_Z_BUFFER)
        bZBuffer = TRUE;

    // loop through the arcsInitialRCStates table to set initial
    // rendering context states.

    while (arcsInitialRCStates [rcsindex].eRSID != C3D_ERS_NUM)
    {
        switch (arcsInitialRCStates [rcsindex].eRSID)
        {
        // The following states will not be initialized here because 
        // they depend on the display mode and drawing and Z buffer surface
        // dimensions. The address, pitch, pixel format, and size of the 
        // display are determined by ATI3DCIF during the ATI3DCIF_Init call. 
        // Applications should set the display mode before calling 
        // ATI3DCIF_Init. These display parameters retrieved during the 
        // ATI3DCIF_init call are used to set the corresponding context states
        // during context creation.

        case C3D_ERS_SURF_DRAW_PTR:
        case C3D_ERS_SURF_DRAW_PITCH:
        case C3D_ERS_SURF_DRAW_PF:
        case C3D_ERS_SURF_VPORT:
        case C3D_ERS_SURF_Z_PTR:
        case C3D_ERS_SURF_Z_PITCH:
        case C3D_ERS_SURF_SCISSOR:
            rcsindex++;
            continue;

        // The following states should only be initialized if Z buffering
        // is supported.

        case C3D_ERS_Z_CMP_FNC:
        case C3D_ERS_Z_MODE:
            if (!bZBuffer)
            {
                rcsindex++;
                continue;
            }
            else
                break;
        } // switch

        // Set the rendering context state.

        if ((ecrval = ATI3DCIF_ContextSetState (ghRC, arcsInitialRCStates [rcsindex].eRSID,
            (C3D_PRSDATA)&(arcsInitialRCStates [rcsindex].u32RSData))) != C3D_EC_OK)
        {
            wsprintf (gszErrMsg, "Error setting initial rendering context states. rcsindex = %d, %u",
                rcsindex, ecrval);
            return FALSE;
        }

        rcsindex++;

    } // while

    return TRUE;
} // InitATI3DCIF


/******************************************************************************
 * LoadTexture                                                                *
 *  Function: load a texture map from a file into a texture surface           *
 *    Inputs: lpszTexFilename - filename of .BMP texture file to load         *
 *            pTex - pointer to texture surface                               *
 *   Outputs: TRUE - texture loaded successfully                              *
 *            FALSE - texture failed to load                                  *
 ******************************************************************************/

BOOL LoadTexture (const char* lpszTexFilename, PTEXTURE pTex)
{
    HRESULT             ddretval;
    C3D_UINT32          log2X = 0L;
    C3D_UINT32          log2Y = 0L;
    C3D_TMAP            TMap;
    int                 maxlog2;
    C3D_EC              ecRetVal;
    HANDLE              hTexFile;
    BITMAPFILEHEADER    bmfh;
    BITMAPINFOHEADER    bmih;
    C3D_UINT32          bufsize;
    void                *ptmap;
    C3D_UINT32          pitch;
    C3D_UINT8           r, g, b;
    C3D_UINT16          rgb565;
    DWORD               bytesread;

    // Validate pTex.

    if (!pTex)
    {
        wsprintf (gszErrMsg, "LoadTexture %s: invalid pointer", lpszTexFilename);
        return FALSE;
    } // if

    // Open texture map file for reading.

    hTexFile = CreateFile (lpszTexFilename, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hTexFile == INVALID_HANDLE_VALUE)
    {
        wsprintf (gszErrMsg, "Could not open texture map %s", lpszTexFilename);
        return FALSE;
    } // if

    // Read BITMAPFILEHEADER.

    if (!ReadFile (hTexFile, &bmfh, sizeof (bmfh), &bytesread, NULL) ||
        (bytesread != sizeof (bmfh)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading file header");
        return FALSE;
    } // if

    // Read BITMAPINFOHEADER.

    if (!ReadFile (hTexFile, &bmih, sizeof (bmih), &bytesread, NULL) ||
        (bytesread != sizeof (bmih)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap info header");
        return FALSE;
    } // if

    // Ensure 24bpp and compression = BI_RGB.

    if ((bmih.biCompression != BI_RGB) || (bmih.biBitCount != 24))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Bitmap is not BI_RGB 24bpp");
        return FALSE;
    } // if

    // Ensure width is power of 2 <= 1024.

    while (log2X <= 11)
    {
        if ((pow (2.0, (double)log2X)) == (double)bmih.biWidth) break;
        log2X++;
    } // while

    if (log2X == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Width of texture %s greater than 1024 or not a power of 2",
            lpszTexFilename);
        return FALSE;
    } // if

    // Ensure height is power of 2 <= 1024.

    while (log2Y <= 11)
    {
        if ((pow (2.0, (double)log2Y)) == (double)bmih.biHeight) break;
        log2Y++;
    } // while

    if (log2Y == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Height of texture %s greater than 1024 or not a power of 2",
            lpszTexFilename);
        return FALSE;
    } // if

    // Allocate memory for bitmap data.

    bufsize = bmfh.bfSize - bmfh.bfOffBits;
    ptmap = (void *) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY, sizeof (char) * bufsize);
    if (!ptmap)
    {
        wsprintf (gszErrMsg, "Could not allocate memory for texture map data");
        CloseHandle (hTexFile);
        return FALSE;
    } // if

    // Read bitmap data.

    if (!ReadFile (hTexFile, ptmap, bufsize, &bytesread, NULL) ||
        (bytesread != bufsize))
    {
        HeapFree (GetProcessHeap (), 0, ptmap);
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap data");
        return FALSE;
    } // if

    // Close the texture file; we're done with it.

    CloseHandle (hTexFile);

    // Create an offscreen surface to store the texture.

    ZeroMemory (&(pTex->ddsd), sizeof (pTex->ddsd));
    pTex->ddsd.dwSize = sizeof (pTex->ddsd);
    pTex->ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    pTex->ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    pTex->ddsd.dwWidth = bmih.biWidth;
    pTex->ddsd.dwHeight = bmih.biHeight;
    ddretval = glpDD->CreateSurface (&(pTex->ddsd), &pTex->lpDDSTex, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create texture surface for file %s",
                  lpszTexFilename);
        HeapFree (GetProcessHeap (), 0, ptmap);
        return FALSE;
    } // if

    // Get a pointer to the texture surface.

    ZeroMemory (&(pTex->ddsd), sizeof (pTex->ddsd));
    pTex->ddsd.dwSize = sizeof (pTex->ddsd);

    // Lock texture to fill ddsd member, and keep it locked so DirectDraw
    // does not move it.  Surface will be unlocked in the UnloadTexture
    // function.

    ddretval = pTex->lpDDSTex->Lock (NULL, &(pTex->ddsd),
                                     DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddretval == DDERR_SURFACELOST)
    {
        pTex->lpDDSTex->Restore ();
    } // if

    // Set biSizeImage if zero.

    if (bmih.biSizeImage == 0)
    {
        bmih.biSizeImage = bmih.biWidth * bmih.biHeight * (bmih.biBitCount/8);
    } // if

    // Pitch of each scan line.

    pitch = bmih.biSizeImage / bmih.biHeight;

    // Copy bitmap data to surface.

    for (int y = 0; y < (int) bmih.biHeight; y++)
    {
        // Scan line pixel counter.

        for (int x = 0; x < (int) bmih.biWidth; x++)
        {
            // Read RGB.

            memcpy (&b, (char *) ptmap + (y*pitch) + (3*x), sizeof (C3D_UINT8));
            memcpy (&g, (char *) ptmap + (y*pitch) + (3*x) + 1, sizeof (C3D_UINT8));
            memcpy (&r, (char *) ptmap + (y*pitch) + (3*x) + 2, sizeof (C3D_UINT8));

            // Pack color data in appropriate format.

            if (gbFive6Five)
            {
                // Pack data in RGB 565 format.

                rgb565 = ((C3D_UINT32) r & 0xf8) << 8 |
                         ((C3D_UINT32) g & 0xfc) << 3 |
                         ((C3D_UINT32) b & 0xf8) >> 3;
            }
            else
            {
                // Pack data in RGB 555 format.

                rgb565 = ((C3D_UINT32) r & 0xf8) << 7 |
                         ((C3D_UINT32) g & 0xf8) << 2 |
                         ((C3D_UINT32) b & 0xf8) >> 3;
            } // if

            // Write texture data to texture cache.

            memcpy ((char *) pTex->ddsd.lpSurface + (y*(bmih.biWidth*2)) + (2*x),
                    &rgb565, sizeof (C3D_UINT16));
        } // for
    } // for

    // Free the texture file buffer.

    HeapFree (GetProcessHeap (), 0, ptmap);

    ddretval = pTex->lpDDSTex->Unlock (NULL);
    if (ddretval != DD_OK)
    {
        if (ddretval = DDERR_SURFACELOST)
        {
            pTex->lpDDSTex->Restore ();
        } // if
    } // if

    // Fill a C3D_TMAP struct.

    ZeroMemory (&TMap, sizeof (TMap));
    TMap.u32Size = sizeof (TMap);

    // Determine the maximum log2 dimension.

    maxlog2 = (int) (log2X >= log2Y ? log2X : log2Y);

    // Set base map.
    // Note that this function does not handle mipmaps currently.

    TMap.apvLevels[0] = pTex->ddsd.lpSurface;

    TMap.bMipMap = FALSE;
    TMap.u32MaxMapXSizeLg2 = log2X;
    TMap.u32MaxMapYSizeLg2 = log2Y;
    TMap.eTexFormat = gbFive6Five ? C3D_ETF_RGB565 : C3D_ETF_RGB1555;
    SET_CIF_COLOR(TMap.clrTexChromaKey, 0, 0, 0, 0);

    // Register the texture.

    ecRetVal = ATI3DCIF_TextureReg (&TMap, &(pTex->hTX));
    if (ecRetVal != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Error registering texture for file %s", lpszTexFilename);
        pTex->lpDDSTex->Release ();
        pTex->lpDDSTex = NULL;
        return FALSE;
    } // if

    return TRUE;
} // LoadTexture


/******************************************************************************
 * UnloadTexture                                                              *
 *  Function: unregister texture and release its surface                      *
 *    Inputs: pTex - pointer to texture surface                               *
 *   Outputs: TRUE - texture unloaded successfully                            *
 *            FALSE - texture failed to unload                                *
 ******************************************************************************/

BOOL UnloadTexture (PTEXTURE pTex)
{
    C3D_EC ecval;

    if (!pTex)
    {
        wsprintf (gszErrMsg, "UnloadTexture: invalid pointer");
        return FALSE;
    } // if

    // Unregister the texture if a non-NULL handle.

    if (pTex->hTX)
    {
        ecval = ATI3DCIF_TextureUnreg (pTex->hTX);
        if ((ecval != C3D_EC_OK))
        {
            wsprintf (gszErrMsg, "UnloadTexture: error unregistering texture. ecval = %u",
                      ecval);

            pTex->lpDDSTex->Release ();
            pTex->lpDDSTex = NULL;
            return FALSE;
        } // if
    } // if

    if (pTex->lpDDSTex)
    {
        pTex->lpDDSTex->Release ();
        pTex->lpDDSTex = NULL;
    } // if

    return TRUE;
} // UnloadTexture


/******************************************************************************
 * CreateZBuffer                                                              *
 *  Function: create a Z buffer, attach it to back buffer, set z test mode,   *
 *            and set the z-compare function                                  *
 *    Inputs: dwWidth - width of z-buufer. Must be equal to width of back     *
 *                      buffer                                                *
 *            dwHeight - height of Z buffer. Must be equal to height of back  *
 *                       buffer                                               *
 *            zCompFnc - z-compare function                                   *
 *            zMode - z-buufer test mode                                      *
 *   Outputs: TRUE - Z buffer created                                         *
 *            FALSE - unable to create Z buffer                               *
 ******************************************************************************/

BOOL CreateZBuffer (DWORD dwWidth, DWORD dwHeight, C3D_EZCMP zCompFnc,
                    C3D_EZMODE zMode)
{
    DDSURFACEDESC ddsd;
    DDBLTFX       ddbltfx;
    HRESULT       ddretval;
    C3D_EC        ecRetVal;

    // Validate pointers.

    if (!ghRC || !glpDD || !glpDDSBack)
    {
        wsprintf (gszErrMsg, "NULL rendering context or DirectDraw objects");
        return FALSE;
    } // if

    // Validate Z compare function.

    switch (zCompFnc)
    {
        case C3D_EZCMP_NEVER:
        case C3D_EZCMP_LESS:
        case C3D_EZCMP_LEQUAL:
        case C3D_EZCMP_EQUAL:
        case C3D_EZCMP_GEQUAL:
        case C3D_EZCMP_GREATER:
        case C3D_EZCMP_NOTEQUAL:
        case C3D_EZCMP_ALWAYS:
            break;

        default:
            wsprintf (gszErrMsg, "invalid Z compare function");
            return FALSE;
    } // switch

    // Validate Z buffer test mode.

    switch (zMode)
    {
        case C3D_EZMODE_OFF:
        case C3D_EZMODE_TESTON:
        case C3D_EZMODE_TESTON_WRITEZ:
            break;

        default:
            wsprintf (gszErrMsg, "invalid Z buffer test mode");
            return FALSE;
    } // switch

    ZeroMemory (&ddsd, sizeof (ddsd));
    ddsd.dwSize = sizeof (ddsd);
    ddsd.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS | DDSD_ZBUFFERBITDEPTH;
    ddsd.dwWidth = dwWidth;
    ddsd.dwHeight = dwHeight;
    ddsd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_VIDEOMEMORY;
    ddsd.dwZBufferBitDepth = 16;
    ddretval = glpDD->CreateSurface (&ddsd, &glpDDSZBuffer, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create the Z buffer surface");
        return FALSE;
    } // if

    // Attach the Z buffer to the back surface.

    ddretval = glpDDSBack->AddAttachedSurface (glpDDSZBuffer);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not attach the Z buffer surface");
        return FALSE;
    } // if

    // Get pointer to Z buffer.

    ZeroMemory (&ddsd, sizeof (ddsd));
    ddsd.dwSize = sizeof (ddsd);
    ddretval = glpDDSZBuffer->Lock (NULL, &ddsd,
                                    DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddretval == DDERR_SURFACELOST)
    {
        glpDDSZBuffer->Restore ();
    } // if

    // Clear Z buffer.

    ZeroMemory (&ddbltfx, sizeof (ddbltfx));
    ddbltfx.dwSize = sizeof (ddbltfx);
    ddbltfx.dwFillDepth = 16;
    glpDDSZBuffer->Blt (NULL, NULL, NULL, DDBLT_DEPTHFILL | DDBLT_WAIT, &ddbltfx);

    // Unlock Z buffer surface.

    ddretval = glpDDSZBuffer->Unlock (NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not unlock the Z buffer surface");
        return FALSE;
    } // if

    // Set the address to the Z buffer surface in ATI3DCIF module.

    if ((ecRetVal = ATI3DCIF_ContextSetState (ghRC, C3D_ERS_SURF_Z_PTR,
        (C3D_PRSDATA) &(ddsd.lpSurface))) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not set Z buffer pointer: ecRetVal = %d",
                  (int) ecRetVal);
        return FALSE;

    } // if

    // Set the Z buffer pitch in ATI3DCIF module.

    if ((ecRetVal = ATI3DCIF_ContextSetState (ghRC, C3D_ERS_SURF_Z_PITCH,
        (C3D_PRSDATA) &(dwWidth))) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not set Z buffer pitch: ecRetVal = %d",
                  (int) ecRetVal);
        return FALSE;

    } // if

    // Set Z buffer test mode.

    if ((ecRetVal = ATI3DCIF_ContextSetState (ghRC, C3D_ERS_Z_MODE, &zMode)) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not set Z buffer mode: ecRetVal = %d", (int)ecRetVal);
        return FALSE;
    } // if

    // Set Z buffer compare function.

    if (ATI3DCIF_ContextSetState (ghRC, C3D_ERS_Z_CMP_FNC, &zCompFnc) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Could not set Z buffer function");
        return FALSE;
    } // if

    return TRUE;
} // CreateZBuffer


/******************************************************************************
 *  LoadCI8Texture                                                            *
 *  Function: create a surface, load CI8 texture, and register the texture    *
 *    Inputs: dwWidth - width of z-buufer. Must be equal to width of back     *
 *              buffer                                                        *
 *            lpszTexFilename - name of .BMP file containing the CI8 texture  *
 *            pPalTex - pointer to PALTEXTURE texture object                  *
 *   Outputs: TRUE - CI8 texture loaded and registered                        *
 *            FALSE - unable to load and register CI8 texture                 *
 ******************************************************************************/

BOOL LoadCI8Texture (const char* lpszTexFilename, PPALTEXTURE pPalTex)
{
    HRESULT             ddretval;
    C3D_UINT32          log2X = 0L;
    C3D_UINT32          log2Y = 0L;
    C3D_TMAP            TMap;
    int                 k, maxlog2;
    C3D_EC              ecRetVal;
    HANDLE              hTexFile;
    BITMAPFILEHEADER    bmfh;
    BITMAPINFOHEADER    bmih;
    RGBQUAD             rgbPalette[256];
    C3D_UINT32          bufsize;
    void*               ptmap;
    char*               bmdata;
    C3D_UINT32          pitch;
    DWORD               bytesread;

    // Validate pointer.

    if (!pPalTex)
    {
        wsprintf (gszErrMsg, "LoadCI8Texture: NULL pPalTex pointer");
        return FALSE;
    } // if

    // Open texture map file for reading.

    hTexFile = CreateFile (lpszTexFilename, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hTexFile == INVALID_HANDLE_VALUE)
    {
        wsprintf (gszErrMsg, "Could not open texture map %s", lpszTexFilename);
        return FALSE;
    } // if

    // Read BITMAPFILEHEADER.

    if (!ReadFile (hTexFile, &bmfh, sizeof (bmfh), &bytesread, NULL) ||
        (bytesread != sizeof (bmfh)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading file header");
        return FALSE;
    } // if

    // Read BITMAPINFOHEADER.

    if (!ReadFile (hTexFile, &bmih, sizeof (bmih), &bytesread, NULL) ||
        (bytesread != sizeof (bmih)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap info header");
        return FALSE;
    } // if

    // Ensure 8bpp and compression = BI_RGB.

    if ((bmih.biCompression != BI_RGB) || (bmih.biBitCount != 8))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Bitmap is not BI_RGB 8bpp");
        return FALSE;
    } // if

    // Ensure width is power of 2 <= 1024.

    while (log2X <= 11)
    {
        if ((pow (2.0, (double)log2X)) == (double)bmih.biWidth) break;
        log2X++;
    } // while

    if (log2X == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Width of texture %s greater than 1024 or not a power of 2",
            lpszTexFilename);
        return FALSE;
    } // if

    // Ensure height is power of 2 <= 1024.

    while (log2Y <= 11)
    {
        if ((pow (2.0, (double)log2Y)) == (double)bmih.biHeight) break;
        log2Y++;
    } // while

    if (log2Y == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Height of texture %s greater than 1024 or not a power of 2",
            lpszTexFilename);
        return FALSE;
    } // if

    // Now get the palette.

    if (!ReadFile (hTexFile, rgbPalette, 256 * sizeof (RGBQUAD), &bytesread, NULL) ||
        (bytesread != (256 * sizeof (RGBQUAD))))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading palette");
        return FALSE;
    } // if

    // Allocate memory for bitmap data.

    bufsize = bmfh.bfSize - bmfh.bfOffBits;
    ptmap = (void*) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY, sizeof (char) * bufsize);
    if (!ptmap)
    {
        wsprintf (gszErrMsg, "Could not allocate memory for texture map data");
        CloseHandle (hTexFile);
        return FALSE;
    } // if

    // Read bitmap data.

    if (!ReadFile (hTexFile, ptmap, bufsize, &bytesread, NULL) ||
        (bytesread != bufsize))
    {
        HeapFree (GetProcessHeap (), 0, ptmap);
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap data");
        return FALSE;
    } // if

    // Close the texture file; we're done with it.

    CloseHandle (hTexFile);

    // Create an offscreen surface to store texture.

    ZeroMemory (&(pPalTex->ddsd), sizeof (pPalTex->ddsd));
    pPalTex->ddsd.dwSize = sizeof (pPalTex->ddsd);
    pPalTex->ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    pPalTex->ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    pPalTex->ddsd.dwWidth = bmih.biWidth;
    pPalTex->ddsd.dwHeight = bmih.biHeight;
    ddretval = glpDD->CreateSurface (&(pPalTex->ddsd), &pPalTex->lpDDSTex, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create texture surface for file %s", lpszTexFilename);
        HeapFree (GetProcessHeap (), 0, ptmap);
        return FALSE;
    } // if

    // Get a pointer to the texture surface.

    ZeroMemory (&(pPalTex->ddsd), sizeof (pPalTex->ddsd));
    pPalTex->ddsd.dwSize = sizeof (pPalTex->ddsd);

    // Lock texture to fill ddsd member and to copy texture
    // BMP data to surface.

    ddretval = pPalTex->lpDDSTex->Lock (NULL, &(pPalTex->ddsd),
                                        DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddretval == DDERR_SURFACELOST)
    {
        pPalTex->lpDDSTex->Restore ();
    } // if

    // Set biSizeImage if zero.

    if (bmih.biSizeImage == 0) bmih.biSizeImage = bmih.biWidth * bmih.biHeight * (bmih.biBitCount/8);

    // Pitch of each scan line.

    pitch = bmih.biSizeImage / bmih.biHeight;

    // Set pointer to start of bitmap data.

    bmdata = (char*)ptmap;

    // Copy bitmap data to texture surface.

    for (int y=0; y < ((int)bmih.biHeight); y++)
    {
        memcpy ((char*)((unsigned long)pPalTex->ddsd.lpSurface + (y*pitch)),
                bmdata + (y*pitch), pitch);
    } // for

    // Free the texture bitmap data buffer.

    HeapFree (GetProcessHeap (), 0, ptmap);

    // Unlock texture surface.

    ddretval = pPalTex->lpDDSTex->Unlock (NULL);
    if (ddretval != DD_OK)
    {
        if (ddretval = DDERR_SURFACELOST) pPalTex->lpDDSTex->Restore ();
    } // if

    // Fill C3D_PALETTENTRY array.

    for (int i = 0; i < 256; i++)
    {
        pPalTex->peTexturePalette[i].r = rgbPalette [i].rgbRed ;
        pPalTex->peTexturePalette[i].g = rgbPalette [i].rgbGreen ;
        pPalTex->peTexturePalette[i].b = rgbPalette [i].rgbBlue ;
        pPalTex->peTexturePalette[i].flags = C3D_LOAD_PALETTE_ENTRY;
    } // for

    // Create texture palette and get handle.

    if (ATI3DCIF_TexturePaletteCreate (C3D_ECI_TMAP_8BIT, pPalTex->peTexturePalette,
        &pPalTex->hTXPal) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Error creating palette");
        return FALSE;
    } // if

    // Fill a C3D_TMAP struct.

    ZeroMemory (&TMap, sizeof (TMap));
    TMap.u32Size = sizeof (TMap);

    // Determine the maximum log2 dimension.

    maxlog2 = (int)(log2X >= log2Y ? log2X : log2Y);
    for (k = 0; k < maxlog2; k++)
    {
        TMap.apvLevels[k] = pPalTex->ddsd.lpSurface;
    } // for
    TMap.bMipMap = FALSE;
    TMap.u32MaxMapXSizeLg2 = log2X;
    TMap.u32MaxMapYSizeLg2 = log2Y;
    TMap.eTexFormat = C3D_ETF_CI8;
    SET_CIF_COLOR (TMap.clrTexChromaKey, 0, 0, 0, 0);
    TMap.htxpalTexPalette = pPalTex->hTXPal;

    // Register the texture.

    ecRetVal = ATI3DCIF_TextureReg (&TMap, &pPalTex->hTX);
    if (ecRetVal != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Error registering texture for file %s", lpszTexFilename);
        ATI3DCIF_TexturePaletteDestroy (pPalTex->hTXPal);
        pPalTex->lpDDSTex->Release ();
        pPalTex->lpDDSTex = NULL;
        return FALSE;
    } // if

    return TRUE;
} // LoadCI8Texture


/******************************************************************************
 *  UnloadCITexture                                                           *
 *  Function: unload and unregister CI texture, release its surface           *
 *    Inputs: pPalTex - pointer to PALTEXTURE texture object                  *
 *   Outputs: TRUE - CI texture unloaded and unregistered                     *
 *            FALSE - unable to unload and unregister CI texture              *
 ******************************************************************************/

BOOL UnloadCITexture (PPALTEXTURE pPalTex)
{
    C3D_EC ecval;

    // Validate pointer.

    if (!pPalTex)
    {
        wsprintf (gszErrMsg, "UnloadCITexture: invalid pointer");
        return FALSE;
    } // if

    // Unregister the texture if a non-NULL handle.

    if (pPalTex->hTX)
    {
        ecval = ATI3DCIF_TextureUnreg (pPalTex->hTX);
        if (ecval != C3D_EC_OK)
        {
            wsprintf (gszErrMsg, "UnloadCITexture: error unregistering texture");
            if (pPalTex->lpDDSTex)
            {
                pPalTex->lpDDSTex->Release ();
                pPalTex->lpDDSTex = NULL;
            } // if
            return FALSE;
        } // if
        pPalTex->hTX = NULL;
    } // if

    if (pPalTex->hTXPal)
    {
        ecval = ATI3DCIF_TexturePaletteDestroy (pPalTex->hTXPal);
        if (ecval != C3D_EC_OK)
        {
            wsprintf (gszErrMsg, "UnloadCI8Texture: error destroying texture palette");
            if (pPalTex->lpDDSTex)
            {
                pPalTex->lpDDSTex->Release ();
                pPalTex->lpDDSTex = NULL;
            } // if
            return FALSE;
        } // if
        pPalTex->hTXPal = NULL;
    } // if

    if (pPalTex->lpDDSTex)
    {
        pPalTex->lpDDSTex->Release ();
        pPalTex->lpDDSTex = NULL;
    } // if

    return TRUE;
} // UnloadCITexture


/******************************************************************************
 *  LoadCI4Texture                                                            *
 *  Function: create a surface, load CI4 texture, and register the texture    *
 *    Inputs: dwWidth - width of z-buufer. Must be equal to width of back     *
 *              buffer                                                        *
 *            lpszTexFilename - name of .BMP file containing the CI4 texture  *
 *            pPalTex - pointer to PALTEXTURE texture object                  *
 *            bLowNibble - if TRUE, texture will be loaded in low nibble,     *
 *              otherwise
 *   Outputs: TRUE - CI4 texture loaded and registered                        *
 *            FALSE - unable to load and register CI4 texture                 *
 ******************************************************************************/

BOOL LoadCI4Texture (const char* lpszTexFilename, PPALTEXTURE pPalTex,
                     BOOL bLowNibble)
{
    HRESULT             ddretval;
    C3D_UINT32          log2X = 0L;
    C3D_UINT32          log2Y = 0L;
    C3D_TMAP            TMap;
    int                 k, maxlog2;
    C3D_EC              ecRetVal;
    HANDLE              hTexFile;
    BITMAPFILEHEADER    bmfh;
    BITMAPINFOHEADER    bmih;
    RGBQUAD             rgbPalette[16];
    C3D_UINT32          bufsize;
    void*               ptmap;
    char*               pbmdata;
    char*               pPalTexwrite;
    C3D_UINT32          scanlinewidth, texpitch;
    DWORD               bytesread;
    int                 y, x;
    C3D_UINT8           bmpbyte, texbyte;
    C3D_ECI_TMAP_TYPE   eCItmap = C3D_ECI_TMAP_4BIT_LOW; // assume low

    // Validate pointer.

    if (!pPalTex)
    {
        wsprintf (gszErrMsg, "NULL pointer in LoadCI4Texture");
        return FALSE;
    } // if

    // Open texture map file for reading.

    hTexFile = CreateFile (lpszTexFilename, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hTexFile == INVALID_HANDLE_VALUE)
    {
        wsprintf (gszErrMsg, "Could not open texture map %s", lpszTexFilename);
        return FALSE;
    } // if

    // Read BITMAPFILEHEADER.

    if (!ReadFile (hTexFile, &bmfh, sizeof (bmfh), &bytesread, NULL) ||
        (bytesread != sizeof (bmfh)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading file header");
        return FALSE;
    } // if

    // Read BITMAPINFOHEADER.

    if (!ReadFile (hTexFile, &bmih, sizeof (bmih), &bytesread, NULL) ||
        (bytesread != sizeof (bmih)))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap info header");
        return FALSE;
    } // if

    // Ensure 4bpp and compression = BI_RGB.

    if ((bmih.biCompression != BI_RGB) || (bmih.biBitCount != 4))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Bitmap is not BI_RGB 4bpp");
        return FALSE;
    } // if

    // Ensure width is power of 2 <= 1024.

    while (log2X <= 11)
    {
        if ((pow (2.0, (double)log2X)) == (double)bmih.biWidth) break;
        log2X++;
    } // while

    if (log2X == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Width of texture %s greater than 1024 or not a power of 2",
                  lpszTexFilename);
        return FALSE;
    } // if

    // Ensure height is power of 2 <= 1024.

    while (log2Y <= 11)
    {
        if ((pow (2.0, (double)log2Y)) == (double)bmih.biHeight) break;
        log2Y++;
    } // while

    if (log2Y == 11)
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Height of texture %s greater than 1024 or not a power of 2",
                  lpszTexFilename);
        return FALSE;
    } // if

    // Now get the palette.

    if (!ReadFile (hTexFile, rgbPalette, 16 * sizeof (RGBQUAD), &bytesread, NULL) ||
        (bytesread != (16 * sizeof (RGBQUAD))))
    {
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading palette");
        return FALSE;
    } // if

    // Allocate memory for bitmap data.

    bufsize = bmfh.bfSize - bmfh.bfOffBits;
    ptmap = (void*) HeapAlloc (GetProcessHeap (), HEAP_ZERO_MEMORY, sizeof (char) * bufsize);
    if (!ptmap)
    {
        wsprintf (gszErrMsg, "Could not allocate memory for texture map data");
        CloseHandle (hTexFile);
        return FALSE;
    } // if

    // Read bitmap data.

    if (!ReadFile (hTexFile, ptmap, bufsize, &bytesread, NULL) ||
        (bytesread != bufsize))
    {
        HeapFree (GetProcessHeap (), 0, ptmap);
        CloseHandle (hTexFile);
        wsprintf (gszErrMsg, "Error reading bitmap data");
        return FALSE;
    } // if

    // Close the texture file; we're done with it.

    CloseHandle (hTexFile);

    // Create an offscreen surface to store texture.

    ZeroMemory (&(pPalTex->ddsd), sizeof (pPalTex->ddsd));
    pPalTex->ddsd.dwSize = sizeof (pPalTex->ddsd);
    pPalTex->ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
    pPalTex->ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    pPalTex->ddsd.dwWidth = bmih.biWidth;
    pPalTex->ddsd.dwHeight = bmih.biHeight;
    ddretval = glpDD->CreateSurface (&(pPalTex->ddsd), &pPalTex->lpDDSTex, NULL);
    if (ddretval != DD_OK)
    {
        wsprintf (gszErrMsg, "Could not create texture surface for file %s", lpszTexFilename);
        HeapFree (GetProcessHeap (), 0, ptmap);
        return FALSE;
    } // if

    // Get a pointer to the texture surface.

    ZeroMemory (&(pPalTex->ddsd), sizeof (pPalTex->ddsd));
    pPalTex->ddsd.dwSize = sizeof (pPalTex->ddsd);

    // Lock texture to fill ddsd member.

    ddretval = pPalTex->lpDDSTex->Lock (NULL, &(pPalTex->ddsd),
                                        DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
    if (ddretval == DDERR_SURFACELOST)
    {
        pPalTex->lpDDSTex->Restore ();
    } // if

    pPalTexwrite = (char *) pPalTex->ddsd.lpSurface;

    scanlinewidth = bufsize / bmih.biHeight; // scanlinewidth includes zero
                                             // padded bytes for DWORD align
    texpitch = bmih.biWidth;

    // Set pointer to start of bitmap data.

    pbmdata = (char *) ptmap;

    // Copy texture bitmap data to texture surface.

    if (bLowNibble)
    {
        eCItmap = C3D_ECI_TMAP_4BIT_LOW;
        for (y = 0; y < (int) bmih.biHeight; y++)
        {
            for (x = 0; x < (int) bmih.biWidth; x++)
            {
                bmpbyte = pbmdata[(y * scanlinewidth) + (x >> 1)];
                if ((x % 2) == 0) // low nibble
                {
                    texbyte = (C3D_UINT8)(bmpbyte & 0x0f);
                }
                else // high nibble
                {
                    texbyte = (C3D_UINT8)((bmpbyte >> 4) & 0x0f);
                } // if
                pPalTexwrite[(y * texpitch) + x] = texbyte;
            } // for
        } // for
    }
    else // high nibble
    {
        eCItmap = C3D_ECI_TMAP_4BIT_HI;
        for (y = 0; y < (int) bmih.biHeight; y++)
        {
            for (x = 0; x < (int) bmih.biWidth; x++)
            {
                bmpbyte = pbmdata[(y * scanlinewidth) + (x >> 1)];
                if ((x % 2) == 0) // low nibble
                {
                    texbyte = (C3D_UINT8)((bmpbyte & 0x0f) << 4);
                }
                else // high nibble
                {
                    texbyte = (C3D_UINT8)(bmpbyte & 0xf0);
                } // if
                pPalTexwrite[(y * texpitch) + x] = texbyte;
            } // for
        } // for
    } // if

    // Free the bitmap data buffer.

    HeapFree (GetProcessHeap (), 0, ptmap);

    // Unlock texture surface.

    ddretval = pPalTex->lpDDSTex->Unlock (NULL);
    if (ddretval != DD_OK)
    {
        if (ddretval = DDERR_SURFACELOST) pPalTex->lpDDSTex->Restore ();
    } // if

    // Fill C3D_PALETTENTRY array.

    for (int i = 0; i < 16; i++)
    {
        pPalTex->peTexturePalette[i].r = rgbPalette [i].rgbRed ;
        pPalTex->peTexturePalette[i].g = rgbPalette [i].rgbGreen ;
        pPalTex->peTexturePalette[i].b = rgbPalette [i].rgbBlue ;
        pPalTex->peTexturePalette[i].flags = C3D_LOAD_PALETTE_ENTRY;
    } // for

    // Create texture palette and get handle.

    if (ATI3DCIF_TexturePaletteCreate (eCItmap, pPalTex->peTexturePalette,
        &pPalTex->hTXPal) != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Error creating palette");
        return FALSE;
    } // if

    // Fill a C3D_TMAP struct.

    ZeroMemory (&TMap, sizeof (TMap));
    TMap.u32Size = sizeof (TMap);

    // Determine the maximum log2 dimension.

    maxlog2 = (int)(log2X >= log2Y ? log2X : log2Y);
    for (k = 0; k < maxlog2; k++)
    {
        TMap.apvLevels[k] = pPalTex->ddsd.lpSurface;
    } // for
    TMap.bMipMap = FALSE;
    TMap.u32MaxMapXSizeLg2 = log2X;
    TMap.u32MaxMapYSizeLg2 = log2Y;
    TMap.eTexFormat = C3D_ETF_CI4;
    SET_CIF_COLOR (TMap.clrTexChromaKey, 0, 0, 0, 0);
    TMap.htxpalTexPalette = pPalTex->hTXPal;

    // Register the texture.

    ecRetVal = ATI3DCIF_TextureReg (&TMap, &(pPalTex->hTX));
    if (ecRetVal != C3D_EC_OK)
    {
        wsprintf (gszErrMsg, "Error registering texture for file %s", lpszTexFilename);
        ATI3DCIF_TexturePaletteDestroy (pPalTex->hTXPal);
        pPalTex->lpDDSTex->Release ();
        pPalTex->lpDDSTex = NULL;
        return FALSE;
    } // if

    return TRUE;
} // LoadCI4Texture


/******************************************************************************
 * CloseDirectDraw                                                            *
 *  Function: destroy DirectDraw object and complex flipping structure        *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CloseDirectDraw (void)
{
    if (glpDD != NULL)
    {
        if (glpDDSPrimary != NULL)
        {
            glpDDSPrimary->Release ();
            glpDDSPrimary = NULL;
        } // if

        // Restore the display mode and destroy the DirectDraw object.

        glpDD->RestoreDisplayMode ();
        glpDD->Release ();
        glpDD = NULL;
    } // if
} // CloseDirectDraw


/******************************************************************************
 * CloseATI3DCIF                                                              *
 *  Function: destroy the rendering context and unload the ATI3DCIF module    *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CloseATI3DCIF (void)
{
    // Destroy ATI 3D rendering context.

    if (ghRC)
    {
        ATI3DCIF_ContextDestroy (ghRC);
        ghRC = NULL;
    } // if

    // Terminate the ATI 3D driver.

    if (gbCIFInit)
    {
        ATI3DCIF_Term ();
        gbCIFInit = FALSE;
    } // if
} // CloseATI3DCIF


/******************************************************************************
 * StartFrameCounter                                                          *
 *  Function: initializes the DirectDraw frame counter                        *
 *    Inputs: none                                                            *
 *   Outputs: TRUE - initialization was successful                            *
 *            FALSE - initialization failed                                   *
 ******************************************************************************/

int StartFrameCounter (void)
{
    if (gbCountFrameRate == FALSE)
    {
        if (QueryPerformanceCounter (gliTime + PREVIOUS(giTime)) == TRUE)
        {
            // Performance counter supported on this system.

            gbCountFrameRate = TRUE;
            QueryPerformanceFrequency (&gliFreq);
            giFramesSoFar = 0;
            gfFrameRate = 0.0f;
            giTime = NEXT(giTime);
        }
        else
        {
            return (FALSE);
        } // if
    } // if
    return (TRUE);
} // StartFrameCounter


/******************************************************************************
 * DisplayFrameCounter                                                        *
 *  Function: displays the DirectDraw frame counter                           *
 *    Inputs: lpDDS - pointer to the DirectDraw surface to draw the frame     *
 *                    rate upon                                               *
 *   Outputs: TRUE - successfully displayed frame rate                        *
 *            FALSE - unable to display frame rate                            *
 ******************************************************************************/

BOOL DisplayFrameCounter (LPDIRECTDRAWSURFACE lpDDS)
{
    HDC hdc;
    char buf[10];

    if (lpDDS->GetDC (&hdc) == DD_OK)
    {
        SetTextColor (hdc, RGB (255, 255, 255));
        SetBkMode (hdc, TRANSPARENT);
        sprintf (buf, "%2.2ffps", gfFrameRate);
        ExtTextOut (hdc, 10, 10, 0, NULL, buf, strlen (buf), (LPINT) NULL);
        lpDDS->ReleaseDC (hdc);
    }
    else
    {
        return (FALSE);
    } // if
    return (TRUE);
} // DisplayFrameCounter


/******************************************************************************
 * EndFrameCounter                                                            *
 *  Function: stops collection of frame rate data                             *
 *    Inputs: none                                                            *
 *   Outputs: always TRUE                                                     *
 ******************************************************************************/

BOOL EndFrameCounter (void)
{
    gbCountFrameRate = FALSE;
    return (TRUE);
} // EndFrameCounter


/******************************************************************************
 * PageFlip                                                                  *
 *  Function: performs page flips and collects frame rate statistics and      *
 *            information                                                     *
 *    Inputs: lpDDS - pointer to the DirectDraw primary surface to flip       *
 *   Outputs: always TRUE                                                     *
 ******************************************************************************/

int PageFlip (LPDIRECTDRAWSURFACE lpDDS)
{
    while (TRUE)
    {
        HRESULT ddrval;

        ddrval = lpDDS->Flip (NULL, DDFLIP_WAIT);
        if (ddrval == DD_OK)
        {
            break;
        } // if
        if (ddrval == DDERR_SURFACELOST)
        {
            ddrval = lpDDS->Restore ();
            if (ddrval != DD_OK)
            {
                break;
            } // if
        } // if
        if (ddrval != DDERR_WASSTILLDRAWING)
        {
            break;
        } // if
    } // while

    if (gbCountFrameRate == TRUE)
    {
        giFramesSoFar += 1;
        QueryPerformanceCounter (gliTime + NEXT (giTime));
        if ((gliTime[NEXT (giTime)].LowPart - gliTime[PREVIOUS (giTime)].LowPart) >
            (FRAMECOUNTINTERVAL * gliFreq.LowPart))
        {
            // Time interval elapsed, update frame rate.

            gfFrameRate = (float) (gliFreq.LowPart * (DWORD)giFramesSoFar);
            gfFrameRate /= (float) (gliTime[NEXT(giTime)].LowPart -
                                    gliTime[PREVIOUS(giTime)].LowPart);
            giTime = NEXT(giTime);
            giFramesSoFar = 0;
        } // if
    } // if
    return (TRUE);
} // PageFlip

#if 0
/******************************************************************************
 * ErrorMsgDirectDraw                                                         *
 *  Function: returns string equivalent to direct draw error code             *
 *    Inputs: hErrorCode - DirectDraw error code                              *
 *   Outputs: pointer to string equivalent of error code                      *
 ******************************************************************************/

char *ErrorMsgDirectDraw (HRESULT hErrorCode)
{
    switch (hErrorCode) {
        case DD_OK:
            return ("DD_OK");

        case DDERR_ALREADYINITIALIZED:
            return ("DDERR_ALREADYINITIALIZED");

        case DDERR_BLTFASTCANTCLIP:
            return ("DDERR_BLTFASTCANTCLIP");

        case DDERR_CANNOTATTACHSURFACE:
            return ("DDERR_CANNOTATTACHSURFACE");

        case DDERR_CANNOTDETACHSURFACE:
            return ("DDERR_CANNOTDETACHSURFACE");

        case DDERR_CANTCREATEDC:
            return ("DDERR_CANTCREATEDC");

        case DDERR_CANTDUPLICATE:
            return ("DDERR_CANTDUPLICATE");

        case DDERR_CANTLOCKSURFACE:
            return ("DDERR_CANTLOCKSURFACE");

        case DDERR_CANTPAGELOCK:
            return ("DDERR_CANTPAGELOCK");

        case DDERR_CANTPAGEUNLOCK:
            return ("DDERR_CANTPAGEUNLOCK");

        case DDERR_CLIPPERISUSINGHWND:
            return ("DDERR_CLIPPERISUSINGHWND");

        case DDERR_COLORKEYNOTSET:
            return ("DDERR_COLORKEYNOTSET");

        case DDERR_CURRENTLYNOTAVAIL:
            return ("DDERR_CURRENTLYNOTAVAIL");

        case DDERR_DCALREADYCREATED:
            return ("DDERR_DCALREADYCREATED");

        case DDERR_DIRECTDRAWALREADYCREATED:
            return ("DDERR_DIRECTDRAWALREADYCREATED");

        case DDERR_EXCEPTION:
            return ("DDERR_EXCEPTION");

        case DDERR_EXCLUSIVEMODEALREADYSET:
            return ("DDERR_EXCLUSIVEMODEALREADYSET");

        case DDERR_GENERIC:
            return ("DDERR_GENERIC");

        case DDERR_HEIGHTALIGN:
            return ("DDERR_HEIGHTALIGN");

        case DDERR_HWNDALREADYSET:
            return ("DDERR_HWNDALREADYSET");

        case DDERR_HWNDSUBCLASSED:
            return ("DDERR_HWNDSUBCLASSED");

        case DDERR_IMPLICITLYCREATED:
            return ("DDERR_IMPLICITLYCREATED");

        case DDERR_INCOMPATIBLEPRIMARY:
            return ("DDERR_INCOMPATIBLEPRIMARY");

        case DDERR_INVALIDCAPS:
            return ("DDERR_INVALIDCAPS");

        case DDERR_INVALIDCLIPLIST:
            return ("DDERR_INVALIDCLIPLIST");

        case DDERR_INVALIDDIRECTDRAWGUID:
            return ("DDERR_INVALIDDIRECTDRAWGUID");

        case DDERR_INVALIDMODE:
            return ("DDERR_INVALIDMODE");

        case DDERR_INVALIDOBJECT:
            return ("DDERR_INVALIDOBJECT");

        case DDERR_INVALIDPARAMS:
            return ("DDERR_INVALIDPARAMS");

        case DDERR_INVALIDPIXELFORMAT:
            return ("DDERR_INVALIDPIXELFORMAT");

        case DDERR_INVALIDPOSITION:
            return ("DDERR_INVALIDPOSITION");

        case DDERR_INVALIDRECT:
            return ("DDERR_INVALIDRECT");

        case DDERR_INVALIDSURFACETYPE:
            return ("DDERR_INVALIDSURFACETYPE");

        case DDERR_LOCKEDSURFACES:
            return ("DDERR_LOCKEDSURFACES");

        case DDERR_NO3D:
            return ("DDERR_NO3D");

        case DDERR_NOALPHAHW:
            return ("DDERR_NOALPHAHW");

        case DDERR_NOBLTHW:
            return ("DDERR_NOBLTHW");

        case DDERR_NOCLIPLIST:
            return ("DDERR_NOCLIPLIST");

        case DDERR_NOCLIPPERATTACHED:
            return ("DDERR_NOCLIPPERATTACHED");

        case DDERR_NOCOLORCONVHW:
            return ("DDERR_NOCOLORCONVHW");

        case DDERR_NOCOLORKEY:
            return ("DDERR_NOCOLORKEY");

        case DDERR_NOCOLORKEYHW:
            return ("DDERR_NOCOLORKEYHW");

        case DDERR_NOCOOPERATIVELEVELSET:
            return ("DDERR_NOCOOPERATIVELEVELSET");

        case DDERR_NODC:
            return ("DDERR_NODC");

        case DDERR_NODDROPSHW:
            return ("DDERR_NODDROPSHW");

        case DDERR_NODIRECTDRAWHW:
            return ("DDERR_NODIRECTDRAWHW");

        case DDERR_NODIRECTDRAWSUPPORT:
            return ("DDERR_NODIRECTDRAWSUPPORT");

        case DDERR_NOEMULATION:
            return ("DDERR_NOEMULATION");

        case DDERR_NOEXCLUSIVEMODE:
            return ("DDERR_NOEXCLUSIVEMODE");

        case DDERR_NOFLIPHW:
            return ("DDERR_NOFLIPHW");

        case DDERR_NOGDI:
            return ("DDERR_NOGDI");

        case DDERR_NOHWND:
            return ("DDERR_NOHWND");

        case DDERR_NOMIPMAPHW:
            return ("DDERR_NOMIPMAPHW");

        case DDERR_NOMIRRORHW:
            return ("DDERR_NOMIRRORHW");

        case DDERR_NOOVERLAYDEST:
            return ("DDERR_NOOVERLAYDEST");

        case DDERR_NOOVERLAYHW:
            return ("DDERR_NOOVERLAYHW");

        case DDERR_NOPALETTEATTACHED:
            return ("DDERR_NOPALETTEATTACHED");

        case DDERR_NOPALETTEHW:
            return ("DDERR_NOPALETTEHW");

        case DDERR_NORASTEROPHW:
            return ("DDERR_NORASTEROPHW");

        case DDERR_NOROTATIONHW:
            return ("DDERR_NOROTATIONHW");

        case DDERR_NOSTRETCHHW:
            return ("DDERR_NOSTRETCHHW");

        case DDERR_NOT4BITCOLOR:
            return ("DDERR_NOT4BITCOLOR");

        case DDERR_NOT4BITCOLORINDEX:
            return ("DDERR_NOT4BITCOLORINDEX");

        case DDERR_NOT8BITCOLOR:
            return ("DDERR_NOT8BITCOLOR");

        case DDERR_NOTAOVERLAYSURFACE:
            return ("DDERR_NOTAOVERLAYSURFACE");

        case DDERR_NOTEXTUREHW:
            return ("DDERR_NOTEXTUREHW");

        case DDERR_NOTFLIPPABLE:
            return ("DDERR_NOTFLIPPABLE");

        case DDERR_NOTFOUND:
            return ("DDERR_NOTFOUND");

        case DDERR_NOTINITIALIZED:
            return ("DDERR_NOTINITIALIZED");

        case DDERR_NOTLOCKED:
            return ("DDERR_NOTLOCKED");

        case DDERR_NOTPAGELOCKED:
            return ("DDERR_NOTPAGELOCKED");

        case DDERR_NOTPALETTIZED:
            return ("DDERR_NOTPALETTIZED");

        case DDERR_NOVSYNCHW:
            return ("DDERR_NOVSYNCHW");

        case DDERR_NOZBUFFERHW:
            return ("DDERR_NOZBUFFERHW");

        case DDERR_NOZOVERLAYHW:
            return ("DDERR_NOZOVERLAYHW");

        case DDERR_OUTOFCAPS:
            return ("DDERR_OUTOFCAPS");

        case DDERR_OUTOFMEMORY:
            return ("DDERR_OUTOFMEMORY");

        case DDERR_OUTOFVIDEOMEMORY:
            return ("DDERR_OUTOFVIDEOMEMORY");

        case DDERR_OVERLAYCANTCLIP:
            return ("DDERR_OVERLAYCANTCLIP");

        case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
            return ("DDERR_OVERLAYCOLORKEYONLYONEACTIVE");

        case DDERR_OVERLAYNOTVISIBLE:
            return ("DDERR_OVERLAYNOTVISIBLE");

        case DDERR_PALETTEBUSY:
            return ("DDERR_PALETTEBUSY");

        case DDERR_PRIMARYSURFACEALREADYEXISTS:
            return ("DDERR_PRIMARYSURFACEALREADYEXISTS");

        case DDERR_REGIONTOOSMALL:
            return ("DDERR_REGIONTOOSMALL");

        case DDERR_SURFACEALREADYATTACHED:
            return ("DDERR_SURFACEALREADYATTACHED");

        case DDERR_SURFACEALREADYDEPENDENT:
            return ("DDERR_SURFACEALREADYDEPENDENT");

        case DDERR_SURFACEBUSY:
            return ("DDERR_SURFACEBUSY");

        case DDERR_SURFACEISOBSCURED:
            return ("DDERR_SURFACEISOBSCURED");

        case DDERR_SURFACELOST:
            return ("DDERR_SURFACELOST");

        case DDERR_SURFACENOTATTACHED:
            return ("DDERR_SURFACENOTATTACHED");

        case DDERR_TOOBIGHEIGHT:
            return ("DDERR_TOOBIGHEIGHT");

        case DDERR_TOOBIGSIZE:
            return ("DDERR_TOOBIGSIZE");

        case DDERR_TOOBIGWIDTH:
            return ("DDERR_TOOBIGWIDTH");

        case DDERR_UNSUPPORTED:
            return ("DDERR_UNSUPPORTED");

        case DDERR_UNSUPPORTEDFORMAT:
            return ("DDERR_UNSUPPORTEDFORMAT");

        case DDERR_UNSUPPORTEDMASK:
            return ("DDERR_UNSUPPORTEDMASK");

        case DDERR_UNSUPPORTEDMODE:
            return ("DDERR_UNSUPPORTEDMODE");

        case DDERR_VERTICALBLANKINPROGRESS:
            return ("DDERR_VERTICALBLANKINPROGRESS");

        case DDERR_WASSTILLDRAWING:
            return ("DDERR_WASSTILLDRAWING");

        case DDERR_WRONGMODE:
            return ("DDERR_WRONGMODE");

        case DDERR_XALIGN:
            return ("DDERR_XALIGN");

        default:
            return ("Unknown error code");
    } // switch
} // ErrorMsgDirectDraw
#endif
