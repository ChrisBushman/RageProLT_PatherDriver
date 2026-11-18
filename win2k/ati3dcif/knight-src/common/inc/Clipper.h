/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	3D object clipping. Takes an object in 3D space, and clips each line 	   *
 *	segment based on which subsector of space each endpoint lies in. The 	   *
 *	planes clipped to are near, far, top, bottom, left and right sides of	   *
 *	the view frustrum.														   *
 *******************************************************************************/
#ifndef CLIPPER_H
#define CLIPPER_H
// -----------------------------------------------------------------------------

#include "Vertex.h"
#include "Polygon.h"

// -----------------------------------------------------------------------------

#define MAX_POLYGON_VERTICES 1000
#define MAX_SHAPE_VERTICES 50

// -----------------------------------------------------------------------------

class Clipper {
public:
	~Clipper() { }
	Clipper() { m_pXFormStack = NULL; m_pContext = NULL;  }

	XFormStack*& Stack(void) { return m_pXFormStack; }
	XFormStack* Stack(void) const { return m_pXFormStack; }
	Ati3dContext*& Context(void) { return m_pContext; }
	Ati3dContext* Context(void) const { return m_pContext; }

	TLVertex* ClipNear(const TLVertex& on, const TLVertex& off, VertexType type);
	TLVertex* ClipFar(const TLVertex& on, const TLVertex& off, VertexType type);
	TLVertex* ClipAbove(const TLVertex& on, const TLVertex& off, VertexType type);
	TLVertex* ClipBelow(const TLVertex& on, const TLVertex& off, VertexType type);
	TLVertex* ClipLeft(const TLVertex& on, const TLVertex& off, VertexType type);
	TLVertex* ClipRight(const TLVertex& on, const TLVertex& off, VertexType type);

	void DrawLine(Vertex& v0, Vertex& v1, VertexType type);
	void DrawLine(Vertex& v0, Vertex& v1, const RGBA& color);
	void DrawTriangle(Vertex& v0, Vertex& v1, Vertex& v2, VertexType type);
	void DrawTriangle(Vertex& v0, Vertex& v1, Vertex& v2, const RGBA& color);
	void DrawQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, VertexType type);
	void DrawQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, const RGBA& color);
	void DrawPolygon(Poly& polygon, VertexType type);

	void DrawTriangle(TLVertex& v0, TLVertex& v1, TLVertex& v2, VertexType type);

	void OutlineTriangle(Vertex& v0, Vertex& v1, Vertex& v2, const RGBA& color);
	void OutlineQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, const RGBA& color);
	void OutlinePolygon(Poly& polygon, const RGBA& selected, const RGBA& unselected);

protected:
private:
	XFormStack*	m_pXFormStack;
	Ati3dContext* m_pContext;

	TLVertex	m_tlVertices[MAX_POLYGON_VERTICES];
	int			m_nTLAlloc;
	void		InitAlloc(void);
	TLVertex*	Alloc(void) { ASSERT(m_nTLAlloc < MAX_POLYGON_VERTICES); return &m_tlVertices[m_nTLAlloc++]; }

	TLVertex*	m_pTLVertices[MAX_SHAPE_VERTICES];
	int			m_nTLVertices;

	void InitClipping(void);
	void SetupVertexMode(VertexType type);
	void DrawVertexBuffer(VertexType type);
	DWORD ClipVertexBufferAgainstEdge(DWORD edgemask, VertexType type, TLVertex* (Clipper::*pClipEdge)(const TLVertex& on, const TLVertex& off, VertexType type));
	void ClipVertexBuffer(VertexType type);
	void RemoveVertex(int index);
	void InsertVertex(int index, TLVertex* pVertex);

	TLVertex* ForceOn(TLVertex* p, VertexType type);

	Clipper(const Clipper& c);
	Clipper& operator=(const Clipper& c);
};

// -----------------------------------------------------------------------------

extern int g_trianglesDrawn;

// -----------------------------------------------------------------------------
#endif
