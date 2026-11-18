/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	C++ wrappers for the standard DirectDraw calls							   *
 *******************************************************************************/
#ifndef ATI_DIRECTDRAW_H
#define ATI_DIRECTDRAW_H
// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#include <ddraw.h>
#include "util.h"

// -----------------------------------------------------------------------------

typedef ExceptionTemplate<HRESULT> DD_Exception;

// -----------------------------------------------------------------------------

enum pixelType {
	PIXELTYPE_UNKNOWN,
	PIXELTYPE_4,
	PIXELTYPE_8,
	PIXELTYPE_332,
	PIXELTYPE_1555,
	PIXELTYPE_565,
	PIXELTYPE_4444,
	PIXELTYPE_8888,
	PIXELTYPE_Z16,
	PIXELTYPE_Z24,
	PIXELTYPE_Z32,
	PIXELTYPE_VQ
};

// -----------------------------------------------------------------------------

class DirectDraw {
public:
	static const char* ErrString(HRESULT err);

protected:
private:
	~DirectDraw(void);		// not allowed
	DirectDraw(void);		// not allowed
};

// -----------------------------------------------------------------------------

class DDPixelFormat : public DDPIXELFORMAT {
public:
	DDPixelFormat(void) { memset(this, 0, sizeof(*this)); dwSize = sizeof(*this); }
	DDPixelFormat(pixelType pixfmt);

	operator DDPIXELFORMAT(void) { return *this; }
	operator pixelType(void) ;

protected:
private:
};

// -----------------------------------------------------------------------------

class DDSurfaceDesc : public DDSURFACEDESC {
public:
	DDSurfaceDesc(void) { memset(this, 0, sizeof(*this)); dwSize = sizeof(*this); }

	operator DDSURFACEDESC(void) { return *this; }

protected:
private:
};

// -----------------------------------------------------------------------------

extern DWORD RGBColor(DDPIXELFORMAT& pPixelFormat, BYTE red, BYTE green, BYTE blue, BYTE alpha);
extern DWORD DDAgp(DWORD flags);
extern DWORD DDAgpUser(void);

// -----------------------------------------------------------------------------

extern void LockSurface(DDSURFACEDESC& pDesc, IDirectDrawSurface& rSurface) ;
extern void UnlockSurface(IDirectDrawSurface& rSurface, void* lpSurface) ;
extern IDirectDrawSurface* LoadSurface(IDirectDraw& rDD, char* pFilename, BOOL bTexture, pixelType texFmtOverride = PIXELTYPE_UNKNOWN) ;

// -----------------------------------------------------------------------------

#define THROW_DD_EXCEPTION(r) throw DD_Exception("Direct Draw", __FILE__, __LINE__, DirectDraw::ErrString(r), r)

// -----------------------------------------------------------------------------
#endif // ATI_DIRECTDRAW_H
