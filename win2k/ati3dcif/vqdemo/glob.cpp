/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * glob.cpp - This module contains the global variables.                      *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#include <windows.h>
#include <ddraw.h>
#include "ati3dcif.h"

LPDIRECTDRAW        glpDD = NULL;           // DirectDraw object
LPDIRECTDRAWSURFACE glpDDSPrimary = NULL;   // DirectDraw primary surface
LPDIRECTDRAWSURFACE glpDDSBack = NULL;      // DirectDraw back surface
LPDIRECTDRAWSURFACE glpDDSOne = NULL;       // DirectDraw plain surface
LPDIRECTDRAWSURFACE glpDDSZBuffer = NULL;   // DirectDraw z-buffer surface
C3D_HRC             ghRC = NULL;            // ATI3DCIF rendering context
RECT                grctBkgd = {0};         // Background bitmap
BOOL                gbCIFInit = FALSE;      // ATI3DCIF init state

char                gszErrMsg[64];          // Global error message text
