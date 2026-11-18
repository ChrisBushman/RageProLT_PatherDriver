/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 *	The base sequence and set up for the pedestal the knight stands on to	   *	
 *	begin the demo. Includes initialization, drawing, shading and clean-up for * 	
 *	modeling of the knight's pedestal.										   *
 *******************************************************************************/
#ifndef PEDESTAL_H
#define PEDESTAL_H
// -----------------------------------------------------------------------------

#define PEDESTAL_MAX_SHADOW_POLYS	10

// -----------------------------------------------------------------------------

class Pedestal {
public:
	static void Cleanup(void) ;
	static void Initialize(void) ;

	static void Draw(Clipper& rClipper) ;
	static void DrawShadow(Clipper& rClipper) ;

	static ::Object*		Object(void) { return m_pObject; }
	static Ati3dTexture*	Texture(void) { return m_pTexture; }

	static const Vector&	Position(void) { return m_position; }
	static const Matrix&	Rotation(void) { return m_rotation; }

	static const Normal&	Top(void) { return m_top; }
	static const Vector&	Corner(void) { return m_corner; }
	static const Vector&	Vector0(void) { return m_vectors[0]; }
	static const Vector&	Vector1(void) { return m_vectors[1]; }

protected:
	static void PrecastShadow(void);

private:
	static Vector	m_position;
	static Matrix	m_rotation;

	static Normal	m_top;
	static Vector	m_corner;
	static Vector	m_vectors[2];

	static Poly*	m_pShadowPolys[PEDESTAL_MAX_SHADOW_POLYS];
	static int		m_nShadowPolys;

	static int		m_shadowVertexCount;
	static int		m_shadowFaceCount;
	static Vertex	m_shadowVertices[];
	static struct _FaceIndex {
		int v0, v1, v2;
	}				m_shadowFacelist[];

	static ::Object*		m_pObject;
	static Ati3dTexture*	m_pTexture;

	static void PrecastCallback(const Poly& rPoly, const Normal& rNormal, const BOOL* const pDrawFace, const BOOL* const pDrawVertex, const Vertex* const pVertices);
	static int  AddShadowVertex(const Vector& v);
	static void AddShadowFace(int a, int b, int c);
};

// -----------------------------------------------------------------------------
#endif
