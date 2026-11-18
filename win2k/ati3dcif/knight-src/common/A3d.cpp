/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  File handling utilities for reading and writing polygon data. 			   *
 *******************************************************************************/
#include "stdwin.h"
#include <stdarg.h>
#include <ctype.h>
#include "a3d.h"
#include "util.h"
#include "Matrix.h"
#include "Polygon.h"
#include "Watchers.h"

// -----------------------------------------------------------------------------

#define A3D_TAG_0	'A3D0'	// Important! Keep these numerically ordered (A3D_TAG_0 < A3D_TAG_1)
#define A3D_TAG_1	'A3D1'
#define A3D_TAG_2	'A3D2'
#define A3D_TAG_3	'A3D3'
#define A3D_TAG_4	'A3D4'
#define A3D_TAG_5	'A3D5'

// -----------------------------------------------------------------------------

static void WriteVertex(fileHandle& fh, const Vertex* pVertex) 
{
	ASSERT(pVertex);

	fh.writeReal32(pVertex->X());
	fh.writeReal32(pVertex->Y());
	fh.writeReal32(pVertex->Z());
	fh.writeReal32(pVertex->U());
	fh.writeReal32(pVertex->V());
	fh.writeReal32(pVertex->R());
	fh.writeReal32(pVertex->G());
	fh.writeReal32(pVertex->B());
	fh.writeReal32(pVertex->A());
	fh.writeReal32(pVertex->Normal().X());
	fh.writeReal32(pVertex->Normal().Y());
	fh.writeReal32(pVertex->Normal().Z());
	fh.writeReal32(pVertex->Normal().W());
}

// -----------------------------------------------------------------------------

static void WritePoly(fileHandle& fh, const Poly* pPoly) 
{
	ASSERT(pPoly);

	fh.writeUint32('POLY');

	switch(pPoly->Type())
	{
	case POLYGON_LIST: fh.writeInt32(0); break;
	case POLYGON_STRIP: fh.writeInt32(1); break;
	}

	fh.writeInt32(pPoly->VertexCount());
	fh.writeInt32(pPoly->FaceCount());

	fh.writeInt32('VERT');
	for(int i=0; i<pPoly->VertexCount(); i++)
		WriteVertex(fh, &pPoly->GetVertex(i));

	fh.writeInt32('FACE');
	if(pPoly->Type() == POLYGON_STRIP)
	{
		fh.writeInt32(pPoly->GetFace(0).vertices[0]);
		fh.writeInt32(pPoly->GetFace(0).vertices[1]);
	}

	for(i=0; i<pPoly->FaceCount(); i++)
	{
		switch(pPoly->Type())
		{
		case POLYGON_LIST:
			fh.writeInt32(pPoly->GetFace(i).vertices[0]);
			fh.writeInt32(pPoly->GetFace(i).vertices[1]);
			fh.writeInt32(pPoly->GetFace(i).vertices[2]);
			break;

		case POLYGON_STRIP:
			fh.writeInt32(pPoly->GetFace(i).vertices[2]);
			break;
		}

		fh.writeReal32(pPoly->GetFaceNormal(i).X());
		fh.writeReal32(pPoly->GetFaceNormal(i).Y());
		fh.writeReal32(pPoly->GetFaceNormal(i).Z());
		fh.writeReal32(pPoly->GetFaceNormal(i).W());
	}
}

// -----------------------------------------------------------------------------

static char zeros[65];



void A3DWrite(fileHandle& fh, const Object* pObject) 
{
	ASSERT(pObject);

	fh.writeInt32(A3D_TAG_5);
	fh.writeInt32(pObject->GroupCount());

	for(GroupIterator iGroup=pObject->GroupHead().Iterator(BREADTH_FIRST); !iGroup.Done(); iGroup.Next())
	{
		Group* pGroup=iGroup.Current();

		fh.writeInt32(pGroup->PolyCount());
		fh.write(pGroup->Name(), 64);
		fh.writeInt32(pGroup->Visible());

		if(pGroup->Node().Parent())
			fh.write(pGroup->Node().Parent()->Object()->Name(), 64);
		else
			fh.write(zeros, 64);

		fh.writeReal32(pGroup->Center().X());
		fh.writeReal32(pGroup->Center().Y());
		fh.writeReal32(pGroup->Center().Z());

		for(PolyIterator iPoly=pGroup->PolyHead().Iterator(); !iPoly.Done(); iPoly.Next())
			WritePoly(fh, iPoly.Current());
	}
}



void A3DWrite(char* filename, const Object* pObject) 
{
	ASSERT(filename);
	ASSERT(pObject);
	fileHandle fh(filename, "wb");
	A3DWrite(fh, pObject);
}

// -----------------------------------------------------------------------------

BOOL IsA3D(char* filename)
{
	ASSERT(filename);
	try
	{
		fileHandle fh(filename, "rb");
		return IsA3D(fh);
	}
	catch(...)
	{
		return FALSE;
	}
}



BOOL IsA3D(fileHandle& fh)
{
	try
	{
		uint32 code = fh.readUint32();
		if((code != A3D_TAG_0) &&
		   (code != A3D_TAG_1) &&
		   (code != A3D_TAG_2) &&
		   (code != A3D_TAG_3) &&
		   (code != A3D_TAG_4))
		   throw 0;
		return TRUE;
	}
	catch(...)
	{
		return FALSE;
	}
}

// -----------------------------------------------------------------------------

static void readVertex(Vertex* pVertex, fileHandle& fh, int code) 
{
	ASSERT(pVertex);
	ASSERT((code>=A3D_TAG_0) && (code<=A3D_TAG_5));

	(*pVertex).X() = fh.readReal32();
	(*pVertex).Y() = fh.readReal32();
	(*pVertex).Z() = fh.readReal32();
	(*pVertex).U() = fh.readReal32();
	(*pVertex).V() = fh.readReal32();
	(*pVertex).R() = fh.readReal32();
	(*pVertex).G() = fh.readReal32();
	(*pVertex).B() = fh.readReal32();
	(*pVertex).A() = fh.readReal32();
	(*pVertex).Normal().X() = fh.readReal32();
	(*pVertex).Normal().Y() = fh.readReal32();
	(*pVertex).Normal().Z() = fh.readReal32();
	(*pVertex).Normal().W() = fh.readReal32();
}

// -----------------------------------------------------------------------------

static void readPoly(Poly** ppPoly, fileHandle& fh, int code) 
{
	ASSERT(ppPoly);
	ASSERT((code>=A3D_TAG_0) && (code<=A3D_TAG_5));

	if(fh.readInt32() != 'POLY') THROW_EXCEPTION();

	int t = fh.readInt32();
	PolyType type;
	switch(t)
	{
	case 0: type = POLYGON_LIST; break;
	case 1: type = POLYGON_STRIP; break;
	default: THROW_EXCEPTION(); break;
	}

	int nVertices = fh.readInt32();
	int nFaces = fh.readInt32();

	(*ppPoly) = new Poly(type, nVertices, nFaces);
	if((*ppPoly) == NULL) THROW_EXCEPTION();

	if(fh.readInt32() != 'VERT') THROW_EXCEPTION();

	for(int index=0; index<nVertices; index++)
	{
		Vertex vertex;
		readVertex(&vertex, fh, code);
		(*ppPoly)->GetVertex(index) = vertex;
	}

	if(fh.readInt32() != 'FACE') THROW_EXCEPTION();

	int stripA;
	int stripB;
	if(type == POLYGON_STRIP)
	{
		stripA = fh.readInt32();
		stripB = fh.readInt32();
	}

	for(int f=0; f<nFaces; f++)
	{
		switch(type)
		{
		case POLYGON_LIST:
			(*ppPoly)->GetFace(f).vertices[0] = fh.readInt32();
			(*ppPoly)->GetFace(f).vertices[1] = fh.readInt32();
			(*ppPoly)->GetFace(f).vertices[2] = fh.readInt32();
			break;

		case POLYGON_STRIP:
			{
				int c = fh.readInt32();

				(*ppPoly)->GetFace(f).vertices[0] = stripA;
				(*ppPoly)->GetFace(f).vertices[1] = stripB;
				(*ppPoly)->GetFace(f).vertices[2] = c;

				stripA = stripB;
				stripB = c;
			}
			break;
		}

		(*ppPoly)->GetFaceNormal(f).X() = fh.readReal32();
		(*ppPoly)->GetFaceNormal(f).Y() = fh.readReal32();
		(*ppPoly)->GetFaceNormal(f).Z() = fh.readReal32();
		(*ppPoly)->GetFaceNormal(f).W() = fh.readReal32();
	}

	if(code == A3D_TAG_0)
	{
		// Version 0 had a problem where it would accidentally create extra points in a polygon
		int *pCount = new int[(*ppPoly)->VertexCount()];
		DECLARE_POINTER_DOER(int*, pCount, pCount);

		for(int i=0; i<(*ppPoly)->VertexCount(); i++)
			pCount[i]=0;

		for(i=0; i<(*ppPoly)->FaceCount(); i++)
		{
			pCount[(*ppPoly)->GetFace(i).vertices[0]]++;
			pCount[(*ppPoly)->GetFace(i).vertices[1]]++;
			pCount[(*ppPoly)->GetFace(i).vertices[2]]++;
		}

		int count=0;
		for(i=0; i<(*ppPoly)->VertexCount(); i++)
			if(pCount[i] > 0)
			{
				pCount[i] = count;
				count++;
			}
			else
			{
				pCount[i] = -1;
			}

		Poly* pPoly = new Poly((*ppPoly)->Type(), count, (*ppPoly)->FaceCount());
		int j;
		for(i=0, j=0; i<(*ppPoly)->VertexCount(); i++)
			if(pCount[i] != -1)
				pPoly->GetVertex(j++) = (*ppPoly)->GetVertex(i);
		for(i=0; i<(*ppPoly)->FaceCount(); i++)
		{
			pPoly->GetFace(i).vertices[0] = pCount[(*ppPoly)->GetFace(i).vertices[0]];
			pPoly->GetFace(i).vertices[1] = pCount[(*ppPoly)->GetFace(i).vertices[1]];
			pPoly->GetFace(i).vertices[2] = pCount[(*ppPoly)->GetFace(i).vertices[2]];
		}

		delete (*ppPoly);
		(*ppPoly) = pPoly;

		code = A3D_TAG_1;
	}

	if(code == A3D_TAG_1)
	{
		// Version 1 didn't copy the face normals... Oops!
		// At least I don't need to re-create the poly

		for(int i=0; i<(*ppPoly)->FaceCount(); i++)
		{
			(*ppPoly)->GetFaceNormal(i) = Normal((*ppPoly)->GetFaceVertex(i, 0).XYZ(),
												 (*ppPoly)->GetFaceVertex(i, 1).XYZ(),
												 (*ppPoly)->GetFaceVertex(i, 2).XYZ());
		}

		code = A3D_TAG_2;
	}

	if(code == A3D_TAG_2)
	{
		code = A3D_TAG_3;
	}

	if(code == A3D_TAG_3)
	{
		code = A3D_TAG_4;
	}

	if(code <= A3D_TAG_4)
	{
		Vector	v(ZERO);
		int		count = 0;
		for(int i=0; i<(*ppPoly)->VertexCount(); i++)
		{
			for(int j=0; j<(*ppPoly)->FaceCount(); j++)
			{
				for(int k=0; k<3; k++)
					if((*ppPoly)->GetFace(j).vertices[k] == i)
					{
						v += (*ppPoly)->GetFaceNormal(j).GetVector();
						count++;
					}
			}

			if(count)
			{
				v /= (float)count;
				(*ppPoly)->GetVertex(i).Normal().GetVector() = Normalize(v);
				(*ppPoly)->GetVertex(i).Normal().W() = -DotProduct(v, (*ppPoly)->GetVertex(i).XYZ());
			}
		}

		code = A3D_TAG_5;
	}
}

// -----------------------------------------------------------------------------

static void readGroup(Group** ppGroup, Object* pObject, fileHandle& fh, int code) 
{
	(*ppGroup) = new Group(DEFAULT_GROUP_NAME);

	int nPolys = fh.readInt32();

	if(code >= A3D_TAG_3)
	{
		fh.read((*ppGroup)->Name(), 64);
		(*ppGroup)->Name()[64] = 0;
		(*ppGroup)->Visible() = fh.readInt32();
	}

	Group* pParent = NULL;
	if(code >= A3D_TAG_4)
	{
		char parent[65];
		fh.read(parent, 64);
		parent[64] = 0;
		if(parent[0])
		{
			pParent = pObject->FindGroup(parent);
			if(!pParent) THROW_EXCEPTION();
		}

		(*ppGroup)->Center().X() = fh.readReal32();
		(*ppGroup)->Center().Y() = fh.readReal32();
		(*ppGroup)->Center().Z() = fh.readReal32();
	}

	if(pParent)
		pParent->Node().InsertChild((*ppGroup));
	else
		pObject->GroupHead().InsertChild((*ppGroup));


	while(nPolys--)
	{
		Poly* pPoly;
		readPoly(&pPoly, fh, code);
		(*ppGroup)->PolyHead().Insert(pPoly);
	}
}

// -----------------------------------------------------------------------------

void A3DRead(Object** ppObject, fileHandle& fh) 
{
	ASSERT(ppObject);

	BOOL successful = FALSE;

	int code = fh.readInt32();
	if((code<A3D_TAG_0) || (code>A3D_TAG_5)) THROW_EXCEPTION();

	(*ppObject) = new Object();
	if((*ppObject) == NULL) THROW_EXCEPTION();
	DECLARE_POINTER_WATCHER(Object*, (*ppObject), successful, ppObject);

	int nGroups = (code>=A3D_TAG_3)?fh.readInt32():1;

	for(int group=0; group<nGroups; group++)
	{
		Group* pGroup;
		readGroup(&pGroup, (*ppObject), fh, code);
	}

	successful = TRUE;
}

// -----------------------------------------------------------------------------

void A3DRead(Object** ppObject, char* filename) 
{
	ASSERT(ppObject);
	ASSERT(filename);

	fileHandle fh(filename, "rb");
	A3DRead(ppObject, fh);
}

// -----------------------------------------------------------------------------
