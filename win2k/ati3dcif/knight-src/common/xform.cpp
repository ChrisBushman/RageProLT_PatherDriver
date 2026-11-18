/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Screen, world and view tranform functionality.							   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include "xform.h"

// -----------------------------------------------------------------------------

XFormStack::XForm*	XFormStack::XForm::m_pFreeList = NULL;
XFormStack::XForm	XFormStack::XForm::m_XFormArray[NUMBER_OF_XFORMS];

// -----------------------------------------------------------------------------

void XFormStack::XForm::Free(void)
{
	ASSERT(m_pPrevious==NULL);
	m_pPrevious = m_pFreeList;
	m_pFreeList = this;
}



XFormStack::XForm* XFormStack::XForm::Alloc(void) 
{
	if(m_pFreeList == NULL)	// First time called
	{
		for(int i=NUMBER_OF_XFORMS-1; i>=0; i--)
		{
			m_XFormArray[i].m_pPrevious = m_pFreeList;
			m_pFreeList = &m_XFormArray[i];
		}
	}

	if(m_pFreeList->m_pPrevious == NULL)	// Make sure we're not out of XForms...
		THROW_EXCEPTION();

	XForm* x = m_pFreeList;
	m_pFreeList = x->m_pPrevious;
	x->m_pPrevious = NULL;

	return x;
}

// -----------------------------------------------------------------------------

XFormStack::~XFormStack(void)
{
	ASSERT(m_pCurrentXForm->m_pPrevious == NULL);
	m_pCurrentXForm->Free();
}



XFormStack::XFormStack(void) : m_prescalar(IDENTITY)
{
	m_depthCue = FALSE;
	m_lighting = FALSE;
	m_farPlane = 1;
	m_halfWidth = 0;
	m_halfHeight = 0;
	m_halfWidth = 0;
	m_zBias = 0;
	m_pCurrentXForm = XFormStack::XForm::Alloc();
	m_pCurrentXForm->m_rotation = Matrix(IDENTITY);
	m_pCurrentXForm->m_translation = Vector(ZERO);
	m_pCurrentXForm->m_view = Vector(ZERO);
	m_pCurrentXForm->m_light = Vector(ZERO);
	RecomputeCurrent();
}

// -----------------------------------------------------------------------------

void XFormStack::RecomputeCurrent(void)
{
	m_current.rotation = m_prescalar * m_pCurrentXForm->m_rotation;
	m_current.translation = m_prescalar * m_pCurrentXForm->m_translation;
	m_current.view = m_pCurrentXForm->m_view;
}

// -----------------------------------------------------------------------------

void XFormStack::TransformDone(void)
{
	ASSERT(m_pCurrentXForm->m_pPrevious);

	XForm* p = m_pCurrentXForm;
	m_pCurrentXForm = p->m_pPrevious;
	p->m_pPrevious = NULL;

	p->Free();

	RecomputeCurrent();
}



void XFormStack::TransformView(const Matrix& rotation, const Vector& translation)
{
	XForm* xform = XForm::Alloc();

	xform->m_rotation =  Transpose(rotation) * m_pCurrentXForm->m_rotation;
	xform->m_translation = Transpose(rotation) * (m_pCurrentXForm->m_translation - translation);
	xform->m_view = m_pCurrentXForm->m_view + translation * m_pCurrentXForm->m_rotation;
	xform->m_light = m_pCurrentXForm->m_light + translation * m_pCurrentXForm->m_rotation;

	xform->m_pPrevious = m_pCurrentXForm;
	m_pCurrentXForm = xform;

	RecomputeCurrent();
}



void XFormStack::TransformObject(const Matrix& rotation, const Vector& translation)
{
	XForm* xform = XForm::Alloc();

	xform->m_rotation =  m_pCurrentXForm->m_rotation * rotation;
	xform->m_translation = m_pCurrentXForm->m_translation +  m_pCurrentXForm->m_rotation * translation;
	xform->m_view = (m_pCurrentXForm->m_view - translation) * rotation;
	xform->m_light = (m_pCurrentXForm->m_light - translation) * rotation;

	xform->m_pPrevious = m_pCurrentXForm;
	m_pCurrentXForm = xform;

	RecomputeCurrent();
}

// -----------------------------------------------------------------------------

void XFormStack::ComputeScreenCoordinate(TLVertex *pVertex, VertexType type)
{
	float w = 1.0 / pVertex->Z();
	float x = pVertex->X() * m_halfWidth * w + m_centerX;
	float y = pVertex->Y() * m_halfHeight * w + m_centerY;
	float z = (pVertex->Z() - 1) * m_zScalar * w;

	ASSERT((x >= -1) && (x <= 641));
	ASSERT((y >= -1) && (y <= 481));

	pVertex->VTCF().x = x;
	pVertex->VTCF().y = y;
	pVertex->VTCF().z = z + m_zBias;
	pVertex->VTCF().w = w;

	if(IsTextured(type))
	{
		pVertex->VTCF().s = pVertex->U() * w;
		pVertex->VTCF().t = pVertex->V() * w;
	}
}

// -----------------------------------------------------------------------------

void XFormStack::InvertScreenCoordinate(Vector* pA, Vector* pB, const POINT& point)
{
	ASSERT(pA);
	ASSERT(pB);

	float x = ((float)(point.x + 0.5) - m_centerX) / m_halfWidth;
	float y = ((float)(point.y + 0.5) - m_centerY) / m_halfHeight;

	Matrix m = Inverse(m_current.rotation);

	Vector a(UNINITIALIZED);
	a.Z() = 1.0;
	a.X() = x * a.Z();
	a.Y() = y * a.Z();

	(*pA) = m * (a - m_current.translation);

	Vector b(UNINITIALIZED);
	b.Z() = FarClipPlane();
	b.X() = x * b.Z();
	b.Y() = y * b.Z();

	(*pB) = m * (b - m_current.translation);
}

// -----------------------------------------------------------------------------

void XFormStack::SetWindow(int x, int y, int width, int height, float fov)
{
	m_halfWidth = (float)width / 2;
	m_halfHeight = (float)height / 2;
	m_centerX = (float)x + m_halfWidth;
	m_centerY = (float)y + m_halfHeight;

	float fovTrig = cos(fov/2) / sin(fov/2);
	m_prescalar = Matrix(SCALE, fovTrig, fovTrig, 1.0);
}

// -----------------------------------------------------------------------------

TLVertex* XFormStack::TransformPoint(TLVertex* pTL, Vertex* pV, VertexType type)
{
	pTL->UV() = pV->UV();

//	pTL->XYZ() = m_current.rotation * pV->XYZ() + m_current.translation;
	{
		real32* pDst = (real32*)&pTL->XYZ();
		real32* pSrcM = (real32*)&m_current.rotation;
		real32* pSrcV = (real32*)&pV->XYZ();
		real32* pSrcT = (real32*)&m_current.translation;

		pDst[0] = pSrcM[0*3+0] * pSrcV[0] + pSrcM[0*3+1] * pSrcV[1] + pSrcM[0*3+2] * pSrcV[2] + pSrcT[0];
		pDst[1] = pSrcM[1*3+0] * pSrcV[0] + pSrcM[1*3+1] * pSrcV[1] + pSrcM[1*3+2] * pSrcV[2] + pSrcT[1];
		pDst[2] = pSrcM[2*3+0] * pSrcV[0] + pSrcM[2*3+1] * pSrcV[1] + pSrcM[2*3+2] * pSrcV[2] + pSrcT[2];
	}

//	pTL->RGBA() = pV->RGBA();

	float colorScale = 1.0;

	if(m_lighting)
	{
		float dot = DotProduct(pV->Normal(), ObjectLight());
		if(dot < 0.0) dot = 0.0;
		if(dot > 1.0) dot = 1.0;
		colorScale *= m_lightScale * dot + m_lightAmbient;
	}

	if(m_depthCue)
	{
		float distance = fabs(pTL->XYZ().Z());
		colorScale *= 1.0 - (distance * m_invDepthCueScale);
	}

	if(colorScale <= 0.0)
	{
		real32* pDst = (real32*)&pTL->RGBA();
		pDst[0] = 0.0;
		pDst[1] = 0.0;
		pDst[2] = 0.0;
		pDst[3] = pV->RGBA().A();
	}
	else if(colorScale >= 1.0)
	{
		pTL->RGBA() = pV->RGBA();
	}
	else
	{
		real32* pDst = (real32*)&pTL->RGBA();
		real32* pSrc = (real32*)&pV->RGBA();
		pDst[0] = pSrc[0] * colorScale;
		pDst[1] = pSrc[1] * colorScale;
		pDst[2] = pSrc[2] * colorScale;
		pDst[3] = pSrc[3];
	}

	if(pTL->ComputeClipCode(m_farPlane) == CC_ON)
		ComputeScreenCoordinate(pTL, type);

	return pTL;
}


Normal XFormStack::TransformNormal(const Normal& rSrc) const
{
	return TransformBy(rSrc, m_current.rotation, m_current.translation);
}



Normal XFormStack::UntransformNormal(const Normal& rSrc) const
{
	return UntransformBy(rSrc, m_current.rotation, m_current.translation);
}

// -----------------------------------------------------------------------------
