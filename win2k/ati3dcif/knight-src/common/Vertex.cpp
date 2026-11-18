/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Linked list maintenance for vertex list functionality used by the 		   *
 *  tranformations module.													   *
 *******************************************************************************/
#include "stdwin.h"
#include "vertex.h"

// -----------------------------------------------------------------------------

Vertex::Vertex(void) : m_xyz(UNINITIALIZED)
{
}

Vertex::Vertex(const Vector& xyz) : m_xyz(xyz)
{
}

Vertex::Vertex(const ::UV& uv) : m_xyz(UNINITIALIZED), m_uv(uv)
{
}

Vertex::Vertex(const Vector& xyz, const ::UV& uv) : m_xyz(xyz), m_uv(uv)
{
}

Vertex::Vertex(const ::RGBA& rgba) : m_xyz(UNINITIALIZED), m_rgba(rgba)
{ 
}

Vertex::Vertex(const Vector& xyz, const ::RGBA& rgba) : m_xyz(xyz), m_rgba(rgba)
{ 
}

Vertex::Vertex(const ::UV& uv, const ::RGBA& rgba) : m_xyz(UNINITIALIZED), m_uv(uv), m_rgba(rgba)
{ 
}

Vertex::Vertex(const Vector& xyz, const ::UV& uv, const ::RGBA& rgba) : m_xyz(xyz), m_uv(uv), m_rgba(rgba)
{
}

Vertex::Vertex(const Vector& xyz, const ::Normal& n, const ::UV& uv, const ::RGBA& rgba) : m_xyz(xyz), m_normal(n), m_uv(uv), m_rgba(rgba)
{
}

// -----------------------------------------------------------------------------

TLVertex::~TLVertex(void)
{
}



TLVertex::TLVertex(void) : m_xyz(UNINITIALIZED)
{
}

// -----------------------------------------------------------------------------
