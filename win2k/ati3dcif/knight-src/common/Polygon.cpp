/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  Polygon face and vertex creation functionality							   *
 *******************************************************************************/
#include "stdwin.h"
#include "Polygon.h"
#include "Clipper.h"
#include "xform.h"
#include "intersect.h"

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

template<> PolyIterator* PolyIterator::m_pFirst = NULL;	// gcc: explicit specialization of template static. CWB

DEFINE_LISTHEAD(PolyHead, Poly, m_node);

// -----------------------------------------------------------------------------

Poly::~Poly(void)
{
	delete [] m_pFaces;
	m_pFaces = NULL;

	delete [] m_pVertices;
	m_pVertices = NULL;
}



Poly::Poly(PolyType type, int nVertices, int nFaces) : m_node()
{
	m_type = type;

	m_nVertices = nVertices;
	m_pVertices = NULL;

	m_nFaces = nFaces;
	m_pFaces = NULL;

	m_pVertices = new Vertex[m_nVertices];
	if(!m_pVertices) THROW_EXCEPTION();

	m_pFaces = new PolyFace[m_nFaces];
	if(!m_pFaces) THROW_EXCEPTION();
}

// -----------------------------------------------------------------------------

Object::~Object(void)
{
	for(GroupIterator iGroup=GroupHead().Iterator(); !iGroup.Done(); iGroup.Next())
	{
		Group* p = iGroup.Current();
		p->Node().Remove();
		delete p;
	}
}



Object::Object(void) : m_groupHead(), m_translation(ZERO), m_translationCopy(ZERO)
{
}

// -----------------------------------------------------------------------------

Group* Object::GroupNumber(int index)
{
	GroupIterator iGroup=GroupHead().Iterator();	// hoisted: used after loop (gcc for-scope). CWB
	for(; !iGroup.Done() && (index>0); iGroup.Next())
		index--;
	return iGroup.Current();
}

const Group* Object::GroupNumber(int index) const
{
	GroupIterator iGroup=GroupHead().Iterator();	// hoisted: used after loop. CWB
	for(; !iGroup.Done() && (index>0); iGroup.Next())
		index--;
	return iGroup.Current();
}

// -----------------------------------------------------------------------------
