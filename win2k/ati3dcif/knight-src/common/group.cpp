/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Object hierarchy management functionality.								   *
 *******************************************************************************/
#include "stdwin.h"
#include "Polygon.h"
#include "Clipper.h"
#include "xform.h"
#include "intersect.h"

// -----------------------------------------------------------------------------

template<> GroupIterator* GroupIterator::m_pFirst = NULL;	// gcc: explicit specialization of template static. CWB

DEFINE_TREENODE(GroupHead, Group, m_node);

// -----------------------------------------------------------------------------

Group::~Group(void)
{
//	ASSERT(m_pNextGroup == NULL);
//	ASSERT(m_pPrevGroup == NULL);

	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
	{
		Poly* pPoly = iPoly.Current();
		pPoly->Node().Remove();
		delete pPoly;
	}
}



Group::Group(char* name) : m_node(), m_polyHead(), 
							m_center(ZERO),
							m_rotation(ZERO), m_rotationCopy(ZERO),
							m_translation(ZERO), m_translationCopy(ZERO)
{
	strncpy(m_name, name, 64);
	m_name[64] = 0;

	m_visible = TRUE;
	m_hidden = FALSE;
}

// -----------------------------------------------------------------------------

int Group::PolyCount(void) const
{
	int total = 0;
	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
		total++;
	return total;
}

int Group::FaceCount(void) const
{
	int total = 0;
	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
		total += iPoly.Current()->FaceCount();
	return total;
}

int Group::VertexCount(void) const
{
	int total = 0;
	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
		total += iPoly.Current()->VertexCount();
	return total;
}

// -----------------------------------------------------------------------------

BOOL Group::Contains(Poly* pPoly) const
{
	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
		if(iPoly.Current()==pPoly)
			return TRUE;
	return FALSE;
}

// -----------------------------------------------------------------------------

void Group::Draw(Clipper& clipper, VertexType type)
{
	clipper.Stack()->TransformObject(RotationCopy(), TranslationCopy());

	if(m_visible && !m_hidden)
	{
		for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
			clipper.DrawPolygon(*iPoly.Current(), type);
	}

	if(Node().Child())
		for(GroupIterator iGroup=Node().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
			iGroup.Current()->Draw(clipper, type);

	clipper.Stack()->TransformDone();
}



BOOL Group::Select(float* pT, Group** ppGroup, Poly** ppPoly, int* pFace, const POINT& pt, XFormStack& gfxStack, BOOL facingOnly)
{
	ASSERT(pT);
	ASSERT(ppPoly);
	ASSERT(pFace);

	(*pT) = 1.1;
	BOOL result = FALSE;
	(*ppPoly) = NULL;
	(*pFace) = -1;

	gfxStack.TransformObject(Matrix(IDENTITY), Center());
	gfxStack.TransformObject(Matrix(ROTATION, Rotation()), Vector(ZERO));
	gfxStack.TransformObject(Matrix(IDENTITY), -Center());

	Vector vNear(UNINITIALIZED);
	Vector vFar(UNINITIALIZED);
	gfxStack.InvertScreenCoordinate(&vNear, &vFar, pt);

	for(PolyIterator iPoly=PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
	{
		Poly* pPoly = iPoly.Current();
		for(int face=0; face<pPoly->FaceCount(); face++)
			if((!facingOnly) || gfxStack.IsFacing(pPoly->GetFaceNormal(face)))
			{
				Vector v0 = pPoly->GetFaceVertex(face, 0).XYZ();
				Vector v1 = pPoly->GetFaceVertex(face, 1).XYZ();
				Vector v2 = pPoly->GetFaceVertex(face, 2).XYZ();

				float distance;
				if(IntersectTriangleSegment(&distance, v0, v1, v2, vNear, vFar))
					if((distance < (*pT)) && (distance>=0.0) && (distance <=1.0))
					{
						result = TRUE;
						(*pT) = distance;
						(*ppPoly) = pPoly;
						(*pFace) = face;
						(*ppGroup) = this;
					}
			}
	}

	if(Node().Child())
		for(GroupIterator iGroup=Node().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		{
			Group* pGroup = iGroup.Current();

			float	t;
			Poly*	pPoly;
			int		face;
			Group*	pGroup1;
			if(pGroup->Visible())
			{
				if(!facingOnly  || !pGroup->Hidden())
					if(pGroup->Select(&t, &pGroup1, &pPoly, &face, pt, gfxStack, facingOnly))
						if((t>=0.0) && (t<=1.0) && (t < (*pT)))
						{
							result = TRUE;
							(*pT) = t;
							(*ppGroup) = pGroup1;
							(*ppPoly) = pPoly;
							(*pFace) = face;
						}
			}
		}

	gfxStack.TransformDone();
	gfxStack.TransformDone();
	gfxStack.TransformDone();

	return result;
}

// -----------------------------------------------------------------------------
