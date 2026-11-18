/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *  C++ wrapper code for ATI 3D C Interface functions. Complete documentation  *
 *  for all function calls is contained in the 3D RAGE Windows95 and DOS	   *
 *  Programmers guide which can be found on the ATI SDK CD. 				   *
 *******************************************************************************/
#ifndef ATI3DCIF_EXTRA_H
#define ATI3DCIF_EXTRA_H
// -----------------------------------------------------------------------------

#include "Ati3dCIF.h"
#include "util.h"
#include <ddraw.h>

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // warning C4290: C++ Exception Specification ignored

// -----------------------------------------------------------------------------

typedef ExceptionTemplate<C3D_EC> C3D_Exception;

class DDSurface;
class DDTextureSurface;
class Ati3dContext;

// -----------------------------------------------------------------------------

// This is here until it can be added into the CIF library.
extern "C" C3D_EC ATI3DCIF_RenderPrimStripFromArray(C3D_VLIST vList, C3D_UINT32 u32NumVert);

// -----------------------------------------------------------------------------

#define C3D_ETF_UNKNOWN ((C3D_ETEXFMT)0)

// -----------------------------------------------------------------------------

class Ati3dTexture {
public:
	~Ati3dTexture(void);
	Ati3dTexture(IDirectDrawSurface& rTexture, void* pPalette = NULL, BOOL tiled = FALSE) /*  */;

	IDirectDrawSurface*	DDSurface(void) const { return m_pSurface; }

	static void Tile(void* pvDst, void* pvSrc, C3D_ETEXFMT eFmt, DWORD dwWidth, DWORD dwHeight) ;

protected:
private:
	IDirectDrawSurface*	m_pSurface;

	C3D_HTX				m_handle;
	C3D_HTXPAL			m_hPalette;

	Ati3dTexture(const Ati3dTexture&);	// not allowed
	Ati3dTexture& operator=(const Ati3dTexture&);	// not allowed

	friend class Ati3dContext;
	friend class ATI3DCIF;
};

// -----------------------------------------------------------------------------

class Ati3dContext {
public:
	~Ati3dContext(void);
	Ati3dContext(void) /*  */;

	void RenderEnd(void) ;
	void RenderBegin(void) ;
	void RenderSwitch(void) ;

	void RenderPrimList(C3D_VLIST vList, int nVertices) ;
	void RenderPrimListVa(int nVertices, ...) ;

	void RenderPrimStrip(C3D_VSTRIP vStrip, int nVertices) ;

	void RenderPrimStripFromArray(C3D_VLIST vStrip, int nVertices) ;
	void RenderPrimStripVa(int nVertices, ...) ;

	void RenderText(int x, int y, char* string, int fg, int bg);

	void PerspectiveOn(BOOL on) ;
	void SetFilter(C3D_ETEXFILTER mode) ;

	void SetSurface(void* pDst, int pixelsPerRow) ;
	void SetSurface(const IDirectDrawSurface& rSurface) ;

	void SetSolidColor(float r, float g, float b, float a = 255.0) ;
	void SetShadeMode(C3D_ESHADE mode) ;

	void SetZBuffer(void* pDst, int pixelsPerRow) ;
	void SetZBuffer(const IDirectDrawSurface& rSurface) ;
	void SetZMode(C3D_EZMODE mode, C3D_EZCMP cmp) ;
	void SetTexture(Ati3dTexture* pTexture) ;
	void SetTextureMode(BOOL on) ;
	void SetLightingMode(C3D_ETLIGHT lighting) ;

	void SetCompositeTexture(Ati3dTexture* pTexture) ;
	void SetCompositeFactor(uint32 factor) ;
	void SetCompositeFilter(C3D_ETEXFILTER mode) ;
	void SetCompositeFunction(C3D_ETEXCOMPFCN fn) ;

	void SetAlphaSrc(C3D_EASRC mode) ;
	void SetAlphaDst(C3D_EADST mode) ;
	void SetTextureOp(C3D_ETEXOP op) ;

	void SetFogColor(C3D_COLOR& color) ;
	void SetFog(BOOL on) ;

	void SetPrimType(C3D_EPRIM type) ;

	C3D_HRC GetHRC(void) const { return m_hContext; }

protected:
private:
	C3D_HRC	m_hContext;
	void*	m_pDst;
	int		m_pixelsPerRow;
	int		m_width;
	int		m_height;

	Ati3dContext(const Ati3dContext& copy);				// Not allowed
	Ati3dContext& operator=(const Ati3dContext& copy);	// Not allowed

	void RenderChar(int x, int y, char c, int fg, int bg);
	void RenderPixel(int x, int y, WORD color);
};

// -----------------------------------------------------------------------------

class ATI3DCIF {
public:
	static void End(void) ;
	static void Begin(void) ;

	static void GetInfo(C3D_3DCIFINFO* pInfo) ;

	static void ASSERT_INITIALIZED(void) { ASSERT(m_initialized); }
	static void ASSERT_CONTEXT_IS(Ati3dContext* pContext) { ASSERT(m_pCurrentContext == pContext); }

	static const char* ErrString(C3D_EC ec);

	static void ErrorCleanup(void);

	static C3D_ETEXFMT TexelFormat(DDPIXELFORMAT& rFmt);

	static BOOL m_reallyDraw;

	static Ati3dTexture* LoadTexture(IDirectDraw& rDD, char* pFilename, C3D_ETEXFMT texFmtOverride = C3D_ETF_UNKNOWN) ;

	static void Recording(BOOL on);
	static void Playback(void);

protected:
private:
	static BOOL				m_initialized;
	static Ati3dContext*	m_pCurrentContext;

	static void SetContext(Ati3dContext* p) { m_pCurrentContext = p; }
	static Ati3dContext* GetContext(void) { return m_pCurrentContext; }

	friend void Ati3dContext::RenderEnd(void) ;
	friend void Ati3dContext::RenderBegin(void) ;
	friend void Ati3dContext::RenderSwitch(void) ;

	~ATI3DCIF(void);			// Not allowed
	ATI3DCIF(void);				// Not allowed
};

// -----------------------------------------------------------------------------

#define THROW_C3D_EXCEPTION(r) throw C3D_Exception("ATI3DCIF", __FILE__, __LINE__, ATI3DCIF::ErrString(r), r)

// -----------------------------------------------------------------------------
#endif
