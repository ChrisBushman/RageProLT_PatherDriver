/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	Vector and Matrix library. 												   *
 *******************************************************************************/
#ifndef MATRIX_H
#define MATRIX_H
// -----------------------------------------------------------------------------

#include "types.h"
#include <math.h>

// -----------------------------------------------------------------------------

class Matrix;

// -----------------------------------------------------------------------------

enum __MATRIX_UNINITIALIZED__ { UNINITIALIZED };
enum __MATRIX_ZERO__ { ZERO };
enum __MATRIX_IDENTITY__ { IDENTITY };
enum __MATRIX_SCALE__ { SCALE };
enum __MATRIX_ROTATION__ { ROTATION };

// -----------------------------------------------------------------------------

class Vector2 {
public:
	~Vector2(void) { }
	Vector2(void) { }
	Vector2(__MATRIX_UNINITIALIZED__) { }
	Vector2(__MATRIX_ZERO__) { X()=0.0; Y()=0.0; }
	Vector2(float x, float y) { X()=x; Y()=y; }
	Vector2(const Vector2& v) { X()=v.X(); Y()=v.Y(); }
	Vector2& operator=(const Vector2& v) { X()=v.X(); Y()=v.Y(); return *this; }

	float  Element(int n) const { ASSERT((n>=0)&&(n<2)); return element[n]; }
	float& Element(int n) { ASSERT((n>=0)&&(n<2)); return element[n]; }

	float  X(void) const { return element[0]; }
	float& X(void) { return element[0]; }

	float  Y(void) const { return element[1]; }
	float& Y(void) { return element[1]; }

	Vector2& operator+=(const Vector2& v) { X()+=v.X(); Y()+=v.Y(); return *this; }
	Vector2& operator-=(const Vector2& v) { X()-=v.X(); Y()-=v.Y(); return *this; }
	Vector2& operator*=(float s) { X()*=s; Y()*=s; return *this; }
	Vector2& operator/=(float s) { X()/=s; Y()/=s; return *this; }

	void Normalize(void);

protected:
private:
	float element[2];
};



Vector2 Normalize(const Vector2& v);
float  DotProduct(const Vector2& a, const Vector2& b);
inline Vector2 operator-(const Vector2& a) { return Vector2(-a.X(), -a.Y()); }
inline Vector2 operator+(const Vector2& a, const Vector2& b) { return Vector2(a.X() + b.X(), a.Y() + b.Y()); }
inline Vector2 operator-(const Vector2& a, const Vector2& b) { return Vector2(a.X() - b.X(), a.Y() - b.Y()); }
inline Vector2 operator*(const Vector2& a, float b) { return Vector2(a.X() * b, a.Y() * b); }
inline Vector2 operator/(const Vector2& a, float b) { return Vector2(a.X() / b, a.Y() / b); }
inline int operator==(const Vector2& a, const Vector2& b) { return (a.X()==b.X())&&(a.Y()==b.Y()); }
inline int operator!=(const Vector2& a, const Vector2& b) { return (a.X()!=b.X())||(a.Y()!=b.Y()); }

// -----------------------------------------------------------------------------

class Vector {
public:
	~Vector(void) { }
	Vector(void) { }
	Vector(__MATRIX_UNINITIALIZED__) { }
	Vector(__MATRIX_ZERO__) { X()=0.0; Y()=0.0; Z()=0.0; }
	Vector(float x, float y, float z) { X()=x; Y()=y; Z()=z; }
	Vector(const Vector& v) { X()=v.X(); Y()=v.Y(); Z()=v.Z(); }
	Vector& operator=(const Vector& v) { X()=v.X(); Y()=v.Y(); Z()=v.Z(); return *this; }

	float  Element(int n) const { ASSERT((n>=0)&&(n<3)); return element[n]; }
	float& Element(int n) { ASSERT((n>=0)&&(n<3)); return element[n]; }

	float  X(void) const { return element[0]; }
	float& X(void) { return element[0]; }

	float  Y(void) const { return element[1]; }
	float& Y(void) { return element[1]; }

	float  Z(void) const { return element[2]; }
	float& Z(void) { return element[2]; }

	Vector& operator+=(const Vector& v) { X()+=v.X(); Y()+=v.Y(); Z()+=v.Z(); return *this; }
	Vector& operator-=(const Vector& v) { X()-=v.X(); Y()-=v.Y(); Z()-=v.Z(); return *this; }
	Vector& operator*=(const Matrix& m);
	Vector& operator*=(float s) { X()*=s; Y()*=s; Z()*=s; return *this; }
	Vector& operator/=(float s) { X()/=s; Y()/=s; Z()/=s; return *this; }

	void Normalize(void);

protected:
private:
	float element[3];
};



Vector Normalize(const Vector& v);
float  DotProduct(const Vector& a, const Vector& b);
Vector CrossProduct(const Vector& a, const Vector& b);
inline Vector operator-(const Vector& a) { return Vector(-a.X(), -a.Y(), -a.Z()); }
inline Vector operator+(const Vector& a, const Vector& b) { return Vector(a.X() + b.X(), a.Y() + b.Y(), a.Z() + b.Z()); }
inline Vector operator-(const Vector& a, const Vector& b) { return Vector(a.X() - b.X(), a.Y() - b.Y(), a.Z() - b.Z()); }
inline Vector operator*(const Vector& a, float b) { return Vector(a.X() * b, a.Y() * b, a.Z() * b); }
inline Vector operator/(const Vector& a, float b) { return Vector(a.X() / b, a.Y() / b, a.Z() / b); }
inline int operator==(const Vector& a, const Vector& b) { return (a.X()==b.X())&&(a.Y()==b.Y())&&(a.Z()==b.Z()); }
inline int operator!=(const Vector& a, const Vector& b) { return (a.X()!=b.X())||(a.Y()!=b.Y())||(a.Z()!=b.Z()); }

inline Vector TransformBy(const Vector& v, const Matrix& rot, const Vector& trans);
inline Vector UntransformBy(const Vector& v, const Matrix& rot, const Vector& trans);
inline Vector UntransformByInverse(const Vector& v, const Matrix& invRot, const Vector& trans);

// -----------------------------------------------------------------------------

class Normal {
public:
	~Normal(void) { }
	Normal(void) : v(UNINITIALIZED) { }
	Normal(__MATRIX_UNINITIALIZED__) : v(UNINITIALIZED) { }
	Normal(__MATRIX_ZERO__) : v(ZERO) { W()=0.0; }
	Normal(float x, float y, float z, float w = 0.0) : v(x, y, z) { W()=w; }
	Normal(const Vector& v1, float d = 0.0) : v(v1), w(d) { }
	Normal(const Normal& n) : v(n.GetVector()) { W()=n.W(); }
	Normal& operator=(const Normal& n) { GetVector() = n.GetVector(); W()=n.W(); return *this; }
	Normal(const Vector& a, const Vector& b, const Vector& c);

	Vector APoint(void) const
	{
		float x = fabs(X());
		float y = fabs(Y());
		float z = fabs(Z());
		if((x >= y) && (x >= z))
			return Vector(-W() / X(), 0, 0);
		else if((y >= x) && (y >= z))
			return Vector(0, -W() / Y(), 0);
		else
			return Vector(0, 0, -W() / Z());
	}

	Vector&			GetVector(void) { return v; }
	const Vector&	GetVector(void) const { return v; }

	float  Element(int n) const { if(n==3) return w; else return v.Element(n); }
	float& Element(int n) { if(n==3) return w; else return v.Element(n); }

	float  X(void) const { return v.X(); }
	float& X(void) { return v.X(); }

	float  Y(void) const { return v.Y(); }
	float& Y(void) { return v.Y(); }

	float  Z(void) const { return v.Z(); }
	float& Z(void) { return v.Z(); }

	float  W(void) const { return w; }
	float& W(void) { return w; }

	Normal& operator+=(const Normal& n) { X()+=n.X(); Y()+=n.Y(); Z()+=n.Z(); W()+=n.W(); return *this; }
	Normal& operator-=(const Normal& n) { X()-=n.X(); Y()-=n.Y(); Z()-=n.Z(); W()-=n.W(); return *this; }
	Normal& operator*=(float s) { X()*=s; Y()*=s; Z()*=s; W()*=s; return *this; }
	Normal& operator/=(float s) { X()/=s; Y()/=s; Z()/=s; W()/=s; return *this; }

protected:
private:
	Vector	v;
	float	w;
};



inline float  DotProduct(const Normal& a, const Vector& b) { return a.X()*b.X()+a.Y()*b.Y()+a.Z()*b.Z()+a.W(); }
inline float  DotProduct(const Normal& a, const Normal& b) { return a.X()*b.X()+a.Y()*b.Y()+a.Z()*b.Z()+a.W()*b.W(); }
inline Normal operator+(const Normal& a, const Normal& b) { return Normal(a.X()+b.X(), a.Y()+b.Y(), a.Z()+b.Z(), a.W()+b.W()); }
inline Normal operator-(const Normal& a, const Normal& b) { return Normal(a.X()-b.X(), a.Y()-b.Y(), a.Z()-b.Z(), a.W()-b.W()); }
inline Normal operator*(const Normal& a, float b) { return Normal(a.X()*b, a.Y()*b, a.Z()*b, a.W()*b); }
inline Normal operator/(const Normal& a, float b) { return Normal(a.X()/b, a.Y()/b, a.Z()/b, a.W()/b); }
inline int operator==(const Normal& a, const Normal& b) { return (a.X()==b.X())&&(a.Y()==b.Y())&&(a.Z()==b.Z())&&(a.W()==b.W()); }
inline int operator!=(const Normal& a, const Normal& b) { return (a.X()!=b.X())||(a.Y()!=b.Y())||(a.Z()!=b.Z())||(a.W()!=b.W()); }

inline Normal TransformBy(const Normal& n, const Matrix& rot, const Vector& trans);
inline Normal UntransformBy(const Normal& n, const Matrix& rot, const Vector& trans);
inline Normal UntransformByInverse(const Normal& n, const Matrix& invRot, const Vector& trans);

// -----------------------------------------------------------------------------

class Matrix {
public:
	~Matrix(void) { }
	Matrix(__MATRIX_UNINITIALIZED__) { }
	Matrix(__MATRIX_ZERO__);
	Matrix(__MATRIX_IDENTITY__);
	Matrix(__MATRIX_SCALE__, float xs, float ys, float zs);
	Matrix(__MATRIX_SCALE__, const Vector& v);
	Matrix(__MATRIX_ROTATION__, float xr, float yr, float zr);
	Matrix(__MATRIX_ROTATION__, const Vector& v);
	Matrix(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22);
	Matrix(const Vector& a, const Vector& b, const Vector& c);
	Matrix(const Matrix& m);
	Matrix& operator=(const Matrix& m);

	Vector GetRow(int row) { ASSERT((row>=0)&&(row<3)); return Vector(elements[row][0], elements[row][1], elements[row][2]); }
	Vector GetColumn(int col) { ASSERT((col>=0)&&(col<3)); return Vector(elements[0][col], elements[1][col], elements[2][col]); }

	Matrix& operator*=(const Matrix& m);
	Matrix& Transpose(void);
	
	float  Element(int row, int col) const { ASSERT((row>=0)&&(row<3)); ASSERT((col>=0)&&(col<3)); return elements[row][col]; }
	float& Element(int row, int col) { ASSERT((row>=0)&&(row<3)); ASSERT((col>=0)&&(col<3)); return elements[row][col]; }

	double Determinant(void) const;

protected:
private:
	float elements[3][3];
};

Matrix operator*(const Matrix& m, float f);
Vector operator*(const Matrix& m, const Vector& v);
Vector operator*(const Vector& v, const Matrix& m);
Matrix operator*(const Matrix& m1, const Matrix& m2);
Matrix Transpose(const Matrix& m);
Matrix Inverse(const Matrix& m);
Matrix operator-(const Matrix& m1, const Matrix& m2);
Matrix operator+(const Matrix& m1, const Matrix& m2);
Matrix operator/(const Matrix& m1, float f);

// -----------------------------------------------------------------------------

inline Vector TransformBy(const Vector& v, const Matrix& rot, const Vector& trans)
{
	return rot * v + trans;
}

inline Vector UntransformBy(const Vector& v, const Matrix& rot, const Vector& trans)
{
	return Inverse(rot) * (v - trans);
}

inline Vector UntransformByInverse(const Vector& v, const Matrix& invRot, const Vector& trans)
{
	return invRot * (v - trans);
}

inline Normal TransformBy(const Normal& n, const Matrix& rot, const Vector& trans)
{
	Vector v = rot * n.GetVector();
	return Normal(v, -DotProduct(v, rot * n.APoint() + trans));
}

inline Normal UntransformBy(const Normal& n, const Matrix& rot, const Vector& trans)
{
	Matrix i = Inverse(rot);
	Vector v = i * n.GetVector();
	return Normal(v, -DotProduct(v, i * (n.APoint() - trans)));
}

inline Normal UntransformByInverse(const Normal& n, const Matrix& invRot, const Vector& trans)
{
	Vector v = invRot * n.GetVector();
	return Normal(v, -DotProduct(v, invRot * (n.APoint() - trans)));
}

// -----------------------------------------------------------------------------
#endif
