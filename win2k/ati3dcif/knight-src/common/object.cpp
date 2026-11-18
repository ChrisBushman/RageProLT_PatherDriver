/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Object management. Vertex, face, polygon and object tracking, selection	   *
 *	and drawing functionality.												   *
 *******************************************************************************/
#include "stdwin.h"
#include "Polygon.h"
#include "Clipper.h"
#include "xform.h"
#include "intersect.h"

// -----------------------------------------------------------------------------

Group* Object::FindGroup(char* name) const
{
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		if(!strcmp(iGroup.Current()->Name(), name))
			return iGroup.Current();
	return NULL;
}



Group* Object::FindGroup(Poly* pPoly) const
{
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		if(iGroup.Current()->Contains(pPoly))
			return iGroup.Current();
	return NULL;
}

// -----------------------------------------------------------------------------

int Object::GroupCount(void) const
{
	int total=0;
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		total++;
	return total;
}

int Object::PolyCount(void) const
{
	int total=0;
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		total += iGroup.Current()->PolyCount();
	return total;
}

int Object::FaceCount(void) const
{
	int total=0;
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		total += iGroup.Current()->FaceCount();
	return total;
}

int Object::VertexCount(void) const
{
	int total=0;
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		total += iGroup.Current()->VertexCount();
	return total;
}

// -----------------------------------------------------------------------------

void Object::Draw(Clipper& clipper, VertexType type)
{
	clipper.Stack()->TransformObject(Matrix(IDENTITY), TranslationCopy());

	for(GroupIterator iGroup=GroupHead().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
		iGroup.Current()->Draw(clipper, type);

	clipper.Stack()->TransformDone();
}

// -----------------------------------------------------------------------------

BOOL Object::Select(float* pT, Group** ppGroup, Poly** ppPoly, int* pFace,
					const POINT& pt, XFormStack& gfxStack, BOOL facingOnly)
{
	ASSERT(pT);
	ASSERT(ppGroup);
	ASSERT(ppPoly);
	ASSERT(pFace);

	BOOL result = FALSE;
	(*pT) = 1.1;
	(*ppGroup) = NULL;
	(*ppPoly) = NULL;
	(*pFace) = -1;

	gfxStack.TransformObject(Matrix(IDENTITY), Translation());

	for(GroupIterator iGroup=GroupHead().Iterator(PARENT_FIRST); !iGroup.Done(); iGroup.SkipChildren())
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

	return result;
}

// -----------------------------------------------------------------------------

void Object::Latch(void)
{
	m_translationCopy = m_translation;

	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
		iGroup.Current()->Latch();
}

// -----------------------------------------------------------------------------
