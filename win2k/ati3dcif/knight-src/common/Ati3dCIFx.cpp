/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  C++ wrapper code for ATI 3D C Interface functions. Complete documentation  *
 *  for all function calls is contained in the 3D RAGE Windows95 and DOS	   *
 *  Programmers guide which can be found on the ATI SDK CD. 				   *
 *******************************************************************************/
#include "stdwin.h"
#include <stdarg.h>
#include <math.h>
#include <ddraw.h>

#include "Ati3dCIF.h"
#include "Ati3dCIFx.h"
#include "DirectDraw.h"
#include "image.h"
#include "watchers.h"

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // warning C4290: C++ Exception Specification ignored

// -----------------------------------------------------------------------------

BOOL			ATI3DCIF::m_initialized = FALSE;
Ati3dContext*	ATI3DCIF::m_pCurrentContext = NULL;

//allows code to be profiled without rasterizing (good for seeing what 
//code overhead is up to the point of interaction with the driver)
BOOL ATI3DCIF::m_reallyDraw = TRUE;

BOOL	g_doTextureAddrTiling;

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Render a primitive strip from a list of vertices. This is here until it can*
 *	be added into the ATI3DCIF library.										   *
 *******************************************************************************/
extern "C" {
	
C3D_EC ATI3DCIF_RenderPrimStripFromArray(C3D_VLIST vList, C3D_UINT32 u32NumVert)
{
	C3D_VTCF* pList = new C3D_VTCF[u32NumVert];
	if(!pList)
		return C3D_EC_MEMALLOCFAIL;

	for(int i=0; i<(int)u32NumVert; i++)
		memcpy(&pList[i], vList[i], sizeof(C3D_VTCF));

	C3D_EC result = ATI3DCIF_RenderPrimStrip(pList, u32NumVert);

	delete pList;

	return result;
}

} // extern "C"

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Unloads a loaded ATI3DCIF module and free system resources.				   *	
 *******************************************************************************/
void ATI3DCIF::End(void)
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT_CONTEXT_IS(NULL);
	C3D_EC result = ATI3DCIF_Term();
}

/*******************************************************************************
 *	Load and initialize the ATI3DCIF module; this must be done				   *
 *	by the application prior to any other ATI3DCIF calls.					   *
 *******************************************************************************/
void ATI3DCIF::Begin(void)
{
	ASSERT(!m_initialized);

	C3D_EC result = ATI3DCIF_Init();
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);

	char buf[128];
	GetProfileString("AtiDemo", "TexAddrTiling", "TRUE", buf, 256);
	if(!stricmp(buf, "TRUE")) g_doTextureAddrTiling = TRUE;
	if(!stricmp(buf, "FALSE")) g_doTextureAddrTiling = FALSE;

	m_initialized = TRUE;
}

// -----------------------------------------------------------------------------

/*******************************************************************************
 * Return information about CIF caps and versions                              *
 *******************************************************************************/
void ATI3DCIF::GetInfo(C3D_3DCIFINFO* pInfo) 
{
	ASSERT(m_initialized);
	ASSERT(pInfo);

	C3D_EC result = ATI3DCIF_GetInfo(pInfo);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Return a human-readable text-string if an error is generated.			   *
 *******************************************************************************/
const char* ATI3DCIF::ErrString(C3D_EC ec)
{
	switch(ec)
	{
#define REGISTER_ERROR(e) case e : return #e
    REGISTER_ERROR(C3D_EC_OK);
    REGISTER_ERROR(C3D_EC_GENFAIL);
    REGISTER_ERROR(C3D_EC_MEMALLOCFAIL);
    REGISTER_ERROR(C3D_EC_BADPARAM);
    REGISTER_ERROR(C3D_EC_UNUSED0);
    REGISTER_ERROR(C3D_EC_BADSTATE);
    REGISTER_ERROR(C3D_EC_NOTIMPYET);
    REGISTER_ERROR(C3D_EC_UNUSED1);
    REGISTER_ERROR(C3D_EC_CHIPCAPABILITY);
#undef REGISTER_ERROR
	default:
		return "ATI3DCIF::ErrString(): Unknown Error Number";
	}
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Destroys a previously created rendering context; this must be done before  *
 *	unloading the ATI3DCIF module (ATI3DCIF_Term)							   *
 *******************************************************************************/
Ati3dContext::~Ati3dContext(void)
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	ATI3DCIF_ContextDestroy(m_hContext);
	m_hContext = NULL;
}


/*******************************************************************************
 *	Creates a uniquely-specified rendering context; all ATI3DCIF rendering 	   *
 *	calls must have a context to render into								   *
 *******************************************************************************/
Ati3dContext::Ati3dContext(void)
{
	m_hContext = NULL;

	ATI3DCIF::ASSERT_INITIALIZED();

	m_hContext = ATI3DCIF_ContextCreate();
	if(!m_hContext)
		THROW_C3D_EXCEPTION(C3D_EC_GENFAIL);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Terminate 3D hardware rendering operations and allow 2D operations. Should *
 *	only be called while while hardware in enabled for 3D rendering operations *
 *	(i.e. after a call to RenderBegin).										   *
 *******************************************************************************/
void Ati3dContext::RenderEnd(void) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ATI3DCIF::ASSERT_CONTEXT_IS(this);

	ATI3DCIF::SetContext(NULL);

	VERIFY(ATI3DCIF_RenderEnd() == C3D_EC_OK);
}


/*******************************************************************************
 *	Enable 3D hardware drawing operations for a given rendering context;	   *
 *	typically called at the beginning of each frame update. 2D operations (such*
 *	as Blts) should not be called from within a RenderBegin/RenderEnd pair.    *
 *******************************************************************************/
void Ati3dContext::RenderBegin(void) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ATI3DCIF::ASSERT_CONTEXT_IS(NULL);

	C3D_EC result = ATI3DCIF_RenderBegin(m_hContext);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);

	ATI3DCIF::SetContext(this);
}


/*******************************************************************************
 *	Switch rendering contexts in which 3D hardware drawing operations occur.   *
 *	This function currently disables 3D rendering, then re-enables it for the  *
 *	existing rendering context only. Included for completeness.				   *
 *******************************************************************************/
void Ati3dContext::RenderSwitch(void) 
{
	ATI3DCIF::ASSERT_INITIALIZED();

	Ati3dContext* pOldContext = ATI3DCIF::GetContext();
	if(pOldContext == this)
		return;

	pOldContext->RenderEnd();

	RenderBegin();
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	For a primitive strip, the first n vertices in the list are				   *
 *	used to render a primitive, then each additional vertex in the list		   *
 *	can be used to render an additional primitive. 							   *
 *	A primitive list requires n vertices to render each 					   *
 *	primitive. 																   *
 *	n is the total number of vertices in a primitive (three 				   *
 *	for a triangle, 4 for a quad or rect, etc.)								   *
 *	A strip is more efficient to render, but may be more complex to set		   *
 *	up as an object description												   *
 *******************************************************************************/

/*******************************************************************************
 *	Draw a primitive list using a list of vertices (vList). nVertices specifies* 
 *	the number of vertices in the list. The primitive type that is rendered is * 
 *	based on the current rendering context; the default is triangle, but may be* 
 *	changed by a call to ContextSetState with the desired enumeration constant.*
 *******************************************************************************/
void Ati3dContext::RenderPrimList(C3D_VLIST vList, int nVertices) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ATI3DCIF::ASSERT_CONTEXT_IS(this);

	if(!ATI3DCIF::m_reallyDraw) return;

	C3D_EC result = ATI3DCIF_RenderPrimList(vList, nVertices);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);
}


/*******************************************************************************
 *	Render a primitive list from a list of vertices. Vertices can be passed    *
 *	in directly, without the need for an array.							       *
 *******************************************************************************/
void Ati3dContext::RenderPrimListVa(int nVertices, ...) 
{
	va_list alist;
	va_start(alist, nVertices);
	RenderPrimList((C3D_VLIST)alist, nVertices);	// UGLY! But it works...
	va_end(alist);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Draw a primitive strip using a list of vertices (vStrip). nVertices 	   *
 *	specifies the number of vertices in the list. The primitive type that is   *
 *	rendered is based on the current rendering context; the default is 		   *
 *	triangle, but may be changed by a call to ContextSetState with the 		   *
 *	desired enumeration constant.											   *
 *******************************************************************************/
void Ati3dContext::RenderPrimStrip(C3D_VSTRIP vStrip, int nVertices) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ATI3DCIF::ASSERT_CONTEXT_IS(this);

	if(!ATI3DCIF::m_reallyDraw) return;

	C3D_EC result = ATI3DCIF_RenderPrimStrip(vStrip, nVertices);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);
}


/*******************************************************************************
 *	Render a primitive strip from a list of vertices.						   *
 *******************************************************************************/
void Ati3dContext::RenderPrimStripFromArray(C3D_VLIST vStrip, int nVertices) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ATI3DCIF::ASSERT_CONTEXT_IS(this);

	if(!ATI3DCIF::m_reallyDraw) return;

	C3D_EC result = ATI3DCIF_RenderPrimStripFromArray(vStrip, nVertices);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);
}


/*******************************************************************************
 *	Render a primitive strip from a list of vertices. Vertices can be passed   *
 *	in directly, without the need for an array.								   *
 *******************************************************************************/
void Ati3dContext::RenderPrimStripVa(int nVertices, ...) 
{
	va_list alist;
	va_start(alist, nVertices);
	RenderPrimStripFromArray((C3D_VLIST)alist, nVertices); // UGLY! But it works...
	va_end(alist);
}



// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the draw surface characteristics including surface pitch and surface   *
 *	address. Since this is a state change, it requires a ContextSetState.  	   *
 *******************************************************************************/
void Ati3dContext::SetSurface(void* pDst, int pixelsPerRow) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SURF_DRAW_PTR, (C3D_PRSDATA)&pDst);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);

	m_pDst = pDst;

	result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SURF_DRAW_PITCH, (C3D_PRSDATA)&pixelsPerRow);
	if(result != C3D_EC_OK)
		THROW_C3D_EXCEPTION(result);

	m_pixelsPerRow = pixelsPerRow;

	m_width = pixelsPerRow;
	m_height = 480;
}


/*******************************************************************************
 *	Change the drawing surface characteristics using a new surface description,* 
 *	which includes height,width and pixel format. The call to SetSurface will  *
 *	perform the ContextSetState that is required in order to change the state  *
 *	of the drawing surface.													   *
 *******************************************************************************/
void Ati3dContext::SetSurface(const IDirectDrawSurface& rSurface) 
{
	DDSurfaceDesc desc;

	LockSurface(desc, (IDirectDrawSurface&)rSurface);
	UnlockSurface((IDirectDrawSurface&)rSurface, desc.lpSurface);

	SetSurface(desc.lpSurface, desc.lPitch * 8 / desc.ddpfPixelFormat.dwRGBBitCount);
	m_width = desc.dwWidth;
	m_height = desc.dwHeight;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the color to be used for all solid-color rendering operations. Since   *
 *	this is a state change, it requires a ContextSetState.					   *
 *******************************************************************************/
void Ati3dContext::SetSolidColor(float r, float g, float b, float a) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_COLOR color;
	color.r = r;
	color.g = g;
	color.b = b;
	color.a = a;
	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SOLID_CLR, (C3D_PRSDATA)&color);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the level of perspective correction to be applied to a texture map. If *
 *	enabled (on=TRUE), the correction level will be the highest available. If  *
 *	disabled (on=FALSE), no perspective correction will be performed.	 Since *
 *	this is a state change, it requires a ContextSetState.					   *
 *******************************************************************************/
void Ati3dContext::PerspectiveOn(BOOL on) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_ETPERSPCOR mode = on?C3D_ETPC_NINE:C3D_ETPC_NONE;
	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_PERSP_COR, (C3D_PRSDATA)&mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}
// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the type of filtering that will be applied to a texture. Various 	   *
 *	combinations of Pick Nearest, Bi-Linear and Tri-Linear are possible.	   *
 *	Since this is a state change, it requires a ContextSetState.			   *
 *******************************************************************************/
void Ati3dContext::SetFilter(C3D_ETEXFILTER mode) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_FILTER, (C3D_PRSDATA)&mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 * 	Set the type of shading that will be used for all shading operations. None,*	 
 *	Solid, Flat and Smooth (Gouraud) are supported. Solid shading uses the 	   *
 *	solid color set for the rendering context to color the primitive with, Flat* 
 *	shading	uses the color of the last vertex in the current primitive to color* 
 *	the	primitive with. Smooth shading interpolates linearly between the 	   *
 *	intensities at each vertex and so applies a smooth gradation across the    *
 *	entire primitive. 														   *
 *	Since this is a state change, it requires a ContextSetState.			   *	 
 *******************************************************************************/
void Ati3dContext::SetShadeMode(C3D_ESHADE mode) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SHADE_MODE, (C3D_PRSDATA)&mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the type of lighting that gets applied to a texture. None, Modulate and* 
 *	Alpha Decal are supported. Modulate applies the current shading state to   *
 *	each texel in the texture. Alpha Decal combines the current shading state  *
 *	with the alpha value in each texel.										   *
 *******************************************************************************/
void Ati3dContext::SetLightingMode(C3D_ETLIGHT lighting) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_LIGHT, (C3D_PRSDATA)&lighting);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the Z buffer surface address and surface pitch.						   *
 *	Since this is a state change, it requires a ContextSetState.  			   *
 *******************************************************************************/
void Ati3dContext::SetZBuffer(void* pDst, int pixelsPerRow) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SURF_Z_PTR, (C3D_PRSDATA)&pDst);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

	result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_SURF_Z_PITCH, (C3D_PRSDATA)&pixelsPerRow);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Set the Z buffer surface characteristics using a surface description. 	   *
 *	The call to SetZBuffer will perform the ContextSetState that is required in* 
 *	order to change the state of the drawing surface.						   *
 *******************************************************************************/
void Ati3dContext::SetZBuffer(const IDirectDrawSurface& rSurface) 
{
	DDSurfaceDesc desc;

	HRESULT result = ((IDirectDrawSurface&)rSurface).Lock(NULL, &desc, DDLOCK_WAIT, NULL);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);

	((IDirectDrawSurface&)rSurface).Unlock(desc.lpSurface);

	SetZBuffer(desc.lpSurface, desc.lPitch * 8 / desc.ddpfPixelFormat.dwRGBBitCount);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set Z buffer mode. Choices are no Z-buffering, test but do not update, and *
 *	test and update. This function also sets the compare mode, which performs  *
 *	a logical operation to select or reject a pixel for rendering.			   *
 *	Since this is a state change, it requires a ContextSetState.  			   *
 *******************************************************************************/
void Ati3dContext::SetZMode(C3D_EZMODE mode, C3D_EZCMP cmp) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_Z_MODE, &mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

	result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_Z_CMP_FNC, &cmp);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set source alpha blending factor. Various combinations of the alpha 	   *
 *	component and R, G, B components can be used.							   *
 *	Since this is a state change, it requires a ContextSetState.  			   *
 *******************************************************************************/
void Ati3dContext::SetAlphaSrc(C3D_EASRC mode) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_ALPHA_SRC, &mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}


/*******************************************************************************
 *	Set destination alpha blending factor. Various combinations of the alpha   *
 *	component and R, G, B components can be used.							   *
 *	Since this is a state change, it requires a ContextSetState.  			   *
 *******************************************************************************/
void Ati3dContext::SetAlphaDst(C3D_EADST mode) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_ALPHA_DST, &mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Set the type of texel rendering operation that should occur during 		   *
 *	texture mapping. Since this is a state change, it requires a 			   *
 *	ContextSetState.    			   										   *
 *******************************************************************************/
void Ati3dContext::SetTextureOp(C3D_ETEXOP op) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_TEXOP, &op);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set the primitive type that will be rendered during during primitive strip *
 *	and list draws. Point, line, triangle, quad and rectangle primitives are   *
 *	supported. Since this is a state change, it requires a ContextSetState.    *			   
 *******************************************************************************/
void Ati3dContext::SetPrimType(C3D_EPRIM type) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_PRIM_TYPE, &type);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Enable or disable texture mapping. Since this is a state change, it 	   *
 *	requires a ContextSetState.    											   *
 *******************************************************************************/
void Ati3dContext::SetTextureMode(BOOL on) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_EN, &on);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Select a particular texture to be used during rendering. This function 	   *
 *	selects a texture based on a valid texture handle. The texture handle is   *
 *	returned when a structure containing the texture information is registered *
 *	through TextureReg.														   *
 *	Since this is a state change, it requires a ContextSetState.    		   *
 *******************************************************************************/
void Ati3dContext::SetTexture(Ati3dTexture* pTexture) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_HTX handle = NULL;
	if(pTexture)
		handle = pTexture->m_handle;

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_TMAP_SELECT, &handle);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Select a particular texture to be used as the second texture for a 		   *
 *	compositing operation during rendering. This function selects a texture	   *
 *	based on a valid texture handle. The texture handle is returned when a     *
 *	structure containing the texture information is registered through 		   *
 *	TextureReg.	Since this is a state change, it requires a ContextSetState.   * 		   
 *	Since it is assumed that a compositing texture will only be chosen if 	   *
 *	texture compositing is desired, this function also enables compositing.	   *
 *	Note that texture mapping must be enabled for this call to succeed.		   *
 *	Composite enabling is also a state change and requires a ContextSetState.  *
 *******************************************************************************/
void Ati3dContext::SetCompositeTexture(Ati3dTexture* pTexture) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_HTX handle = NULL;
	if(pTexture)
		handle = pTexture->m_handle;

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_COMPOSITE_SELECT, &handle);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

	BOOL on = handle?TRUE:FALSE;
	result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_COMPOSITE_EN, &on);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}


/*******************************************************************************
 *	Specify the Blend Factor (range: 0-15) to be used during compositing 	   *
 *	operations. The Blend Factor controls how much of each texture appears in  *
 *	the final blended image. Changing this factor over time allows dissolves   *
 *	of one texture into another. 											   *
 *	Since this is a state change, it requires a ContextSetState.			   *
 *******************************************************************************/
void Ati3dContext::SetCompositeFactor(uint32 factor) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);
	ASSERT(factor <= 16);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_COMPOSITE_FACTOR, &factor);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Set the type of composite filtering. Supported filtering modes 			   *
 *  include various combinations of Pick Nearest, Bi-Linear and Tri-Linear.	   *
 *	Since this is a state change, it requires a ContextSetState.			   *
 *******************************************************************************/
void Ati3dContext::SetCompositeFilter(C3D_ETEXFILTER mode) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_COMPOSITE_FILTER, (C3D_PRSDATA)&mode);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Set the type of compositing function. Supported functions are Blend and	   *
 *	Modulate. Blend combines two textures according to a Blend Factor which	   *
 *	may be set through SetCompositeFactor. Modulate involves a simple 		   *
 *	multiplication of the primary and secondary texels; there is no blend	   *
 *	factor. Since this is a state change, it requires a ContextSetState.	   *
 *******************************************************************************/
void Ati3dContext::SetCompositeFunction(C3D_ETEXCOMPFCN fn) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_COMPOSITE_FNC, (C3D_PRSDATA)&fn);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Unregister a texture that has been previously registered through 		   *
 *	TextureReg. If the texture is paletted, the palette is deallocated here.   *
 *	This must be done after the texture is unregistered, but before the 	   *
 *	ATI3DCIF module is unloaded. 											   *
 *******************************************************************************/
Ati3dTexture::~Ati3dTexture(void)
{
	ASSERT(m_handle);
	ATI3DCIF_TextureUnreg(m_handle);
	m_handle = NULL;

	if(m_hPalette)
	{
		ATI3DCIF_TexturePaletteDestroy(m_hPalette);
		m_hPalette = NULL;
	}
}

/*******************************************************************************
 *	Fill out a texture map structure description for use by LoadTexture. 	   *
 *	Surfaces are always set up as MIPmaps; the application may choose to use   *
 *	only one level when rendering. Palette and MIP fields are filled out	   *
 *	and the texture map structure is registered.							   *
 *******************************************************************************/
Ati3dTexture::Ati3dTexture(IDirectDrawSurface& rTexture, void* pPalette, BOOL tiled) /*  */
{
	ATI3DCIF::ASSERT_INITIALIZED();

	int                 mipLevels=1;
	IDirectDrawSurface* pMips[10];

	pMips[mipLevels-1] = &rTexture;

	//determine number of MIPlevels
	while(1)
	{
		DDSCAPS caps;
		caps.dwCaps = DDSCAPS_MIPMAP;
		HRESULT result = pMips[mipLevels-1]->GetAttachedSurface(&caps, &pMips[mipLevels]);
		if(result == DD_OK)
			mipLevels++;
		else
			break;
	}

	m_pSurface = &rTexture;
	m_hPalette = NULL;

	C3D_TMAP tmap;
	memset(&tmap, 0, sizeof(tmap));
	tmap.u32Size = sizeof(tmap);

	tmap.htxpalTexPalette = NULL;
	tmap.bMipMap = (mipLevels > 1)?TRUE:FALSE;
	tmap.eTexTiling = tiled?C3D_ETEXTILE_ON:C3D_ETEXTILE_DEFAULT;

	for(int i=0; i<mipLevels; i++)
	{
		DDSurfaceDesc desc;
		LockSurface(desc, *pMips[i]);
		UnlockSurface(*pMips[i], desc.lpSurface);

		if(i==0)
		{
			//fill out shift value fields for MIP level access 
			//and texel format field in texture map structure
			tmap.u32MaxMapXSizeLg2 = log((double)desc.dwWidth) / log(2.0);
			tmap.u32MaxMapYSizeLg2 = log((double)desc.dwHeight) / log(2.0);
			tmap.eTexFormat = ATI3DCIF::TexelFormat(desc.ddpfPixelFormat);

			if(tmap.eTexFormat == C3D_ETF_VQ)
			{
				// CIF needs to know the size in post-palette texels
				tmap.u32MaxMapXSizeLg2++;
				tmap.u32MaxMapYSizeLg2++;
			}

			//if paletted format, load palette array, mark each entry so that
			//it will replacing the physical palette entry with the specified 
			//color when the palette gets loaded.
			if((tmap.eTexFormat == C3D_ETF_CI8) && pPalette)
			{
				static C3D_PALETTENTRY pal[256];
				for(int entry=0; entry<256; entry++)
				{
					pal[entry].r = ((BYTE*)pPalette)[entry*3+0];
					pal[entry].g = ((BYTE*)pPalette)[entry*3+1];
					pal[entry].b = ((BYTE*)pPalette)[entry*3+2];
					pal[entry].flags = C3D_LOAD_PALETTE_ENTRY;
				}

				//use newly created palette array to create a logical texture palette
				//within ATI3DCIF
				C3D_EC result = ATI3DCIF_TexturePaletteCreate(C3D_ECI_TMAP_8BIT, pal, &m_hPalette);
				if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

				tmap.htxpalTexPalette = m_hPalette;
			}

			if((tmap.eTexFormat == C3D_ETF_VQ) && pPalette)
			{
				//use newly created palette array to create a logical texture palette
				//within ATI3DCIF
				C3D_EC result = ATI3DCIF_TexturePaletteCreate(C3D_ECI_TMAP_VQ, pPalette, &m_hPalette);
				if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

				tmap.htxpalTexPalette = m_hPalette;
			}
		}
		else
			pMips[i]->Release();
		tmap.apvLevels[i] = desc.lpSurface;
	}

	for(; i<=10; i++) tmap.apvLevels[i] = tmap.apvLevels[i-1];

	m_handle = NULL;
	C3D_EC result = ATI3DCIF_TextureReg(&tmap, &m_handle);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);

	rTexture.AddRef();
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Reset the hardware to a 2D state if an error has occured.				   *
 *******************************************************************************/
void ATI3DCIF::ErrorCleanup(void)
{
	if(m_pCurrentContext)
		m_pCurrentContext->RenderEnd();
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Return the enumeration constant representing the current texture format.   *		
 *******************************************************************************/
C3D_ETEXFMT ATI3DCIF::TexelFormat(DDPIXELFORMAT& rFmt)
{
	pixelType pixfmt = ((DDPixelFormat&)rFmt);
	switch(pixfmt)
	{
	case PIXELTYPE_8: return C3D_ETF_CI8;
	case PIXELTYPE_332: return C3D_ETF_RGB332;
	case PIXELTYPE_1555: return C3D_ETF_RGB1555;
	case PIXELTYPE_565: return C3D_ETF_RGB565;
	case PIXELTYPE_4444: return C3D_ETF_RGB4444;
	case PIXELTYPE_8888: return C3D_ETF_RGB8888;
	case PIXELTYPE_VQ: return C3D_ETF_VQ;
	default: THROW_C3D_EXCEPTION(C3D_EC_NOTIMPYET); return C3D_ETF_UNKNOWN;
	}
}

// -----------------------------------------------------------------------------

/*******************************************************************************
 *	Handle texture creation. Appropriate surface (multiple if MIPmapped) is    *
 *	created and loaded with the image data in the requested format. If the 	   *
 *	texel format is UNKNOWN, the format will be decided based on what the	   *
 *	image format is. When this function returns, the texture will have been	   *
 *	registered and will be ready to use in a rendering context.				   *
 *******************************************************************************/
Ati3dTexture* ATI3DCIF::LoadTexture(IDirectDraw& rDD, char* pFilename, C3D_ETEXFMT texFmtOverride) 
{
	pixelType pixfmt;
	switch(texFmtOverride)
	{
	case C3D_ETF_UNKNOWN: pixfmt = PIXELTYPE_UNKNOWN; break;
	case C3D_ETF_RGB1555: pixfmt = PIXELTYPE_1555; break;
	case C3D_ETF_RGB565: pixfmt = PIXELTYPE_565; break;
	case C3D_ETF_RGB8888: pixfmt = PIXELTYPE_8888; break;
	case C3D_ETF_RGB332: pixfmt = PIXELTYPE_332; break;
	case C3D_ETF_RGB4444: pixfmt = PIXELTYPE_4444; break;
	case C3D_ETF_CI8: pixfmt = PIXELTYPE_8; break;
	case C3D_ETF_VQ: pixfmt = PIXELTYPE_VQ; break;
	default:
		THROW_C3D_EXCEPTION(C3D_EC_NOTIMPYET);
	}

	//Surface creation and image loading
	IDirectDrawSurface* pSurface = LoadSurface(rDD, pFilename, TRUE, pixfmt);

	DDSurfaceDesc desc;
	LockSurface(desc, *pSurface);

	union {
		uint8		ci8[256][3];
		VqtCodebook	vq;
	} palette;
	if((pixelType)((DDPixelFormat&)desc.ddpfPixelFormat) == PIXELTYPE_8)
		ImageReadPalette(palette.ci8, pFilename);
	else if((pixelType)((DDPixelFormat&)desc.ddpfPixelFormat) == PIXELTYPE_VQ)
		VqtReadCodebook(&palette.vq, pFilename);

	BOOL tiled = FALSE;

	// Tile the texture if necessary
	if(g_doTextureAddrTiling)
	{
		try
		{
			DWORD size = desc.lPitch * desc.dwHeight;
			void* pTmp = new char[size];
			DECLARE_POINTER_DOER(void*, pTmp, pTmp);

			memcpy(pTmp, desc.lpSurface, size);
//			memset(desc.lpSurface, 0, size);

			Ati3dTexture::Tile(desc.lpSurface, pTmp, TexelFormat(desc.ddpfPixelFormat), desc.dwWidth, desc.dwHeight);

			tiled = TRUE;
		}
		catch(...)
		{
		}
	}

	UnlockSurface(*pSurface, desc.lpSurface);

	//MIPmap the texture if required, palette (if indexed) and register it
	Ati3dTexture* pTexture = new Ati3dTexture(*pSurface, (void*)&palette, tiled);
	pSurface->Release();

	return pTexture;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Set up the color to be used for fog. Since this is a state change, it 	   *
 *	requires a ContextSetState.    		   									   *
 *******************************************************************************/
void Ati3dContext::SetFogColor(C3D_COLOR& color) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_FG_CLR, &color);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

/*******************************************************************************
 *	Enable or disable fog. Since this is a state change, it requires		   *
 *	a ContextSetState.    											   		   *
 *******************************************************************************/
void Ati3dContext::SetFog(BOOL on) 
{
	ATI3DCIF::ASSERT_INITIALIZED();
	ASSERT(m_hContext);

	C3D_EC result = ATI3DCIF_ContextSetState(m_hContext, C3D_ERS_FOG_EN, &on);
	if(result != C3D_EC_OK) THROW_C3D_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
