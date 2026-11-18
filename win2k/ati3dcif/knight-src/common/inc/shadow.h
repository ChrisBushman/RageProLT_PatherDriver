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
#ifndef SHADOW_H
#define SHADOW_H
// -----------------------------------------------------------------------------

#define MAX_CLIPPED_VERTICES	100
#define MAX_CLIPPED_FACES		100

// -----------------------------------------------------------------------------

typedef void CustomShadowHandler(const Poly& rPoly, const Normal& rNormal, const BOOL* const pDrawFace, const BOOL* const pDrawVertex, const Vertex* const pVertices);

// -----------------------------------------------------------------------------

class Shadow {
public:
	static void Drop(const Object& rObject, const Normal& normal);

	static void DropClipped(const Poly& rPoly, const Vector& clipA, const Matrix& clipBasis, const Normal& normal);
	static void DropClipped(const Group& rGroup, const Vector& clipA, const Matrix& clipBasis, const Normal& normal);
	static void DropClipped(const Object& rObject, const Vector& clipA, const Matrix& clipBasis, const Normal& normal);

	static void DropCustom(const Poly& rPoly, const Normal& rNormal, CustomShadowHandler* pFn);
	static void DropCustom(const Group& rGroup, const Normal& rNormal, CustomShadowHandler* pFn);
	static void DropCustom(const Object& rObject, const Normal& rNormal, CustomShadowHandler* pFn);

	static Vector Project(const Normal& n, const Vector& a, const Vector& b);

protected:
	static BOOL FastIntersect(float& t, const Normal& n, const Vector& delta);
	static void ComputeShadowPoly(const Poly& rPoly, const Normal& rNormal);

private:
	~Shadow(void);		// not allowed
	Shadow(void);		// not allowed

	static BOOL		m_drawFace[MAX_CLIPPED_FACES];
	static BOOL		m_xformVertex[MAX_CLIPPED_VERTICES];
	static Vertex	m_xformedVertices[MAX_CLIPPED_VERTICES];
	static DWORD	m_vertexMask[MAX_CLIPPED_VERTICES];

	static void StdDropFunction(const Poly& rPoly, const Normal& rNormal, const BOOL* const pDrawFace, const BOOL* const pDrawVertex, const Vertex* const pVertices);
};

// -----------------------------------------------------------------------------
#endif
