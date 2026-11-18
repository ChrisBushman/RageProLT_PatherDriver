/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Shadow intersection, projection and rendering functionality.			   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include <malloc.h>
#include "Util.h"
#include "Watchers.h"
#include "DirectDraw.h"

#include "Ati3dCIFx.h"
#include "Matrix.h"
#include "Multimedia.h"

#include "AtiDemoWnd.h"
#include "AtiDemo.h"
#include "Polygon.h"
#include "shadow.h"

// -----------------------------------------------------------------------------

BOOL	Shadow::m_drawFace[MAX_CLIPPED_FACES];
BOOL	Shadow::m_xformVertex[MAX_CLIPPED_VERTICES];
Vertex	Shadow::m_xformedVertices[MAX_CLIPPED_VERTICES];
DWORD	Shadow::m_vertexMask[MAX_CLIPPED_VERTICES];

// -----------------------------------------------------------------------------

static double s_numerator;

BOOL Shadow::FastIntersect(float& t, const Normal& n, const Vector& delta)
{
	//s_numerator = -DotProduct(n, a); - Only done once per group
	double denominator = n.X()*delta.X() + n.Y()*delta.Y() + n.Z()*delta.Z();
	if(denominator == 0.0) return FALSE;
	t =  s_numerator / denominator;
	return TRUE;
}

Vector Shadow::Project(const Normal& n, const Vector& a, const Vector& b)
{
	double numerator = -DotProduct(n, a);
	double denominator = n.X()*(b.X()-a.X()) + n.Y()*(b.Y()-a.Y()) + n.Z()*(b.Z()-a.Z());
	if(denominator == 0.0) return a;
	float t =  numerator / denominator;
	return ((b - a) * t + a);
}

// -----------------------------------------------------------------------------

void Shadow::ComputeShadowPoly(const Poly& rPoly, const Normal& rNormal)
{
	ASSERT(rPoly.VertexCount() < MAX_CLIPPED_VERTICES);
	ASSERT(rPoly.FaceCount() < MAX_CLIPPED_FACES);

	for(int i=0; i<rPoly.VertexCount(); i++) m_xformVertex[i] = FALSE;

	for(i=0; i<rPoly.FaceCount(); i++)
	{
		const PolyFace* pFace = &rPoly.GetFace(i);
		if(g_gfxStack.IsFacingLight(pFace->normal))
		{
			m_drawFace[i] = TRUE;
			m_xformVertex[pFace->vertices[0]] = TRUE;
			m_xformVertex[pFace->vertices[1]] = TRUE;
			m_xformVertex[pFace->vertices[2]] = TRUE;
		}
		else
			m_drawFace[i] = FALSE;
	}

	for(i=0; i<rPoly.VertexCount(); i++)
		if(m_xformVertex[i])
		{
			Vector deltaA = rPoly.GetVertex(i).XYZ() - g_gfxStack.ObjectLight();
			float tA;
			if(!FastIntersect(tA, rNormal, deltaA)) return;
			m_xformedVertices[i].XYZ() = (deltaA * tA + g_gfxStack.ObjectLight());
		}
}

// -----------------------------------------------------------------------------

void Shadow::DropCustom(const Poly& rPoly, const Normal& rNormal, CustomShadowHandler* pFn)
{
	ComputeShadowPoly(rPoly, rNormal);
	(*pFn)(rPoly, rNormal, m_drawFace, m_xformVertex, m_xformedVertices);
}

void Shadow::DropClipped(const Poly& rPoly, const Vector& clipA, const Matrix& clipBasis, const Normal& rNormal)
{
	ComputeShadowPoly(rPoly, rNormal);

	// And now... El-Cheezo(tm) clipping!

	Matrix invBasis = Inverse(clipBasis);

	for(int i=0; i<rPoly.VertexCount(); i++)
		if(m_xformVertex[i])
		{
			Vector basis = invBasis * (m_xformedVertices[i].XYZ() - clipA);

			m_vertexMask[i] = 0;
			if(basis.X() < 0) m_vertexMask[i] |= 1;
			if(basis.X() > 1) m_vertexMask[i] |= 2;
			if(basis.Y() < 0) m_vertexMask[i] |= 4;
			if(basis.Y() > 1) m_vertexMask[i] |= 8;

			if(m_vertexMask[i])
			{
				if(m_vertexMask[i] & 1) basis.X() = 0;
				if(m_vertexMask[i] & 2) basis.X() = 1;
				if(m_vertexMask[i] & 4) basis.Y() = 0;
				if(m_vertexMask[i] & 8) basis.Y() = 1;
				m_xformedVertices[i].XYZ() = clipBasis * basis + clipA;
			}
		}

	for(i=0; i<rPoly.FaceCount(); i++)
		if(m_drawFace[i])
		{
			const PolyFace* pFace = &rPoly.GetFace(i);
			if(m_vertexMask[pFace->vertices[0]] & m_vertexMask[pFace->vertices[1]] & m_vertexMask[pFace->vertices[2]])
				m_drawFace[i] = FALSE;
		}

	StdDropFunction(rPoly, rNormal, m_drawFace, m_xformVertex, m_xformedVertices);
}

// -----------------------------------------------------------------------------

void Shadow::DropCustom(const Group& rGroup, const Normal& normal, CustomShadowHandler* pFn)
{
	Normal nObject = UntransformBy(normal, rGroup.RotationCopy(), rGroup.TranslationCopy());
	g_gfxStack.TransformObject(rGroup.RotationCopy(), rGroup.TranslationCopy());

	// for speedier intersections
	s_numerator = -DotProduct(nObject, g_gfxStack.ObjectLight());

	if(rGroup.Visible() && !rGroup.Hidden())
		for(PolyIterator iPoly = rGroup.PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
			Shadow::DropCustom(*iPoly.Current(), nObject, pFn);

	for(GroupIterator iGroup = rGroup.Node().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		Shadow::DropCustom(*iGroup.Current(), nObject, pFn);

	g_gfxStack.TransformDone();
}

void Shadow::DropClipped(const Group& rGroup, const Vector& clipA, const Matrix& clipBasis, const Normal& normal)
{
	Matrix invRot = Inverse(rGroup.RotationCopy());
	Normal nObject = UntransformByInverse(normal, invRot, rGroup.TranslationCopy());
	Vector clipAObject = UntransformByInverse(clipA, invRot, rGroup.TranslationCopy());
	Matrix clipBasisObject = invRot * clipBasis;

	g_gfxStack.TransformObject(rGroup.RotationCopy(), rGroup.TranslationCopy());

	// for speedier intersections
	s_numerator = -DotProduct(nObject, g_gfxStack.ObjectLight());

	if(rGroup.Visible() && !rGroup.Hidden())
		for(PolyIterator iPoly = rGroup.PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
			Shadow::DropClipped(*iPoly.Current(), clipAObject, clipBasisObject, nObject);

	for(GroupIterator iGroup = rGroup.Node().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		Shadow::DropClipped(*iGroup.Current(), clipAObject, clipBasisObject, nObject);

	g_gfxStack.TransformDone();
}

// -----------------------------------------------------------------------------

void Shadow::DropCustom(const Object& rObject, const Normal& normal, CustomShadowHandler* pFn)
{
	Normal nObject = UntransformBy(normal, Matrix(IDENTITY), rObject.Translation());
	g_gfxStack.TransformObject(Matrix(IDENTITY), rObject.Translation());

	for(GroupIterator iGroup = rObject.GroupHead().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		Shadow::DropCustom(*iGroup.Current(), nObject, pFn);

	g_gfxStack.TransformDone();
}



void Shadow::StdDropFunction(const Poly& rPoly, const Normal& rNormal, const BOOL* const pDrawFace, const BOOL* const pDrawVertex, const Vertex* const pVertices)
{
	if(g_gfxStack.IsFacing(rNormal))
	{
		TLVertex* pTLVertex = (TLVertex*)_alloca(sizeof(TLVertex) * rPoly.VertexCount());
	
		for(int i=0; i<rPoly.VertexCount(); i++)
			if(pDrawVertex[i])
				g_gfxStack.TransformPoint(&pTLVertex[i], (Vertex*)&pVertices[i], VERTEXTYPE_1);

		for(i=0; i<rPoly.FaceCount(); i++)
			if(pDrawFace[i])
			{
				const PolyFace* pFace = &rPoly.GetFace(i);
				ASSERT(pDrawVertex[pFace->vertices[0]]);
				ASSERT(pDrawVertex[pFace->vertices[1]]);
				ASSERT(pDrawVertex[pFace->vertices[2]]);
				g_clipper.DrawTriangle(pTLVertex[pFace->vertices[0]],
									   pTLVertex[pFace->vertices[1]],
									   pTLVertex[pFace->vertices[2]],
									   VERTEXTYPE_1);
			}
	}
}

void Shadow::Drop(const Object& rObject, const Normal& normal)
{
	DropCustom(rObject, normal, StdDropFunction);
}



void Shadow::DropClipped(const Object& rObject, const Vector& clipA, const Matrix& clipBasis, const Normal& normal)
{
//	ASSERT(DotProduct(normal, clipA) < 0.0001);

	Matrix invRot = Inverse(Matrix(IDENTITY));
	Normal nObject = UntransformByInverse(normal, invRot, rObject.TranslationCopy());
	Vector clipAObject = UntransformByInverse(clipA, invRot, rObject.TranslationCopy());
	Matrix clipBasisObject = invRot * clipBasis;
	g_gfxStack.TransformObject(Matrix(IDENTITY), rObject.TranslationCopy());

	for(GroupIterator iGroup = rObject.GroupHead().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		Shadow::DropClipped(*iGroup.Current(), clipAObject, clipBasisObject, nObject);

	g_gfxStack.TransformDone();
}

// -----------------------------------------------------------------------------
 