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
#ifndef VERTEX_H
#define VERTEX_H
// -----------------------------------------------------------------------------

#include "Ati3dCIFx.h"
#include "Matrix.h"

class XFormStack;
class Vertex;
class TLVertex;

// -----------------------------------------------------------------------------

#define NUMBER_OF_VERTICES 20000

// -----------------------------------------------------------------------------

class RGBA {
public:
	~RGBA(void) { }
	RGBA(void) { }
	RGBA(const RGBA& c) { r=c.r; g=c.g; b=c.b; a=c.a; }
	RGBA& operator=(const RGBA& c) { r=c.r; g=c.g; b=c.b; a=c.a; return *this; }

	RGBA(float r, float g, float b, float a=255) { this->r=r; this->g=g; this->b=b; this->a=a; }

	float& R(void) { return r; }
	float& G(void) { return g; }
	float& B(void) { return b; }
	float& A(void) { return a; }

	float R(void) const { return r; }
	float G(void) const { return g; }
	float B(void) const { return b; }
	float A(void) const { return a; }

protected:
private:
	float r;
	float g;
	float b;
	float a;
};



inline RGBA operator* (const RGBA& c, float scalar) { RGBA r; r.R()=c.R() * scalar; r.G()=c.G() * scalar; r.B()=c.B() * scalar; return r; }

// -----------------------------------------------------------------------------

class UV {
public:
	~UV(void) { }
	UV(void) { }
	UV(float u, float v) { m_u = u; m_v = v; }
	UV(const UV& uv) { U()=uv.U(); V()=uv.V(); }
	UV& operator=(const UV& uv) { U()=uv.U(); V()=uv.V(); return *this; }

	float& U(void) { return m_u; }
	float& V(void) { return m_v; }

	float U(void) const { return m_u; }
	float V(void) const { return m_v; }

protected:
private:
	float m_u;
	float m_v;
};



inline int operator==(const UV& a, const UV& b) { return (a.U()==b.U())&&(a.V()==b.V()); }
inline int operator!=(const UV& a, const UV& b) { return (a.U()!=b.U())||(a.V()!=b.V()); }

// -----------------------------------------------------------------------------

enum VertexType {
	VERTEXTYPE_1 = 0,		// ((x, y, z))
	VERTEXTYPE_C = 1,		// ((x, y, z), (r, g, b, a))
	VERTEXTYPE_T = 2,		// ((x, y, z), (u, v))
	VERTEXTYPE_TC = 3		// ((x, y, z), (u, v), (r, g, b, a))
};



inline BOOL IsColored(VertexType v) { return ((int)v & 1)==1; }
inline BOOL IsTextured(VertexType v) { return ((int)v & 2)==2; }

// -----------------------------------------------------------------------------

#define CC_ON		0x00000000
#define CC_LEFT		0x00000001
#define CC_RIGHT	0x00000002
#define CC_ABOVE	0x00000004
#define CC_BELOW	0x00000008
#define CC_NEAR		0x00000010
#define CC_FAR		0x00000020

// -----------------------------------------------------------------------------

class Vertex {
public:
	inline ~Vertex(void) { }
	Vertex(void);
	Vertex(const Vector& xyz);
	Vertex(const ::UV& uv);
	Vertex(const Vector& xyz, const ::UV& uv);
	Vertex(const ::RGBA& rgba);
	Vertex(const Vector& xyz, const ::RGBA& rgba);
	Vertex(const ::UV& uv, const ::RGBA& rgba);
	Vertex(const Vector& xyz, const ::UV& uv, const ::RGBA& rgba);
	Vertex(const Vector& xyz, const ::Normal& n, const ::UV& uv, const ::RGBA& rgba);

	Vertex(const Vertex& v) : m_xyz(v.XYZ()), m_normal(v.Normal()), m_uv(v.UV()), m_rgba(v.RGBA()) { }
	Vertex& operator=(const Vertex& v) { XYZ() = v.XYZ(); Normal() = v.Normal(); SetUV(v.UV()); RGBA() = v.RGBA(); return *this; }

	Vector&			XYZ(void) { return m_xyz; }
	const Vector&	XYZ(void) const { return m_xyz; }
	float&			X(void) { return m_xyz.X(); }
	float			X(void) const { return m_xyz.X(); }
	float&			Y(void) { return m_xyz.Y(); }
	float			Y(void) const { return m_xyz.Y(); }
	float&			Z(void) { return m_xyz.Z(); }
	float			Z(void) const { return m_xyz.Z(); }

	::Normal&		Normal(void) { return m_normal; }
	const ::Normal&	Normal(void) const { return m_normal; }

	inline void SetUV(const ::UV& uv) { m_uv = uv; }
	const ::UV&	UV(void) const { return m_uv; }
	float&	U(void) { return m_uv.U(); }
	float	U(void) const { return m_uv.U(); }
	float&	V(void) { return m_uv.V(); }
	float	V(void) const { return m_uv.V(); }

	::RGBA&		RGBA(void) { return m_rgba; }
	const ::RGBA&	RGBA(void) const { return m_rgba; }
	float&	R(void) { return m_rgba.R(); }
	float	R(void) const { return m_rgba.R(); }
	float&	G(void) { return m_rgba.G(); }
	float	G(void) const { return m_rgba.G(); }
	float&	B(void) { return m_rgba.B(); }
	float	B(void) const { return m_rgba.B(); }
	float&	A(void) { return m_rgba.A(); }
	float	A(void) const { return m_rgba.A(); }

protected:
private:
	Vector		m_xyz;
	::Normal	m_normal;
	::UV		m_uv;
	::RGBA		m_rgba;
};

// -----------------------------------------------------------------------------

class TLVertex {
public:
	~TLVertex(void);
	TLVertex(void);

	inline DWORD&	ClipCode(void) { return m_clipCode; }
	inline DWORD	ClipCode(void) const { return m_clipCode; }

	inline Vector&	XYZ(void) { return m_xyz; }
	inline const Vector& XYZ(void) const { return m_xyz; }
	inline float& X(void) { return m_xyz.X(); }
	inline float X(void) const { return m_xyz.X(); }
	inline float& Y(void) { return m_xyz.Y(); }
	inline float Y(void) const { return m_xyz.Y(); }
	inline float& Z(void) { return m_xyz.Z(); }
	inline float Z(void) const { return m_xyz.Z(); }
	
	inline ::UV&	UV(void) { return m_uv; }
	inline const ::UV& UV(void) const { return m_uv; }
	inline float& U(void) { return m_uv.U(); }
	inline float U(void) const { return m_uv.U(); }
	inline float& V(void) { return m_uv.V(); }
	inline float V(void) const { return m_uv.V(); }

	inline ::RGBA& RGBA(void) { return *((::RGBA*)&m_vtcf.r); }
	inline const ::RGBA& RGBA(void) const { return *((::RGBA*)&m_vtcf.r); }
	inline float& R(void) { return m_vtcf.r; }
	inline float R(void) const { return m_vtcf.r; }
	inline float& G(void) { return m_vtcf.g; }
	inline float G(void) const { return m_vtcf.g; }
	inline float& B(void) { return m_vtcf.b; }
	inline float B(void) const { return m_vtcf.b; }
	inline float& A(void) { return m_vtcf.a; }
	inline float A(void) const { return m_vtcf.a; }

	inline const C3D_VTCF& VTCF(void) const { return m_vtcf; }
	inline C3D_VTCF& VTCF(void) { return m_vtcf; }

	inline DWORD ComputeClipCode(float farClip);

protected:
private:
	DWORD		m_clipCode;
	Vector		m_xyz;
	::UV		m_uv;
	C3D_VTCF	m_vtcf;

	TLVertex(const TLVertex& p);
	TLVertex& operator=(const TLVertex& p);
};

// -----------------------------------------------------------------------------

inline DWORD TLVertex::ComputeClipCode(float farClip)
{
	DWORD cc = 0;

	if(Z() < 1.0)
		cc |= CC_NEAR;
	else if(Z() > farClip)
		cc |= CC_FAR;
	else
	{
		if(X() > Z())		cc |= CC_RIGHT;
		if(X() < -Z())		cc |= CC_LEFT;
		if(Y() > Z())		cc |= CC_ABOVE;
		if(Y() < -Z())		cc |= CC_BELOW;
	}

	ClipCode() = cc;
	return cc;
}

// -----------------------------------------------------------------------------

inline BOOL IsOn(const TLVertex& v) { return v.ClipCode() == CC_ON; }
inline BOOL IsNear(const TLVertex& v) { return (v.ClipCode() & CC_NEAR)==CC_NEAR; }
inline BOOL IsFar(const TLVertex& v) { return (v.ClipCode() & CC_FAR)==CC_FAR; }
inline BOOL IsLeft(const TLVertex& v) { return (v.ClipCode() & CC_LEFT)==CC_LEFT; }
inline BOOL IsRight(const TLVertex& v) { return (v.ClipCode() & CC_RIGHT)==CC_RIGHT; }
inline BOOL IsAbove(const TLVertex& v) { return (v.ClipCode() & CC_ABOVE)==CC_ABOVE; }
inline BOOL IsBelow(const TLVertex& v) { return (v.ClipCode() & CC_BELOW)==CC_BELOW; }

inline BOOL IsOn(const TLVertex* v) { return IsOn(*v); }
inline BOOL IsNear(const TLVertex* v) { return IsNear(*v); }
inline BOOL IsFar(const TLVertex* v) { return IsFar(*v); }
inline BOOL IsLeft(const TLVertex* v) { return IsLeft(*v); }
inline BOOL IsRight(const TLVertex* v) { return IsRight(*v); }
inline BOOL IsAbove(const TLVertex* v) { return IsAbove(*v); }
inline BOOL IsBelow(const TLVertex* v) { return IsBelow(*v); }

// -----------------------------------------------------------------------------
#endif
