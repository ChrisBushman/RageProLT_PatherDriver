/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	3D object clipping. Takes an object in 3D space, and clips each line 	   *
 *	segment based on which subsector of space each endpoint lies in. The 	   *
 *	planes clipped to are near, far, top, bottom, left and right sides of	   *
 *	the view frustrum.														   *
 *******************************************************************************/
#include "stdwin.h"
#include <stdlib.h>
#include <malloc.h>
#include "clipper.h"
#include "xform.h"

// -----------------------------------------------------------------------------

int g_trianglesDrawn;

// -----------------------------------------------------------------------------

static inline double Interpolate(double a, double b, double t) { return (a - b) * t + b; }

// -----------------------------------------------------------------------------

#define DO_INTERPOLATION()							\
	if(IsTextured(type))							\
	{												\
		p->U() = Interpolate(off.U(), on.U(), t);	\
		p->V() = Interpolate(off.V(), on.V(), t);	\
	}												\
													\
	if(IsColored(type))								\
	{												\
		p->R() = Interpolate(off.R(), on.R(), t);	\
		p->G() = Interpolate(off.G(), on.G(), t);	\
		p->B() = Interpolate(off.B(), on.B(), t);	\
		p->A() = Interpolate(off.A(), on.A(), t);	\
	}


TLVertex* Clipper::ClipNear(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT(off.Z() != on.Z());
	ASSERT(!IsNear(on));
	ASSERT(IsNear(off));

	double t=(1.0 - on.Z()) / (off.Z() - on.Z());

	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = 1.0;

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);

	return p;
}



TLVertex* Clipper::ClipFar(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT(off.Z() != on.Z());
	ASSERT(!IsFar(on));
	ASSERT(IsFar(off));

	double t=(m_pXFormStack->FarClipPlane() - on.Z()) / (off.Z() - on.Z());

	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = m_pXFormStack->FarClipPlane();

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);


	return p;
}



TLVertex* Clipper::ClipAbove(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT((off.Y()-on.Y()) != (off.Z()-on.Z()));
	ASSERT(!IsAbove(on));
	ASSERT(IsAbove(off));

	double t=(on.Z() - on.Y()) / ((off.Y() - on.Y()) - (off.Z() - on.Z()));
	
	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = p->Y();

	// Handle rounding errors
	if(p->Z() < 1.0) p->Z() = 1.0;
	if(p->Z() > m_pXFormStack->FarClipPlane()) p->Z() = m_pXFormStack->FarClipPlane();

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);

	return p;
}



TLVertex* Clipper::ClipBelow(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT((off.Y()-on.Y()) != -(off.Z()-on.Z()));
	ASSERT(!IsBelow(on));
	ASSERT(IsBelow(off));

	double t=(-on.Z() - on.Y()) / ((off.Y() - on.Y()) + (off.Z() - on.Z()));

	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = -p->Y();

	// Handle rounding errors
	if(p->Z() < 1.0) p->Z() = 1.0;
	if(p->Z() > m_pXFormStack->FarClipPlane()) p->Z() = m_pXFormStack->FarClipPlane();

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);

	return p;
}



TLVertex* Clipper::ClipLeft(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT((off.X()-on.X()) != -(off.Z()-on.Z()));
	ASSERT(!IsLeft(on));
	ASSERT(IsLeft(off));

	double t=(-on.Z() - on.X()) / ((off.X() - on.X()) + (off.Z() - on.Z()));

	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = -p->X();

	// Handle rounding errors
	if(p->Z() < 1.0) p->Z() = 1.0;
	if(p->Z() > m_pXFormStack->FarClipPlane()) p->Z() = m_pXFormStack->FarClipPlane();

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);

	return p;
}



TLVertex* Clipper::ClipRight(const TLVertex& on, const TLVertex& off, VertexType type)
{
	TLVertex* p=Alloc();

	ASSERT((off.X()-on.X()) != (off.Z()-on.Z()));
	ASSERT(!IsRight(on));
	ASSERT(IsRight(off));

	double t=(on.Z() - on.X()) / ((off.X() - on.X()) - (off.Z() - on.Z()));

	p->X() = Interpolate(off.X(), on.X(), t);
	p->Y() = Interpolate(off.Y(), on.Y(), t);
	p->Z() = p->X();

	// Handle rounding errors
	if(p->Z() < 1.0) p->Z() = 1.0;
	if(p->Z() > m_pXFormStack->FarClipPlane()) p->Z() = m_pXFormStack->FarClipPlane();

	DO_INTERPOLATION();

	if(p->ComputeClipCode(m_pXFormStack->FarClipPlane()) == CC_ON)
		Stack()->ComputeScreenCoordinate(p, type);

	return p;
}

// -----------------------------------------------------------------------------

TLVertex* Clipper::ForceOn(TLVertex* p, VertexType type)
{
	if(!IsOn(p))
	{
		p->ClipCode() = 0;
		Stack()->ComputeScreenCoordinate(p, type);
	}

	return p;
}

// -----------------------------------------------------------------------------

static int triangleSize(C3D_VTCF& v0, C3D_VTCF& v1, C3D_VTCF& v2)
{
	return abs((int)(v0.x*v1.y-v0.y*v1.x + v1.x*v2.y-v1.y*v2.x+v2.x*v0.y-v2.y*v0.x))/2;
}

// -----------------------------------------------------------------------------

void Clipper::InitAlloc(void)
{
	m_nTLAlloc = 0;
}

void Clipper::InitClipping(void)
{
#ifdef _DEBUG
	for(int i=0; i<MAX_SHAPE_VERTICES; i++)
		m_pTLVertices[i] = NULL;
#endif
}

// -----------------------------------------------------------------------------

void Clipper::SetupVertexMode(VertexType type)
{
	switch(type)
	{
	case VERTEXTYPE_1:
		m_pContext->SetTextureMode(FALSE);
		m_pContext->SetShadeMode(C3D_ESH_SOLID);
		break;

	case VERTEXTYPE_C:
		m_pContext->SetTextureMode(FALSE);
		m_pContext->SetShadeMode(C3D_ESH_SMOOTH);
		break;

	case VERTEXTYPE_T:
		m_pContext->SetTextureMode(TRUE);
		m_pContext->SetLightingMode(C3D_ETL_NONE);
		m_pContext->SetShadeMode(C3D_ESH_SOLID);
		break;

	case VERTEXTYPE_TC:
		m_pContext->SetTextureMode(TRUE);
		m_pContext->SetLightingMode(C3D_ETL_MODULATE);
		m_pContext->SetShadeMode(C3D_ESH_SMOOTH);
		break;
	}
}

// -----------------------------------------------------------------------------

void Clipper::RemoveVertex(int index)
{
	ASSERT(m_nTLVertices > 0);
	for(int i=index; i<m_nTLVertices-1; i++)
		m_pTLVertices[i] = m_pTLVertices[i+1];
	m_nTLVertices--;
}

void Clipper::InsertVertex(int index, TLVertex* pVertex)
{
	ASSERT(m_nTLVertices < MAX_SHAPE_VERTICES);
	for(int i=m_nTLVertices; i>index; i--)
		m_pTLVertices[i] = m_pTLVertices[i-1];
	m_pTLVertices[index] = pVertex;
	m_nTLVertices++;
}

// -----------------------------------------------------------------------------

DWORD Clipper::ClipVertexBufferAgainstEdge(DWORD edgemask, VertexType type, TLVertex* (Clipper::*pClipEdge)(const TLVertex& on, const TLVertex& off, VertexType type))
{
	if(m_nTLVertices == 1)
	{
		if((m_pTLVertices[0]->ClipCode() & edgemask) != CC_ON)
			m_nTLVertices = 0;
		return 0;
	}

	DWORD or = 0;

	TLVertex* pStart = m_pTLVertices[0];
	int index=0;
	while(index<m_nTLVertices-1)
	{
		if(m_pTLVertices[index]->ClipCode() & edgemask)	// OFF -> ?
		{
			if(m_pTLVertices[index+1]->ClipCode() & edgemask)	// OFF -> OFF
				RemoveVertex(index);
			else											// OFF -> ON
			{
				m_pTLVertices[index] = (this->*pClipEdge)(*m_pTLVertices[index+1], *m_pTLVertices[index], type);
				or |= m_pTLVertices[index]->ClipCode();
				index++;
			}
		}
		else											// ON -> ?
		{
			or |= m_pTLVertices[index]->ClipCode();
			if(m_pTLVertices[index+1]->ClipCode() & edgemask) // ON -> OFF
			{
				InsertVertex(index+1, (this->*pClipEdge)(*m_pTLVertices[index], *m_pTLVertices[index+1], type));
				or |= m_pTLVertices[index+1]->ClipCode();
				index+=2;
			}
			else											// ON -> ON
				index++;
		}
	}

	if(m_pTLVertices[index]->ClipCode() & edgemask)	// OFF -> ?
	{
		if(pStart->ClipCode() & edgemask)				// OFF -> OFF
			RemoveVertex(index);
		else											// OFF -> ON
		{
			m_pTLVertices[index] = (this->*pClipEdge)(*pStart, *m_pTLVertices[index], type);
			or |= m_pTLVertices[index]->ClipCode();
		}
	}
	else											// ON -> ?
	{
		or |= m_pTLVertices[index]->ClipCode();
		if(pStart->ClipCode() & edgemask)				// ON -> OFF
		{
			InsertVertex(index+1, (this->*pClipEdge)(*m_pTLVertices[index], *pStart, type));
			or |= m_pTLVertices[index+1]->ClipCode();
		}
		else											// ON -> ON
			;
	}

	return or;
}



void Clipper::ClipVertexBuffer(VertexType type)
{
	if(m_nTLVertices == 0) return;

	DWORD and = CC_NEAR | CC_FAR | CC_LEFT | CC_RIGHT | CC_ABOVE | CC_BELOW | CC_ON;
	DWORD or = 0;
	for(int i=0; i<m_nTLVertices; i++)
	{
		DWORD cc = m_pTLVertices[i]->ClipCode();
		and &= cc;
		or |= cc;
	}

	if(and)
	{
		m_nTLVertices = 0;
		return;		// Trivial reject
	}

	if(or)				// Needs clipping
	{
		if(or & CC_NEAR)
			or = ClipVertexBufferAgainstEdge(CC_NEAR, type, &Clipper::ClipNear);

		if(m_nTLVertices == 0) return;
		if(or & CC_FAR)
			or = ClipVertexBufferAgainstEdge(CC_FAR, type, &Clipper::ClipFar);

		if(m_nTLVertices == 0) return;
		if(or & CC_LEFT)
			or = ClipVertexBufferAgainstEdge(CC_LEFT, type, &Clipper::ClipLeft);

		if(m_nTLVertices == 0) return;
		if(or & CC_RIGHT)
			or = ClipVertexBufferAgainstEdge(CC_RIGHT, type, &Clipper::ClipRight);

		if(m_nTLVertices == 0) return;
		if(or & CC_ABOVE)
			or = ClipVertexBufferAgainstEdge(CC_ABOVE, type, &Clipper::ClipAbove);

		if(m_nTLVertices == 0) return;
		if(or & CC_BELOW)
			or = ClipVertexBufferAgainstEdge(CC_BELOW, type, &Clipper::ClipBelow);

		if(or)
			for(i=0; i<m_nTLVertices; i++)
				ForceOn(m_pTLVertices[i], type);
	}
}

// -----------------------------------------------------------------------------

void Clipper::DrawVertexBuffer(VertexType type)
{
	if(m_nTLVertices < 3) return;

	m_pContext->SetPrimType(C3D_EPRIM_TRI);
	SetupVertexMode(type);

	static C3D_VTCF*	pVTCFArray[MAX_SHAPE_VERTICES];
	static unsigned int	pFaceArray[] = {
		0, 1, 2,  0, 2, 3,  0, 3, 4,  0, 4, 5,  0, 5, 6,  0, 6, 7,  0, 7, 8,  0, 8, 9,  0, 9, 10,  
	};

	for(int i=0; i<m_nTLVertices; i++)
		pVTCFArray[i] = &m_pTLVertices[i]->VTCF();

	int nFaces = m_nTLVertices-2;

	ASSERT(nFaces*3 <= (sizeof(pFaceArray)/sizeof(unsigned int)));

	for(i=0; i<nFaces; i++)
	{
		pFaceArray[i*3+0] = 0;
		pFaceArray[i*3+1] = i+1;
		pFaceArray[i*3+2] = i+2;
	}

#if 0
	m_pContext->RenderPrimMesh((void**)pVTCFArray, pFaceArray, nFaces);
#else
	for(i=0; i<nFaces; i++)
		m_pContext->RenderPrimListVa(3,
									 pVTCFArray[pFaceArray[i*3+0]],
									 pVTCFArray[pFaceArray[i*3+1]],
									 pVTCFArray[pFaceArray[i*3+2]]);
#endif

	g_trianglesDrawn += nFaces;
}

// -----------------------------------------------------------------------------

void Clipper::DrawLine(Vertex& v0, Vertex& v1, VertexType type)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	m_pTLVertices[0] = m_pXFormStack->TransformPoint(Alloc(), &v0, type);
	m_pTLVertices[1] = m_pXFormStack->TransformPoint(Alloc(), &v1, type);
	m_nTLVertices = 2;

	ClipVertexBuffer(VERTEXTYPE_1);

	if(m_nTLVertices < 2) return;

	ASSERT((m_nTLVertices==2) || (m_nTLVertices==3));
	if(m_nTLVertices==3) m_nTLVertices--;

	SetupVertexMode(type);
	m_pContext->SetPrimType(C3D_EPRIM_LINE);

	m_pContext->RenderPrimListVa(2,
								 &m_pTLVertices[0]->VTCF(),
								 &m_pTLVertices[m_nTLVertices-1]->VTCF());
}



void Clipper::DrawTriangle(Vertex& v0, Vertex& v1, Vertex& v2, VertexType type)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	InitAlloc();
	InitClipping();

	m_pTLVertices[0] = m_pXFormStack->TransformPoint(Alloc(), &v0, type);
	m_pTLVertices[1] = m_pXFormStack->TransformPoint(Alloc(), &v1, type);
	m_pTLVertices[2] = m_pXFormStack->TransformPoint(Alloc(), &v2, type);
	m_nTLVertices = 3;
	
	ClipVertexBuffer(type);
	DrawVertexBuffer(type);
}



void Clipper::DrawTriangle(TLVertex& v0, TLVertex& v1, TLVertex& v2, VertexType type)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	InitAlloc();
	InitClipping();

	m_pTLVertices[0] = &v0;
	m_pTLVertices[1] = &v1;
	m_pTLVertices[2] = &v2;
	m_nTLVertices = 3;
	
	ClipVertexBuffer(type);
	DrawVertexBuffer(type);
}



void Clipper::DrawQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, VertexType type)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	InitAlloc();
	InitClipping();

	m_pTLVertices[0] = m_pXFormStack->TransformPoint(Alloc(), &v0, type);
	m_pTLVertices[1] = m_pXFormStack->TransformPoint(Alloc(), &v1, type);
	m_pTLVertices[2] = m_pXFormStack->TransformPoint(Alloc(), &v2, type);
	m_pTLVertices[3] = m_pXFormStack->TransformPoint(Alloc(), &v3, type);
	m_nTLVertices = 4;
	
	ClipVertexBuffer(type);
	DrawVertexBuffer(type);
}

// -----------------------------------------------------------------------------

void Clipper::DrawPolygon(Poly& polygon, VertexType type)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	TLVertex** ppTLVertexList = (TLVertex**)_alloca(sizeof(TLVertex*) * polygon.VertexCount());

	InitAlloc();

	for(int i=0; i<polygon.VertexCount(); i++)
		ppTLVertexList[i] = m_pXFormStack->TransformPoint(Alloc(), &polygon.GetVertex(i), type);

	for(i=0; i<polygon.FaceCount(); i++)
	{
		PolyFace* pFace = &polygon.GetFace(i);
		if(!(ppTLVertexList[pFace->vertices[0]]->ClipCode() &
		     ppTLVertexList[pFace->vertices[1]]->ClipCode() &
		     ppTLVertexList[pFace->vertices[2]]->ClipCode()))
		{
			if(m_pXFormStack->IsFacing(pFace->normal))
			{
				InitClipping();

				ASSERT(pFace->vertices[0] < polygon.VertexCount());
				ASSERT(pFace->vertices[1] < polygon.VertexCount());
				ASSERT(pFace->vertices[2] < polygon.VertexCount());

				m_pTLVertices[0] = ppTLVertexList[pFace->vertices[0]];
				m_pTLVertices[1] = ppTLVertexList[pFace->vertices[1]];
				m_pTLVertices[2] = ppTLVertexList[pFace->vertices[2]];
				m_nTLVertices = 3;

				ClipVertexBuffer(type);
				DrawVertexBuffer(type);
			}
		}
	}
}

// -----------------------------------------------------------------------------

void Clipper::DrawLine(Vertex& v0, Vertex& v1, const RGBA& color)
{
	ASSERT(m_pContext);
	m_pContext->SetSolidColor(color.R(), color.G(), color.B());
	DrawLine(v0, v1, VERTEXTYPE_1);
}



void Clipper::DrawTriangle(Vertex& v0, Vertex& v1, Vertex& v2, const RGBA& color)
{
	ASSERT(m_pContext);
	m_pContext->SetSolidColor(color.R(), color.G(), color.B());
	DrawTriangle(v0, v1, v2, VERTEXTYPE_1);
}

void Clipper::DrawQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, const RGBA& color)
{
	ASSERT(m_pContext);
	m_pContext->SetSolidColor(color.R(), color.G(), color.B());
	DrawQuad(v0, v1, v2, v3, VERTEXTYPE_1);
}

// -----------------------------------------------------------------------------

void Clipper::OutlineTriangle(Vertex& v0, Vertex& v1, Vertex& v2, const RGBA& color)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	m_pContext->SetSolidColor(color.R(), color.G(), color.B());

	DrawLine(v0, v1, VERTEXTYPE_1);
	DrawLine(v1, v2, VERTEXTYPE_1);
	DrawLine(v2, v0, VERTEXTYPE_1);
}



void Clipper::OutlineQuad(Vertex& v0, Vertex& v1, Vertex& v2, Vertex& v3, const RGBA& color)
{
	ASSERT(m_pXFormStack);
	ASSERT(m_pContext);

	m_pContext->SetSolidColor(color.R(), color.G(), color.B());

	DrawLine(v0, v1, VERTEXTYPE_1);
	DrawLine(v1, v2, VERTEXTYPE_1);
	DrawLine(v2, v3, VERTEXTYPE_1);
	DrawLine(v3, v0, VERTEXTYPE_1);
}



void Clipper::OutlinePolygon(Poly& polygon, const RGBA& selected, const RGBA& unselected)
{
	for(int i=0; i<polygon.FaceCount(); i++)
		if(!polygon.GetFace(i).selected)
			OutlineTriangle(polygon.GetFaceVertex(i, 0),
							polygon.GetFaceVertex(i, 1),
							polygon.GetFaceVertex(i, 2),
							unselected);

	for(i=0; i<polygon.FaceCount(); i++)
		if(polygon.GetFace(i).selected)
			OutlineTriangle(polygon.GetFaceVertex(i, 0),
							polygon.GetFaceVertex(i, 1),
							polygon.GetFaceVertex(i, 2),
							selected);
}

// -----------------------------------------------------------------------------
