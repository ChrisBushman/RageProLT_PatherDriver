/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	C++ wrappers for the standard DirectDraw calls							   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include <stdlib.h>	// malloc/free for the Win2000 texture bypass. CWB
#include <string.h>	// memset/memcpy
#include "DirectDraw.h"
#include "image.h"
#include "watchers.h"

// -----------------------------------------------------------------------------

BOOL g_doMipMap = FALSE;
int  g_agpVidMemState = 0;

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Make those ridiculous DirectDraw error returns actually mean something.	   *
 *******************************************************************************/
const char* DirectDraw::ErrString(HRESULT err)
{
	switch(err)
	{
#define REGISTER_ERROR(e) case e : return #e
		REGISTER_ERROR(DD_OK);
		REGISTER_ERROR(DDERR_ALREADYINITIALIZED);
		REGISTER_ERROR(DDERR_CANNOTATTACHSURFACE);
		REGISTER_ERROR(DDERR_CANNOTDETACHSURFACE);
		REGISTER_ERROR(DDERR_CURRENTLYNOTAVAIL);
		REGISTER_ERROR(DDERR_EXCEPTION);
		REGISTER_ERROR(DDERR_GENERIC);
		REGISTER_ERROR(DDERR_HEIGHTALIGN);
		REGISTER_ERROR(DDERR_INCOMPATIBLEPRIMARY);
		REGISTER_ERROR(DDERR_INVALIDCAPS);
		REGISTER_ERROR(DDERR_INVALIDCLIPLIST);
		REGISTER_ERROR(DDERR_INVALIDMODE);
		REGISTER_ERROR(DDERR_INVALIDOBJECT);
		REGISTER_ERROR(DDERR_INVALIDPARAMS);
		REGISTER_ERROR(DDERR_INVALIDPIXELFORMAT);
		REGISTER_ERROR(DDERR_INVALIDRECT);
		REGISTER_ERROR(DDERR_LOCKEDSURFACES);
		REGISTER_ERROR(DDERR_NO3D);
		REGISTER_ERROR(DDERR_NOALPHAHW);
		REGISTER_ERROR(DDERR_NOCLIPLIST);
		REGISTER_ERROR(DDERR_NOCOLORCONVHW);
		REGISTER_ERROR(DDERR_NOCOOPERATIVELEVELSET);
		REGISTER_ERROR(DDERR_NOCOLORKEY);
		REGISTER_ERROR(DDERR_NOCOLORKEYHW);
		REGISTER_ERROR(DDERR_NODIRECTDRAWSUPPORT);
		REGISTER_ERROR(DDERR_NOEXCLUSIVEMODE);
		REGISTER_ERROR(DDERR_NOFLIPHW);
		REGISTER_ERROR(DDERR_NOGDI);
		REGISTER_ERROR(DDERR_NOMIRRORHW);
		REGISTER_ERROR(DDERR_NOTFOUND);
		REGISTER_ERROR(DDERR_NOOVERLAYHW);
		REGISTER_ERROR(DDERR_NORASTEROPHW);
		REGISTER_ERROR(DDERR_NOROTATIONHW);
		REGISTER_ERROR(DDERR_NOSTRETCHHW);
		REGISTER_ERROR(DDERR_NOT4BITCOLOR);
		REGISTER_ERROR(DDERR_NOT4BITCOLORINDEX);
		REGISTER_ERROR(DDERR_NOT8BITCOLOR);
		REGISTER_ERROR(DDERR_NOTEXTUREHW);
		REGISTER_ERROR(DDERR_NOVSYNCHW);
		REGISTER_ERROR(DDERR_NOZBUFFERHW);
		REGISTER_ERROR(DDERR_NOZOVERLAYHW);
		REGISTER_ERROR(DDERR_OUTOFCAPS);
		REGISTER_ERROR(DDERR_OUTOFMEMORY);
		REGISTER_ERROR(DDERR_OUTOFVIDEOMEMORY);
		REGISTER_ERROR(DDERR_OVERLAYCANTCLIP);
		REGISTER_ERROR(DDERR_OVERLAYCOLORKEYONLYONEACTIVE);
		REGISTER_ERROR(DDERR_PALETTEBUSY);
		REGISTER_ERROR(DDERR_COLORKEYNOTSET);
		REGISTER_ERROR(DDERR_SURFACEALREADYATTACHED);
		REGISTER_ERROR(DDERR_SURFACEALREADYDEPENDENT);
		REGISTER_ERROR(DDERR_SURFACEBUSY);
		REGISTER_ERROR(DDERR_CANTLOCKSURFACE);
		REGISTER_ERROR(DDERR_SURFACEISOBSCURED);
		REGISTER_ERROR(DDERR_SURFACELOST);
		REGISTER_ERROR(DDERR_SURFACENOTATTACHED);
		REGISTER_ERROR(DDERR_TOOBIGHEIGHT);
		REGISTER_ERROR(DDERR_TOOBIGSIZE);
		REGISTER_ERROR(DDERR_TOOBIGWIDTH);
		REGISTER_ERROR(DDERR_UNSUPPORTED);
		REGISTER_ERROR(DDERR_UNSUPPORTEDFORMAT);
		REGISTER_ERROR(DDERR_UNSUPPORTEDMASK);
		REGISTER_ERROR(DDERR_VERTICALBLANKINPROGRESS);
		REGISTER_ERROR(DDERR_WASSTILLDRAWING);
		REGISTER_ERROR(DDERR_XALIGN);
		REGISTER_ERROR(DDERR_INVALIDDIRECTDRAWGUID);
		REGISTER_ERROR(DDERR_DIRECTDRAWALREADYCREATED);
		REGISTER_ERROR(DDERR_NODIRECTDRAWHW);
		REGISTER_ERROR(DDERR_PRIMARYSURFACEALREADYEXISTS);
		REGISTER_ERROR(DDERR_NOEMULATION);
		REGISTER_ERROR(DDERR_REGIONTOOSMALL);
		REGISTER_ERROR(DDERR_CLIPPERISUSINGHWND);
		REGISTER_ERROR(DDERR_NOCLIPPERATTACHED);
		REGISTER_ERROR(DDERR_NOHWND);
		REGISTER_ERROR(DDERR_HWNDSUBCLASSED);
		REGISTER_ERROR(DDERR_HWNDALREADYSET);
		REGISTER_ERROR(DDERR_NOPALETTEATTACHED);
		REGISTER_ERROR(DDERR_NOPALETTEHW);
		REGISTER_ERROR(DDERR_BLTFASTCANTCLIP);
		REGISTER_ERROR(DDERR_NOBLTHW);
		REGISTER_ERROR(DDERR_NODDROPSHW);
		REGISTER_ERROR(DDERR_OVERLAYNOTVISIBLE);
		REGISTER_ERROR(DDERR_NOOVERLAYDEST);
		REGISTER_ERROR(DDERR_INVALIDPOSITION);
		REGISTER_ERROR(DDERR_NOTAOVERLAYSURFACE);
		REGISTER_ERROR(DDERR_EXCLUSIVEMODEALREADYSET);
		REGISTER_ERROR(DDERR_NOTFLIPPABLE);
		REGISTER_ERROR(DDERR_CANTDUPLICATE);
		REGISTER_ERROR(DDERR_NOTLOCKED);
		REGISTER_ERROR(DDERR_CANTCREATEDC);
		REGISTER_ERROR(DDERR_NODC);
		REGISTER_ERROR(DDERR_WRONGMODE);
		REGISTER_ERROR(DDERR_IMPLICITLYCREATED);
		REGISTER_ERROR(DDERR_NOTPALETTIZED);
		REGISTER_ERROR(DDERR_UNSUPPORTEDMODE);
		REGISTER_ERROR(DDERR_NOMIPMAPHW);
		REGISTER_ERROR(DDERR_INVALIDSURFACETYPE);
		REGISTER_ERROR(DDERR_DCALREADYCREATED);
		REGISTER_ERROR(DDERR_CANTPAGELOCK);
		REGISTER_ERROR(DDERR_CANTPAGEUNLOCK);
		REGISTER_ERROR(DDERR_NOTPAGELOCKED);
		REGISTER_ERROR(DDERR_NOTINITIALIZED);
#undef REGISTER_ERROR
	default:
		return "DirectDraw::DDErrString(): Unknown Error Number";
	}
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Generate a 32 bit RGBA color from the given red, blue, green and alpha 	   *
 *	components. Each coponent's length can be determined from the bit mask 	   *
 *	values found in the pixel format structure.								   *
 *******************************************************************************/
DWORD RGBColor(DDPIXELFORMAT& rPixelFormat, BYTE red, BYTE green, BYTE blue, BYTE alpha)
{
	if(!(rPixelFormat.dwFlags & DDPF_RGB)) return 0;

	DWORD result = 0;

	int shift = 0;
	DWORD bits = rPixelFormat.dwRBitMask;
	if(bits)
	{
		while(!(bits & 1))
		{
			shift++;
			bits >>= 1;
		}

		while(!(bits & 0x80))
		{
			shift--;
			bits <<= 1;
		}
	}

	if(shift > 0)
		result |= (((DWORD)red) & bits) << shift;
	else
		result |= (((DWORD)red) & bits) >> -shift;


	shift = 0;
	bits = rPixelFormat.dwGBitMask;
	if(bits)
	{
		while(!(bits & 1))
		{
			shift++;
			bits >>= 1;
		}

		while(!(bits & 0x80))
		{
			shift--;
			bits <<= 1;
		}
	}

	if(shift > 0)
		result |= (((DWORD)green) & bits) << shift;
	else
		result |= (((DWORD)green) & bits) >> -shift;

	shift = 0;
	bits = rPixelFormat.dwBBitMask;
	if(bits)
	{
		while(!(bits & 1))
		{
			shift++;
			bits >>= 1;
		}

		while(!(bits & 0x80))
		{
			shift--;
			bits <<= 1;
		}
	}

	if(shift > 0)
		result |= (((DWORD)blue) & bits) << shift;
	else
		result |= (((DWORD)blue) & bits) >> -shift;

	shift = 0;
	bits = rPixelFormat.dwRGBAlphaBitMask;
	if(bits)
	{
		while(!(bits & 1))
		{
			shift++;
			bits >>= 1;
		}

		while(!(bits & 0x80))
		{
			shift--;
			bits <<= 1;
		}
	}

	if(shift > 0)
		result |= (((DWORD)alpha) & bits) << shift;
	else
		result |= (((DWORD)alpha) & bits) >> -shift;

	return result;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	C++ wrapper for standard DirectDraw Lock function. If DEBUG is defined,    *
 *	then the surface is unlocked immediately after it is locked, so that a 	   *
 *	source level debugger may then be used to step through the subsequent code.* 
 *	Lock returns a valid surface pointer (the 'lpSurface' field of the surface *
 *	description) which can then be used to access the surface directly.		   *
 *******************************************************************************/
void LockSurface(DDSURFACEDESC& rDesc, IDirectDrawSurface& rSurface) 
{
	HRESULT result = rSurface.Lock(NULL, &rDesc, DDLOCK_WAIT, NULL);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);

#ifdef _DEBUG
	result = rSurface.Unlock(rDesc.lpSurface);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
#endif
}


/*******************************************************************************
 *	C++ wrapper for standard DirectDraw Unlock function. If DEBUG is defined,  *
 *  this call does nothing (in DEBUG the Lock function immediately unlocks 	   *
 *	the surface).															   *
 *******************************************************************************/
void UnlockSurface(IDirectDrawSurface& rSurface, void* ptr) 
{
#ifndef _DEBUG
	HRESULT result = rSurface.Unlock(ptr);
	if(result != DD_OK) THROW_DD_EXCEPTION(result);
#endif
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *  Win2000 texture-surface bypass (CWB 2026-09).
 *
 *  The Win2000 ATI DDraw HAL rejects CreateSurface with DDERR_INVALIDPIXELFORMAT
 *  for the demo's texel formats (8bpp CI8 / VQ) even for system memory -- it dies
 *  at DirectDraw.cpp CreateSurface before any ATI3DCIF_TextureReg. But the CIF only
 *  needs a *host pointer* to the texel bytes: LoadTexture()/Ati3dTexture() Lock the
 *  surface, read lpSurface (+ lPitch/width/height/pixelformat), and hand the pointer
 *  to ATI3DCIF_TextureReg. The texture surface is NEVER Blt'd or GetDC'd (Blt appears
 *  only in the g_doMipMap branch, which is off by default; the font path -- which DOES
 *  Blt -- uses bTexture=FALSE and keeps a real DDraw surface).
 *
 *  So for textures we back the "surface" with a plain malloc'd, tightly-packed buffer
 *  via a minimal IDirectDrawSurface: Lock hands back the buffer + a pitch of
 *  width*bytesperpixel (the CIF assumes power-of-two, tightly packed rows), Unlock is
 *  a no-op, GetAttachedSurface reports "no mip", and Release frees the buffer. Every
 *  other interface method is an unused stub (never called on this object).
 *******************************************************************************/
class SysMemSurface : public IDirectDrawSurface {
public:
	SysMemSurface(int w, int h, const DDPIXELFORMAT& pf)
	{
		m_ref    = 1;
		m_width  = w;
		m_height = h;
		memset(&m_pf, 0, sizeof(m_pf));
		m_pf     = pf;
		int bpp  = pf.dwRGBBitCount ? (int)pf.dwRGBBitCount : 8;
		m_pitch  = (w * bpp) / 8;			// tightly packed (CIF expects pow2, no pad)
		m_size   = (DWORD)m_pitch * (h > 0 ? h : 1);
		m_bits   = malloc(m_size ? m_size : 1);
		if(m_bits) memset(m_bits, 0, m_size ? m_size : 1);
	}
	void FillDesc(LPDDSURFACEDESC d)
	{
		memset(d, 0, sizeof(*d));
		d->dwSize            = sizeof(*d);
		d->dwFlags           = DDSD_WIDTH|DDSD_HEIGHT|DDSD_PITCH|DDSD_PIXELFORMAT|DDSD_CAPS|DDSD_LPSURFACE;
		d->dwWidth           = m_width;
		d->dwHeight          = m_height;
		d->lPitch            = m_pitch;
		d->ddpfPixelFormat   = m_pf;
		d->ddsCaps.dwCaps    = DDSCAPS_SYSTEMMEMORY | DDSCAPS_TEXTURE;
		d->lpSurface         = m_bits;
	}

	// --- IUnknown ---
	STDMETHOD(QueryInterface)(THIS_ REFIID, void** ppv) { if(ppv) *ppv = NULL; return E_NOINTERFACE; }
	STDMETHOD_(ULONG,AddRef)(THIS) { return ++m_ref; }
	STDMETHOD_(ULONG,Release)(THIS) { ULONG r = --m_ref; if(r == 0) { if(m_bits) free(m_bits); delete this; } return r; }

	// --- the three the demo actually calls on a texture surface ---
	STDMETHOD(GetAttachedSurface)(THIS_ LPDDSCAPS, LPDIRECTDRAWSURFACE* out) { if(out) *out = NULL; return DDERR_NOTFOUND; }
	STDMETHOD(Lock)(THIS_ LPRECT, LPDDSURFACEDESC d, DWORD, HANDLE) { if(!d) return DDERR_INVALIDPARAMS; FillDesc(d); return DD_OK; }
	STDMETHOD(Unlock)(THIS_ LPVOID) { return DD_OK; }

	// --- harmless helpers (not required, but correct if ever queried) ---
	STDMETHOD(GetSurfaceDesc)(THIS_ LPDDSURFACEDESC d) { if(!d) return DDERR_INVALIDPARAMS; FillDesc(d); return DD_OK; }
	STDMETHOD(GetPixelFormat)(THIS_ LPDDPIXELFORMAT p) { if(!p) return DDERR_INVALIDPARAMS; *p = m_pf; return DD_OK; }
	STDMETHOD(IsLost)(THIS) { return DD_OK; }
	STDMETHOD(Restore)(THIS) { return DD_OK; }

	// --- unused stubs (never invoked on this object) ---
	STDMETHOD(AddAttachedSurface)(THIS_ LPDIRECTDRAWSURFACE) { return DDERR_UNSUPPORTED; }
	STDMETHOD(AddOverlayDirtyRect)(THIS_ LPRECT) { return DDERR_UNSUPPORTED; }
	STDMETHOD(Blt)(THIS_ LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX) { return DDERR_UNSUPPORTED; }
	STDMETHOD(BltBatch)(THIS_ LPDDBLTBATCH, DWORD, DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(BltFast)(THIS_ DWORD, DWORD, LPDIRECTDRAWSURFACE, LPRECT, DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(DeleteAttachedSurface)(THIS_ DWORD, LPDIRECTDRAWSURFACE) { return DDERR_UNSUPPORTED; }
	STDMETHOD(EnumAttachedSurfaces)(THIS_ LPVOID, LPDDENUMSURFACESCALLBACK) { return DDERR_UNSUPPORTED; }
	STDMETHOD(EnumOverlayZOrders)(THIS_ DWORD, LPVOID, LPDDENUMSURFACESCALLBACK) { return DDERR_UNSUPPORTED; }
	STDMETHOD(Flip)(THIS_ LPDIRECTDRAWSURFACE, DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetBltStatus)(THIS_ DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetCaps)(THIS_ LPDDSCAPS) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetClipper)(THIS_ LPDIRECTDRAWCLIPPER*) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetColorKey)(THIS_ DWORD, LPDDCOLORKEY) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetDC)(THIS_ HDC*) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetFlipStatus)(THIS_ DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetOverlayPosition)(THIS_ LPLONG, LPLONG) { return DDERR_UNSUPPORTED; }
	STDMETHOD(GetPalette)(THIS_ LPDIRECTDRAWPALETTE*) { return DDERR_UNSUPPORTED; }
	STDMETHOD(Initialize)(THIS_ LPDIRECTDRAW, LPDDSURFACEDESC) { return DDERR_UNSUPPORTED; }
	STDMETHOD(ReleaseDC)(THIS_ HDC) { return DDERR_UNSUPPORTED; }
	STDMETHOD(SetClipper)(THIS_ LPDIRECTDRAWCLIPPER) { return DDERR_UNSUPPORTED; }
	STDMETHOD(SetColorKey)(THIS_ DWORD, LPDDCOLORKEY) { return DDERR_UNSUPPORTED; }
	STDMETHOD(SetOverlayPosition)(THIS_ LONG, LONG) { return DDERR_UNSUPPORTED; }
	STDMETHOD(SetPalette)(THIS_ LPDIRECTDRAWPALETTE) { return DDERR_UNSUPPORTED; }
	STDMETHOD(UpdateOverlay)(THIS_ LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDOVERLAYFX) { return DDERR_UNSUPPORTED; }
	STDMETHOD(UpdateOverlayDisplay)(THIS_ DWORD) { return DDERR_UNSUPPORTED; }
	STDMETHOD(UpdateOverlayZOrder)(THIS_ DWORD, LPDIRECTDRAWSURFACE) { return DDERR_UNSUPPORTED; }

private:
	ULONG         m_ref;
	int           m_width, m_height;
	long          m_pitch;
	DWORD         m_size;
	DDPIXELFORMAT m_pf;
	void*         m_bits;
};

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	Load an image from a file, create a surface for it and write the image 	   *
 *	to the surface, converting formats appropriately.						   *
 *******************************************************************************/
IDirectDrawSurface* LoadSurface(IDirectDraw& rDD, char* pFilename, BOOL bTexture, pixelType texFmtOverride)
{
	BOOL success = FALSE;

	int width, height;
	pixelType fmt;
	ImageSize(&width, &height, &fmt, pFilename);

	if(texFmtOverride == PIXELTYPE_UNKNOWN)
		texFmtOverride = fmt;

	DDSurfaceDesc desc;
	desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
	desc.dwWidth = width;
	desc.dwHeight = height;
	desc.ddpfPixelFormat = DDPixelFormat(texFmtOverride);

  	//If we're trying to load a texture, set up a DirectDraw surface 
	//description subject to the following:
	//	If AGP-enabled, use card video RAM (LOCALVIDRAM) or AGP 
	//		memory (NONLOCALVIDRAM),
	//	If non-AGP use card video RAM.
	//	If there is not enough room on the card, the call will fail 
	//		because we currently load all textures into video memory.
	//If we are MIPmapping, we set the approprate surface creation flag 
	//and create a surface with multiple attached surfaces to load the 
	//various MIPmap levels into. If we're not loading a texture, then
	//we just create an offscreen plain surface
	IDirectDrawSurface* pSurface;
	if(bTexture)
	{
		// --- Win2000 / ATI Rage DDraw HAL fix (CWB 2026-09) ---
		// The Win2000 ATI HAL rejects CreateSurface (DDERR_INVALIDPIXELFORMAT)
		// for the demo's texel formats even with DDSCAPS_SYSTEMMEMORY, so bypass
		// DDraw entirely: back the texture "surface" with a malloc'd host buffer.
		// The CIF only needs that host pointer (see SysMemSurface note above).
		// mipmaps are unsupported by the bypass, but g_doMipMap is off by default.
		pSurface = new SysMemSurface(width, height, desc.ddpfPixelFormat);
		if(!pSurface) THROW_DD_EXCEPTION(DDERR_OUTOFMEMORY);
		// --- end fix ---
	}
	else
	{
		desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
		HRESULT result = rDD.CreateSurface(&desc, &pSurface, NULL);
		if(result != DD_OK) THROW_DD_EXCEPTION(result);
	}
	DECLARE_MEMBER_WATCHER_WINAPI(IDirectDrawSurface, ULONG, *pSurface, Release, success, pSurface);

	//Lock the surface and get a valid pointer so we can write directly 
	//to the surface. ImageRead will do any file to color format conversion 
	//necessary. texFmtOverride specifies the format the surface will be 
	//created with. The default is UNKNOWN which means that the surface format 
	//will be based on the format the image was stored with in the image's 
	//data file. If texFmtOverride is explicitly set then the image will be 
	//converted to that format, if necessary, and written to the surface.
	LockSurface(desc, *pSurface);

	ImageRead(desc.lpSurface, width, height, texFmtOverride, desc.lPitch, pFilename);

	UnlockSurface(*pSurface, desc.lpSurface);

	//Set our first MIP surface to the main surface in the complex surface
	//structure, so we can enumerate our attached surfaces and blit the 
	//MIPs to them, one at a time.
	if(g_doMipMap)
	{
		int                 mipLevel=0;
		IDirectDrawSurface* pMipLevel = pSurface;

		while(1)
		{

			DDSCAPS caps;
			caps.dwCaps = DDSCAPS_MIPMAP;
			HRESULT result = pMipLevel->GetAttachedSurface(&caps, &pMipLevel);
			if(result == DD_OK)
				mipLevel++;
			else
				break;

			pMipLevel->Blt(NULL, pSurface, NULL, DDBLT_WAIT, NULL);
		}
	}

	success = TRUE;

	return pSurface;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	C++ wrapper for pixel format information. This usually gets used during    *
 *	surface creation to tell the surface what pixel format it should be created* 
 *	with. Information in the pixel format structures includes bits per pixel,  *
 *	color format and alpha format.											   *
 *******************************************************************************/
DDPixelFormat::DDPixelFormat(pixelType pixfmt)
{
	memset(this, 0, sizeof(*this));
	dwSize = sizeof(*this);

	dwFlags = DDPF_RGB;

	switch(pixfmt)
	{
	case PIXELTYPE_8:
		dwRGBBitCount = 8;
		dwRBitMask = 0xFF;
		dwGBitMask = 0x00;
		dwBBitMask = 0x00;
		dwRGBAlphaBitMask = 0x00;
		break;

	case PIXELTYPE_VQ:
		dwRGBBitCount = 8;
		dwRBitMask = 0x00;
		dwGBitMask = 0xFF;
		dwBBitMask = 0x00;
		dwRGBAlphaBitMask = 0x00;
		break;

	case PIXELTYPE_332:
		dwRGBBitCount = 8;
		dwRBitMask = 0xE0;
		dwGBitMask = 0x1C;
		dwBBitMask = 0x03;
		dwRGBAlphaBitMask = 0x00;
		break;

	case PIXELTYPE_1555:
		dwRGBBitCount = 16;
		dwRBitMask = 0x7C00;
		dwGBitMask = 0x03E0;
		dwBBitMask = 0x001F;
		dwRGBAlphaBitMask = 0x8000;
		break;

	case PIXELTYPE_565:
		dwRGBBitCount = 16;
		dwRBitMask = 0xF800;
		dwGBitMask = 0x07E0;
		dwBBitMask = 0x001F;
		dwRGBAlphaBitMask = 0x0000;
		break;

	case PIXELTYPE_4444:
		dwRGBBitCount = 16;
		dwRBitMask = 0x0F00;
		dwGBitMask = 0x00F0;
		dwBBitMask = 0x000F;
		dwRGBAlphaBitMask = 0xF000;
		break;

	case PIXELTYPE_8888:
		dwRGBBitCount = 32;
		dwRBitMask = 0x00FF0000;
		dwGBitMask = 0x0000FF00;
		dwBBitMask = 0x000000FF;
		dwRGBAlphaBitMask = 0xFF000000;
		break;

	default:
		THROW_DD_EXCEPTION(DDERR_INVALIDPIXELFORMAT);
	}
}


/*******************************************************************************
 *	Returns the pixel enumeration constant of the currently referenced surface *
 *	based on the surface's RGB color format. 								   *
 *******************************************************************************/
DDPixelFormat::operator pixelType(void) 
{
	if(dwFlags & DDPF_RGB)
		switch(dwRGBBitCount)
		{
		case 8:
			if((dwRBitMask==0xE0) && (dwGBitMask==0x1C) && (dwBBitMask==0x03))
				return PIXELTYPE_332;
			else if((dwRBitMask==0xFF) && (dwGBitMask==0x00) && (dwBBitMask==0x00))
				return PIXELTYPE_8;
			else if((dwRBitMask==0x00) && (dwGBitMask==0xFF) && (dwBBitMask==0x00))
				return PIXELTYPE_VQ;
			else
				THROW_DD_EXCEPTION(DDERR_INVALIDPIXELFORMAT);
			break;
		case 16:
			if((dwRBitMask==0x7C00) && (dwGBitMask==0x03E0) && (dwBBitMask==0x001F))
				return PIXELTYPE_1555;
			else if((dwRBitMask==0xF800) && (dwGBitMask==0x07E0) && (dwBBitMask==0x001F))
				return PIXELTYPE_565;
			else if((dwRBitMask==0x0F00) && (dwGBitMask==0x00F0) && (dwBBitMask==0x000F))
				return PIXELTYPE_4444;
			else
				THROW_DD_EXCEPTION(DDERR_INVALIDPIXELFORMAT);
			break;

		case 32:
			if((dwRBitMask==0x00FF0000) && (dwGBitMask==0x0000FF00) && (dwBBitMask==0x000000FF))
				return PIXELTYPE_8888;
			else
				THROW_DD_EXCEPTION(DDERR_INVALIDPIXELFORMAT);
			break;
		}
	else
		THROW_DD_EXCEPTION(DDERR_INVALIDPIXELFORMAT);

	return PIXELTYPE_UNKNOWN;
}

// -----------------------------------------------------------------------------
/*******************************************************************************
 *	These functions specify where video memory should come from. On non-AGP    *
 *	enabled systems the driver will determine where video memory comes from.   *
 *	For the demo, this will always be on-board video memory. For AGP-enabled   *
 *	machines all memory	becomes available for use as video memory. In this 	   *
 *	case, video memory can be specified to come directly from the video 	   *
 *	board (LOCALVIDMEM) or AGP memory (NONLOCALVIDMEM).						   *					   *
 *******************************************************************************/

DWORD DDAgp(DWORD flags)
{
	if(g_agpVidMemState == 0)
		return 0;
	else
		return flags;
}

DWORD DDAgpUser(void)
{
	switch(g_agpVidMemState)
	{
	case 0:  return 0;
	case 1:  return DDSCAPS_LOCALVIDMEM;
	default: return DDSCAPS_NONLOCALVIDMEM;
	}
}

// -----------------------------------------------------------------------------
