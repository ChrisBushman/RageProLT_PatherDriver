/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * glob.h - Header file for global variables in GLOB.CPP.                     *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#include <windows.h>
#include <ddraw.h>
#include "ati3dcif.h"

extern LPDIRECTDRAW             glpDD;          // DirectDraw object
extern LPDIRECTDRAWSURFACE      glpDDSPrimary;  // DirectDraw primary surface
extern LPDIRECTDRAWSURFACE      glpDDSBack;     // DirectDraw back surface
extern LPDIRECTDRAWSURFACE      glpDDSOne;      // DirectDraw plain surface
extern LPDIRECTDRAWSURFACE      glpDDSZBuffer;  // DirectDraw z-buffer surface
extern C3D_HRC                  ghRC;           // ATI3DCIF rendering context
extern RECT                     grctBkgd;       // Background bitmap
extern BOOL                     gbCIFInit;      // ATI3DCIF init state

extern char                     gszErrMsg[64];  // Global error message text
