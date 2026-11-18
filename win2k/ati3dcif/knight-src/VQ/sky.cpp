/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	The base sequence and set up for rendering of the sky polygon.			   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include "Util.h"
#include "Watchers.h"
#include "DirectDraw.h"

#include "Ati3dCIFx.h"
#include "Matrix.h"
#include "Multimedia.h"

#include "AtiDemoWnd.h"
#include "AtiDemo.h"
#include "Polygon.h"
#include "physics.h"
#include "normalmode.h"
#include "a3d.h"
#include "ajm.h"
#include "sky.h"

#include "filenames.h"

#include "vqt.h"
// -----------------------------------------------------------------------------

#define DAYSKY_PCX_TEXTURE_FILENAME			BASE_PATH "sky.pcx"
#define DAYSKY_VQT_TEXTURE_FILENAME			BASE_PATH "sky.vqt"

// -----------------------------------------------------------------------------

#define SKY_SIZE	(1000 * 3 * 12)
#define SKY_HEIGHT0	(250 * 12)
#define SKY_HEIGHT1	(-300 * 3 * 12)
#define SKY_HEIGHT2	(-900 * 3 * 12)

#define SKYCOLOR RGBA(82, 132, 173, 0)

// -----------------------------------------------------------------------------

static Ati3dTexture* s_pSkyTexture[2];
extern int g_mode;

// -----------------------------------------------------------------------------

void Sky::Cleanup(void)
{
	delete s_pSkyTexture[1];
	s_pSkyTexture[1] = NULL;

	delete s_pSkyTexture[0];
	s_pSkyTexture[0] = NULL;
}

void Sky::Initialize(void)
{
	BOOL success = FALSE;

	s_pSkyTexture[0] = ATI3DCIF::LoadTexture(*g_pDD, DAYSKY_PCX_TEXTURE_FILENAME);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, s_pSkyTexture[0], success, sky1);

	s_pSkyTexture[1] = ATI3DCIF::LoadTexture(*g_pDD, DAYSKY_VQT_TEXTURE_FILENAME, C3D_ETF_VQ);
	DECLARE_POINTER_WATCHER(Ati3dTexture*, s_pSkyTexture[1], success, sky2);

#if 0
	DDSurfaceDesc desc;
	LockSurface(desc, *s_pSkyTexture[1]->DDSurface());
	BYTE* pDst=(BYTE*)desc.lpSurface;
	for(DWORD j=0; j<desc.dwHeight; j++)
	{
		for(DWORD i=0; i<desc.dwWidth; i++)
			pDst[j*desc.lPitch+i] = j&255;
	}
	UnlockSurface(*s_pSkyTexture[1]->DDSurface(), desc.lpSurface);


#define RGB565(r, g, b)  ((((r)<<8)&0xF800)|(((g)<<2)&0x07C0)|((b)>>3))

	VqtCodebook codebook;
	for(j=0; j<256; j++)
	{
		if(!(j&1))
		{
			codebook.entry[j].t0 = RGB565(255, 0, 0);
			codebook.entry[j].t1 = RGB565(255, 0, 0);
			codebook.entry[j].t2 = RGB565(0, 255, 0);
			codebook.entry[j].t3 = RGB565(0, 255, 0);
		}
		else
		{
			codebook.entry[j].t0 = RGB565(255, 255, 0);
			codebook.entry[j].t1 = RGB565(255, 255, 0);
			codebook.entry[j].t2 = RGB565(0, 0, 255);
			codebook.entry[j].t3 = RGB565(0, 0, 255);
		}
	}

	s_pSkyTexture[1] = new Ati3dTexture(*s_pSkyTexture[1]->DDSurface(), &codebook, FALSE);
#endif

	success = TRUE;
}

// -----------------------------------------------------------------------------

#define SKY0 (-0.500 * SKY_SIZE)
#define SKY1 (-0.212 * SKY_SIZE)
#define SKY2 ( 0.212 * SKY_SIZE)
#define SKY3 ( 0.500 * SKY_SIZE)

struct _SkyPolyDef {
	_SkyPolyDef(const Vertex& ia, const Vertex& ib, const Vertex& ic) : count(3),
																		a(ia),
																		b(ib),
																		c(ic),
																		d(ZERO),
																		normal(ia.XYZ(), ib.XYZ(), ic.XYZ())
	{
	}

	_SkyPolyDef(const Vertex& ia, const Vertex& ib, const Vertex& ic, const Vertex& id) :
																		count(4),
																		a(ia),
																		b(ib),
																		c(ic),
																		d(id),
																		normal(ia.XYZ(), ib.XYZ(), ic.XYZ())
	{
	}

	int		count;
	Vertex	a;
	Vertex	b;
	Vertex	c;
	Vertex	d;
	Normal	normal;
};

#define SKYPOLYS_COUNT (sizeof(skyPolys) / sizeof(_SkyPolyDef))

#define SKYA0	Vector(SKY2, SKY_HEIGHT0, SKY0)
#define SKYA1	Vector(SKY3, SKY_HEIGHT0, SKY1)
#define SKYA2	Vector(SKY3, SKY_HEIGHT0, SKY2)
#define SKYA3	Vector(SKY2, SKY_HEIGHT0, SKY3)
#define SKYA4	Vector(SKY1, SKY_HEIGHT0, SKY3)
#define SKYA5	Vector(SKY0, SKY_HEIGHT0, SKY2)
#define SKYA6	Vector(SKY0, SKY_HEIGHT0, SKY1)
#define SKYA7	Vector(SKY1, SKY_HEIGHT0, SKY0)
#define SKYB0	Vector(SKY2, SKY_HEIGHT1, SKY0)
#define SKYB1	Vector(SKY3, SKY_HEIGHT1, SKY1)
#define SKYB2	Vector(SKY3, SKY_HEIGHT1, SKY2)
#define SKYB3	Vector(SKY2, SKY_HEIGHT1, SKY3)
#define SKYB4	Vector(SKY1, SKY_HEIGHT1, SKY3)
#define SKYB5	Vector(SKY0, SKY_HEIGHT1, SKY2)
#define SKYB6	Vector(SKY0, SKY_HEIGHT1, SKY1)
#define SKYB7	Vector(SKY1, SKY_HEIGHT1, SKY0)
#define SKYC0	Vector(SKY2, SKY_HEIGHT2, SKY1)
#define SKYC1	Vector(SKY2, SKY_HEIGHT2, SKY2)
#define SKYC2	Vector(SKY1, SKY_HEIGHT2, SKY2)
#define SKYC3	Vector(SKY1, SKY_HEIGHT2, SKY1)

#define TEX0	0.008
#define TEX1	0.055
#define TEX2	0.500
#define TEX3	0.992

#define TEXX(x) ((float)(x) / 1024.0)

static _SkyPolyDef skyPolys[] = {
	_SkyPolyDef(Vertex(SKYA0, UV(TEXX(   0), TEX3)),
				Vertex(SKYB0, UV(TEXX(   0), TEX2)),
				Vertex(SKYB1, UV(TEXX( 128), TEX2)),
				Vertex(SKYA1, UV(TEXX( 128), TEX3))),
  
	_SkyPolyDef(Vertex(SKYA1, UV(TEXX( 128), TEX3)),
				Vertex(SKYB1, UV(TEXX( 128), TEX2)),
				Vertex(SKYB2, UV(TEXX( 256), TEX2)),
				Vertex(SKYA2, UV(TEXX( 256), TEX3))),
  
	_SkyPolyDef(Vertex(SKYA2, UV(TEXX( 256), TEX3)),
				Vertex(SKYB2, UV(TEXX( 256), TEX2)),
				Vertex(SKYB3, UV(TEXX( 384), TEX2)),
				Vertex(SKYA3, UV(TEXX( 384), TEX3))),
  
	_SkyPolyDef(Vertex(SKYA3, UV(TEXX( 384), TEX3)),
				Vertex(SKYB3, UV(TEXX( 384), TEX2)),
				Vertex(SKYB4, UV(TEXX( 512), TEX2)),
				Vertex(SKYA4, UV(TEXX( 512), TEX3))),
  
	_SkyPolyDef(Vertex(SKYA4, UV(TEXX( 512), TEX3)),
				Vertex(SKYB4, UV(TEXX( 512), TEX2)),
				Vertex(SKYB5, UV(TEXX( 640), TEX2)),
				Vertex(SKYA5, UV(TEXX( 640), TEX3))),
	
	_SkyPolyDef(Vertex(SKYA5, UV(TEXX( 640), TEX3)),
				Vertex(SKYB5, UV(TEXX( 640), TEX2)),
				Vertex(SKYB6, UV(TEXX( 768), TEX2)),
				Vertex(SKYA6, UV(TEXX( 768), TEX3))),
	
	_SkyPolyDef(Vertex(SKYA6, UV(TEXX( 768), TEX3)),
				Vertex(SKYB6, UV(TEXX( 768), TEX2)),
				Vertex(SKYB7, UV(TEXX( 896), TEX2)),
				Vertex(SKYA7, UV(TEXX( 896), TEX3))),
	
	_SkyPolyDef(Vertex(SKYA7, UV(TEXX( 896), TEX3)),
				Vertex(SKYB7, UV(TEXX( 896), TEX2)),
				Vertex(SKYB0, UV(TEXX(1024), TEX2)),
				Vertex(SKYA0, UV(TEXX(1024), TEX3))),

	_SkyPolyDef(Vertex(SKYB0, UV(TEXX(   0), TEX2)),
				Vertex(SKYC0, UV(TEXX(  64), TEX1)),
				Vertex(SKYB1, UV(TEXX( 128), TEX2))),

	_SkyPolyDef(Vertex(SKYB1, UV(TEXX( 128), TEX2)),
				Vertex(SKYC0, UV(TEXX( 128), TEX0)),
				Vertex(SKYC1, UV(TEXX( 256), TEX0)),
				Vertex(SKYB2, UV(TEXX( 256), TEX2))),

	_SkyPolyDef(Vertex(SKYB2, UV(TEXX( 256), TEX2)),
				Vertex(SKYC1, UV(TEXX( 320), TEX1)),
				Vertex(SKYB3, UV(TEXX( 384), TEX2))),

	_SkyPolyDef(Vertex(SKYB3, UV(TEXX( 384), TEX2)),
				Vertex(SKYC1, UV(TEXX( 384), TEX0)),
				Vertex(SKYC2, UV(TEXX( 512), TEX0)),
				Vertex(SKYB4, UV(TEXX( 512), TEX2))),
	
	_SkyPolyDef(Vertex(SKYB4, UV(TEXX( 512), TEX2)),
				Vertex(SKYC2, UV(TEXX( 576), TEX1)),
				Vertex(SKYB5, UV(TEXX( 640), TEX2))),
	
	_SkyPolyDef(Vertex(SKYB5, UV(TEXX( 640), TEX2)),
				Vertex(SKYC2, UV(TEXX( 640), TEX0)),
				Vertex(SKYC3, UV(TEXX( 768), TEX0)),
				Vertex(SKYB6, UV(TEXX( 768), TEX2))),
	
	_SkyPolyDef(Vertex(SKYB6, UV(TEXX( 768), TEX2)),
				Vertex(SKYC3, UV(TEXX( 832), TEX1)),
				Vertex(SKYB7, UV(TEXX( 896), TEX2))),
	
	_SkyPolyDef(Vertex(SKYB7, UV(TEXX( 896), TEX2)),
				Vertex(SKYC3, UV(TEXX( 896), TEX0)),
				Vertex(SKYC0, UV(TEXX(1024), TEX0)),
				Vertex(SKYB0, UV(TEXX(1024), TEX2))),
};

static _SkyPolyDef skyCap = 
	_SkyPolyDef(Vertex(SKYC0),
				Vertex(SKYC3),
				Vertex(SKYC2),
				Vertex(SKYC1));

void Sky::Draw(Clipper& rClipper) 
{
	rClipper.Context()->SetZMode(C3D_EZMODE_OFF, C3D_EZCMP_NEVER);

	switch(g_mode)
	{
	case 1: rClipper.Context()->SetTexture(s_pSkyTexture[0]); break;
	case 2: rClipper.Context()->SetTexture(s_pSkyTexture[1]); break;
	}
	float savedFarClipPlane = rClipper.Stack()->FarClipPlane();
	rClipper.Stack()->SetFarClipPlane(100000);

	for(int i=0; i<SKYPOLYS_COUNT; i++)
	{
		if(rClipper.Stack()->IsFacing(skyPolys[i].normal))
			if(skyPolys[i].count == 3)
				rClipper.DrawTriangle(skyPolys[i].a, skyPolys[i].b, skyPolys[i].c, VERTEXTYPE_T);
			else
				rClipper.DrawQuad(skyPolys[i].a, skyPolys[i].b, skyPolys[i].c, skyPolys[i].d, VERTEXTYPE_T);
	}

	if(rClipper.Stack()->IsFacing(skyCap.normal))
		rClipper.DrawQuad(skyCap.a, skyCap.b, skyCap.c, skyCap.d, SKYCOLOR);

	rClipper.Stack()->SetFarClipPlane(savedFarClipPlane);

#if 0
	if(g_mode==2)
	{
		C3D_VTCF v0, v1, v2, v3;
		v0.x =   0; v0.y =   0; v0.z = 0; v0.w = 1; v0.s = 0; v0.t = 0;
		v1.x = 512; v1.y =   0; v1.z = 0; v1.w = 1; v1.s = .25; v1.t = 0;
		v2.x = 512; v2.y = 256; v2.z = 0; v2.w = 1; v2.s = .25; v2.t = .5;
		v3.x =   0; v3.y = 256; v3.z = 0; v3.w = 1; v3.s = 0; v3.t = .5;

		rClipper.Context()->SetPrimType(C3D_EPRIM_TRI);
		rClipper.Context()->SetTextureMode(TRUE);
		rClipper.Context()->SetLightingMode(C3D_ETL_NONE);
		rClipper.Context()->SetShadeMode(C3D_ESH_SOLID);
		rClipper.Context()->RenderPrimListVa(6, &v0, &v1, &v2, &v0, &v2, &v3);
	}
#endif
}

// -----------------------------------------------------------------------------
