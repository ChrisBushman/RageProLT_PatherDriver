/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	Landscape construction functionality. The landscape is constructed from	   *
 *	landsquares. Each landsquare is made up of two faces and two sides. The	   *
 *	sides may or may not be hidden, depending on the specific landscape		   *
 *	feature being built. 													   *
 *******************************************************************************/
#ifndef LANDSCAPE_H
#define LANDSCAPE_H
// -----------------------------------------------------------------------------

#include "util.h"
#include "clipper.h"

// -----------------------------------------------------------------------------

class Landscape;

// -----------------------------------------------------------------------------

class LandSquare {
public:
	LandSquare(void);
	void Setup(Landscape* pLandscape, int i, int j);

	void Draw(Clipper& clipper);
	void DrawWater(Clipper& clipper);
	void DrawOutline(Clipper& clipper, const RGBA& color, BOOL drawCross);
	void DrawSurfaceOutline(Clipper& clipper, const RGBA& color, BOOL drawCross, BOOL labelCorners);
	void DrawNorthFaceOutline(Clipper& clipper, const RGBA& color);
	void DrawEastFaceOutline(Clipper& clipper, const RGBA& color);

	BOOL Intersects(float* pT, int* pFace, const Vector& a, const Vector& b, BOOL hidden) const;
	float HeightAt(float x, float y);

	void AdjustHeight(float dNw, float dNe, float dSe, float dSw, BOOL setAttachments);
	void SetHeight(float nw, float ne, float se, float sw, BOOL setAttachments);

#define BOOL_DEF(n, m)                     \
	const BOOL n(void) const { return m; } \
	void n(BOOL s) { m = s; }

#define BOOL_DEF1(n, m)                     \
	const BOOL n(void) const { return m; } \
	void n(BOOL s);

	BOOL_DEF1(AttachNW, m_attachNW)
	BOOL_DEF1(AttachNE, m_attachNE)
	BOOL_DEF1(AttachSE, m_attachSE)
	BOOL_DEF1(AttachSW, m_attachSW)

	BOOL_DEF(SurfaceDrawn, m_surfaceDrawn)
	BOOL_DEF(SurfaceTextured, m_surfaceTextured)
	const float SurfaceHeight(int n) const { ASSERT((n>=0)&&(n<4)); return m_surfaceHeight[n]; }
	float& SurfaceHeight(int n) { ASSERT((n>=0)&&(n<4)); return m_surfaceHeight[n]; }
	const UV& SurfaceUV(int n) const { ASSERT((n>=0)&&(n<4)); if(AlternateSurface()) return m_alternateUV[n]; else return m_surfaceUV[n]; }
	UV& SurfaceUV(int n) { ASSERT((n>=0)&&(n<4)); if(AlternateSurface()) return m_alternateUV[n]; else return m_surfaceUV[n]; }
	const RGBA& SurfaceColor(void) const { return m_surfaceColor; }
	RGBA& SurfaceColor(void) { return m_surfaceColor; }
	BOOL_DEF(SurfaceNWSE, m_surfaceNWSE)
	Vector SurfaceVector(int n) const;
	Vertex SurfaceVertex(int n) const;
	BOOL SurfaceIntersect(float* pT, const Vector& a, const Vector& b) const;
	Normal& SurfaceNormal(int n) { ASSERT((n>=0)&&(n<2)); return m_surfaceNormal[n]; }
	const Normal& SurfaceNormal(int n) const { ASSERT((n>=0)&&(n<2)); return m_surfaceNormal[n]; }

	BOOL_DEF(NorthFaceDrawn, m_northFaceDrawn)
	BOOL_DEF(NorthFaceTextured, m_northFaceTextured)
	const UV& NorthFaceUV(int n) const { ASSERT((n>=0)&&(n<4)); return m_northFaceUV[n]; }
	UV& NorthFaceUV(int n) { ASSERT((n>=0)&&(n<4)); return m_northFaceUV[n]; }
	const RGBA& NorthFaceColor(void) const { return m_northFaceColor; }
	RGBA& NorthFaceColor(void) { return m_northFaceColor; }
	Vector NorthFaceVector(int n) const;
	Vertex NorthFaceVertex(int n) const;
	BOOL NorthFaceIntersect(float* pT, const Vector& a, const Vector& b) const;

	BOOL_DEF(EastFaceDrawn, m_eastFaceDrawn)
	BOOL_DEF(EastFaceTextured, m_eastFaceTextured)
	const UV& EastFaceUV(int n) const { ASSERT((n>=0)&&(n<4)); return m_eastFaceUV[n]; }
	UV& EastFaceUV(int n) { ASSERT((n>=0)&&(n<4)); return m_eastFaceUV[n]; }
	const RGBA& EastFaceColor(void) const { return m_eastFaceColor; }
	RGBA& EastFaceColor(void) { return m_eastFaceColor; }
	Vector EastFaceVector(int n) const;
	Vertex EastFaceVertex(int n) const;
	BOOL EastFaceIntersect(float* pT, const Vector& a, const Vector& b) const;

	BOOL_DEF(Watered, m_watered);
	float WaterLevel(void) const { return m_waterLevel; }
	float& WaterLevel(void) { return m_waterLevel; }
	const RGBA& WaterColor(int n) const { ASSERT((n>=0)&&(n<4)); return m_waterColor[n]; }
	RGBA& WaterColor(int n) { ASSERT((n>=0)&&(n<4)); return m_waterColor[n]; }

	BOOL_DEF(AlternateSurface, m_alternateSurface);

#undef BOOL_DEF

	const BOOL FaceDrawn(int f) const;
	void FaceDrawn(int f, BOOL s);
	const BOOL FaceTextured(int f) const;
	void FaceTextured(int f, BOOL s);
	const UV& FaceUV(int f, int n) const;
	UV& FaceUV(int f, int n);
	const RGBA& FaceColor(int f) const;
	RGBA& FaceColor(int f);
	Vector FaceVector(int f, int n) const;
	Vertex FaceVertex(int f, int n) const;
	BOOL FaceIntersect(int f, float* pT, const Vector& a, const Vector& b) const;

	LandSquare* FromHereNW(void) const { return FromHere(-1, -1); }
	LandSquare* FromHereN(void) const  { return FromHere( 0, -1); }
	LandSquare* FromHereNE(void) const { return FromHere( 1, -1); }
	LandSquare* FromHereW(void) const  { return FromHere(-1,  0); }
	LandSquare* FromHereE(void) const  { return FromHere( 1,  0); }
	LandSquare* FromHereSW(void) const { return FromHere(-1,  1); }
	LandSquare* FromHereS(void) const  { return FromHere( 0,  1); }
	LandSquare* FromHereSE(void) const { return FromHere( 1,  1); }
	
	void RecomputeNormals(void);

	void Write(fileHandle& fh) ;
	void Read(fileHandle& fh, uint32 version) ;

private:
	Landscape*	m_pLandscape;
	int			m_squareI;
	int			m_squareJ;

	union {
		struct {
			DWORD	m_attachNW:1;
			DWORD	m_attachNE:1;
			DWORD	m_attachSE:1;
			DWORD	m_attachSW:1;
			DWORD	m_surfaceDrawn:1;
			DWORD	m_surfaceTextured:1;
			DWORD	m_surfaceNWSE:1;
			DWORD	m_northFaceDrawn:1;
			DWORD	m_northFaceTextured:1;
			DWORD	m_eastFaceDrawn:1;
			DWORD	m_eastFaceTextured:1;
			DWORD	m_watered:1;
			DWORD	m_alternateSurface:1;
		};
		DWORD	m_bitFlags;
	};

	float		m_surfaceHeight[4];
	UV			m_surfaceUV[4];
	RGBA		m_surfaceColor;
	Normal		m_surfaceNormal[2];

	UV			m_northFaceUV[4];
	RGBA		m_northFaceColor;
	Normal		m_northFaceNormal;

	UV			m_eastFaceUV[4];
	RGBA		m_eastFaceColor;
	Normal		m_eastFaceNormal;

	float		m_waterLevel;
	RGBA		m_waterColor[4];

	LandSquare* FromHere(int di, int dj) const;

	static UV m_alternateUV[4];
};



class Landscape {
public:
	~Landscape(void) { }
	Landscape(void);

	LandSquare& Square(int i, int j) { ASSERT((i>=0)&&(i<17)&&(j>=0)&&(j<17)); return m_grid[i][j]; }
	const LandSquare& Square(int i, int j) const { ASSERT((i>=0)&&(i<17)&&(j>=0)&&(j<17)); return m_grid[i][j]; }

	LandSquare* IntersectingSquare(int* pFace, const Vector& a, const Vector& b, BOOL hidden);
	LandSquare* IntersectingSquare(int* pFace, float* pT, const Vector& a, const Vector& b, BOOL hidden);

	float HeightAt(float x, float y);
	LandSquare& SquareAt(float x, float y);
	const LandSquare& SquareAt(float x, float y) const;

	void Draw(Clipper& clipper, BOOL outline);
	void DrawWater(Clipper& clipper);

	void Write(fileHandle& fh) ;
	void Write(char* filename) ;

	void Read(fileHandle& fh) ;
	void Read(char* filename) ;

	Poly*	CreateMesh(void) const;

	void Initialize(void);

	Ati3dTexture*	m_pTexture;
	Ati3dTexture*	m_pAlternate;

protected:
private:
	LandSquare m_grid[17][17];

	Landscape(const Landscape&);
	Landscape& operator=(const Landscape&);
};

// -----------------------------------------------------------------------------
#endif
