/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Vector and Matrix library. 												   *
 *******************************************************************************/
#include "stdwin.h"

#include <math.h>

#include "Matrix.h"
#include "Util.h"

// -----------------------------------------------------------------------------

Vector& Vector::operator*=(const Matrix& m)
{
	float x = X() * m.Element(0, 0) + Y() * m.Element(1, 0) + Z() * m.Element(2, 0);
	float y = X() * m.Element(0, 1) + Y() * m.Element(1, 1) + Z() * m.Element(2, 1);
	float z = X() * m.Element(0, 2) + Y() * m.Element(1, 2) + Z() * m.Element(2, 2);

	X() = x;
	Y() = y;
	Z() = z;

	return *this;
}

// -----------------------------------------------------------------------------

float DotProduct(const Vector& a, const Vector& b)
{
	return a.X() * b.X() + a.Y() * b.Y() + a.Z() * b.Z();
}



Vector CrossProduct(const Vector& a, const Vector& b)
{
	Vector c(UNINITIALIZED);
	c.X() = a.Y() * b.Z() - a.Z() * b.Y();
	c.Y() = a.Z() * b.X() - a.X() * b.Z();
	c.Z() = a.X() * b.Y() - a.Y() * b.X();
	return c;
}

// -----------------------------------------------------------------------------

Vector Normalize(const Vector& v)
{
	double l = sqrt(v.X() * v.X() + v.Y() * v.Y() + v.Z() * v.Z());
	return Vector(v.X()/l, v.Y()/l, v.Z()/l);
}



void Vector::Normalize(void)
{
	double l = sqrt(X() * X() + Y() * Y() + Z() * Z());
	X() /= l;
	Y() /= l;
	Z() /= l;
}

// -----------------------------------------------------------------------------

Normal::Normal(const Vector& a, const Vector& b, const Vector& c) : v(UNINITIALIZED)
{
	Vector cross = Normalize(CrossProduct(b-a, c-a));
	X() = cross.X();
	Y() = cross.Y();
	Z() = cross.Z();
	W() = -DotProduct(cross, a);
}

// -----------------------------------------------------------------------------

Matrix::Matrix(__MATRIX_ZERO__)
{
	elements[0][0] = 0.0; elements[0][1] = 0.0; elements[0][2] = 0.0;
	elements[1][0] = 0.0; elements[1][1] = 0.0; elements[1][2] = 0.0;
	elements[2][0] = 0.0; elements[2][1] = 0.0; elements[2][2] = 0.0;
}



Matrix::Matrix(__MATRIX_IDENTITY__)
{
	elements[0][0] = 1.0; elements[0][1] = 0.0; elements[0][2] = 0.0;
	elements[1][0] = 0.0; elements[1][1] = 1.0; elements[1][2] = 0.0;
	elements[2][0] = 0.0; elements[2][1] = 0.0; elements[2][2] = 1.0;
}



Matrix::Matrix(__MATRIX_SCALE__, float xs, float ys, float zs)
{
	elements[0][0] = xs;  elements[0][1] = 0.0; elements[0][2] = 0.0;
	elements[1][0] = 0.0; elements[1][1] = ys;  elements[1][2] = 0.0;
	elements[2][0] = 0.0; elements[2][1] = 0.0; elements[2][2] = zs;
}



Matrix::Matrix(__MATRIX_SCALE__, const Vector& v)
{
	elements[0][0] = v.X();  elements[0][1] = 0.0;    elements[0][2] = 0.0;
	elements[1][0] = 0.0;    elements[1][1] = v.Y();  elements[1][2] = 0.0;
	elements[2][0] = 0.0;    elements[2][1] = 0.0;    elements[2][2] = v.Z();
}



Matrix::Matrix(__MATRIX_ROTATION__, float xr, float yr, float zr)
{
	float cx = cos(xr), sx = sin(xr);
	float cy = cos(yr), sy = sin(yr);
	float cz = cos(zr), sz = sin(zr);

	elements[0][0] = cy*cz;
	elements[0][1] = -cy*sz;
	elements[0][2] = sy;
	elements[1][0] = sx*sy*cz+cx*sz;
	elements[1][1] = -sx*sy*sz+cx*cz;
	elements[1][2] = -sx*cy;
	elements[2][0] = -cx*sy*cz+sx*sz;
	elements[2][1] = cx*sy*sz+sx*cz;
	elements[2][2] = cx*cy;
}



Matrix::Matrix(__MATRIX_ROTATION__, const Vector& v)
{
	float cx = cos(v.X()), sx = sin(v.X());
	float cy = cos(v.Y()), sy = sin(v.Y());
	float cz = cos(v.Z()), sz = sin(v.Z());

	elements[0][0] = cy*cz;
	elements[0][1] = -cy*sz;
	elements[0][2] = sy;
	elements[1][0] = sx*sy*cz+cx*sz;
	elements[1][1] = -sx*sy*sz+cx*cz;
	elements[1][2] = -sx*cy;
	elements[2][0] = -cx*sy*cz+sx*sz;
	elements[2][1] = cx*sy*sz+sx*cz;
	elements[2][2] = cx*cy;
}

// -----------------------------------------------------------------------------

Matrix::Matrix(float m00, float m01, float m02, float m10, float m11, float m12, float m20, float m21, float m22)
{
	elements[0][0] = m00;
	elements[0][1] = m01;
	elements[0][2] = m02;
	elements[1][0] = m10;
	elements[1][1] = m11;
	elements[1][2] = m12;
	elements[2][0] = m20;
	elements[2][1] = m21;
	elements[2][2] = m22;
}

// -----------------------------------------------------------------------------

Matrix::Matrix(const Vector& a, const Vector& b, const Vector& c)
{
	elements[0][0] = a.X();
	elements[0][1] = b.X();
	elements[0][2] = c.X();
	elements[1][0] = a.Y();
	elements[1][1] = b.Y();
	elements[1][2] = c.Y();
	elements[2][0] = a.Z();
	elements[2][1] = b.Z();
	elements[2][2] = c.Z();
}

// -----------------------------------------------------------------------------

Matrix::Matrix(const Matrix& m)
{
	elements[0][0] = m.elements[0][0]; 
	elements[0][1] = m.elements[0][1];
	elements[0][2] = m.elements[0][2];
	elements[1][0] = m.elements[1][0];
	elements[1][1] = m.elements[1][1];
	elements[1][2] = m.elements[1][2];
	elements[2][0] = m.elements[2][0];
	elements[2][1] = m.elements[2][1];
	elements[2][2] = m.elements[2][2];
}



Matrix& Matrix::operator=(const Matrix& m)
{
	elements[0][0] = m.elements[0][0]; 
	elements[0][1] = m.elements[0][1];
	elements[0][2] = m.elements[0][2];
	elements[1][0] = m.elements[1][0];
	elements[1][1] = m.elements[1][1];
	elements[1][2] = m.elements[1][2];
	elements[2][0] = m.elements[2][0];
	elements[2][1] = m.elements[2][1];
	elements[2][2] = m.elements[2][2];
	return *this;
}

// -----------------------------------------------------------------------------

Matrix& Matrix::operator*=(const Matrix& m)
{
	float result[3][3];

	for(int r=0; r<3; r++)
		for(int c=0; c<3; c++)
			result[r][c] = (elements[r][0] * m.elements[0][c] +
							elements[r][1] * m.elements[1][c] +
							elements[r][2] * m.elements[2][c]);

	elements[0][0] = result[0][0]; 
	elements[0][1] = result[0][1];
	elements[0][2] = result[0][2];
	elements[1][0] = result[1][0];
	elements[1][1] = result[1][1];
	elements[1][2] = result[1][2];
	elements[2][0] = result[2][0];
	elements[2][1] = result[2][1];
	elements[2][2] = result[2][2];

	return *this;
}

// -----------------------------------------------------------------------------

Matrix& Matrix::Transpose(void)
{
	float tmp;
	tmp = elements[0][1]; elements[0][1] = elements[1][0]; elements[1][0] = tmp;
	tmp = elements[0][2]; elements[0][2] = elements[2][0]; elements[2][0] = tmp;
	tmp = elements[1][2]; elements[1][2] = elements[2][1]; elements[2][1] = tmp;
	return *this;
}

// -----------------------------------------------------------------------------

Matrix operator*(const Matrix& m, float f)
{
	Matrix r(UNINITIALIZED);
	r.Element(0, 0) = m.Element(0, 0) * f;
	r.Element(0, 1) = m.Element(0, 1) * f;
	r.Element(0, 2) = m.Element(0, 2) * f;
	r.Element(1, 0) = m.Element(1, 0) * f;
	r.Element(1, 1) = m.Element(1, 1) * f;
	r.Element(1, 2) = m.Element(1, 2) * f;
	r.Element(2, 0) = m.Element(2, 0) * f;
	r.Element(2, 1) = m.Element(2, 1) * f;
	r.Element(2, 2) = m.Element(2, 2) * f;
	return r;
}



Vector operator*(const Matrix& m, const Vector& v)
{
	Vector r(UNINITIALIZED);
	r.X() = m.Element(0, 0) * v.X() + m.Element(0, 1) * v.Y() + m.Element(0, 2) * v.Z();
	r.Y() = m.Element(1, 0) * v.X() + m.Element(1, 1) * v.Y() + m.Element(1, 2) * v.Z();
	r.Z() = m.Element(2, 0) * v.X() + m.Element(2, 1) * v.Y() + m.Element(2, 2) * v.Z();
	return r;
}



Vector operator*(const Vector& v, const Matrix& m)
{
	Vector r(UNINITIALIZED);
	r.X() = v.X() * m.Element(0, 0) + v.Y() * m.Element(1, 0) + v.Z() * m.Element(2, 0);
	r.Y() = v.X() * m.Element(0, 1) + v.Y() * m.Element(1, 1) + v.Z() * m.Element(2, 1);
	r.Z() = v.X() * m.Element(0, 2) + v.Y() * m.Element(1, 2) + v.Z() * m.Element(2, 2);
	return r;
}

// -----------------------------------------------------------------------------

Matrix operator*(const Matrix& m1, const Matrix& m2)
{
	Matrix result(UNINITIALIZED);

	for(int r=0; r<3; r++)
		for(int c=0; c<3; c++)
			result.Element(r, c) = (m1.Element(r, 0) * m2.Element(0, c) +
									m1.Element(r, 1) * m2.Element(1, c) +
									m1.Element(r, 2) * m2.Element(2, c));
	return result;
}

// -----------------------------------------------------------------------------

Matrix Transpose(const Matrix& m)
{
	Matrix result(UNINITIALIZED);
	result.Element(0, 0) = m.Element(0, 0);
	result.Element(0, 1) = m.Element(1, 0);
	result.Element(0, 2) = m.Element(2, 0);
	result.Element(1, 0) = m.Element(0, 1);
	result.Element(1, 1) = m.Element(1, 1);
	result.Element(1, 2) = m.Element(2, 1);
	result.Element(2, 0) = m.Element(0, 2);
	result.Element(2, 1) = m.Element(1, 2);
	result.Element(2, 2) = m.Element(2, 2);
	return result;
}

// -----------------------------------------------------------------------------

double Matrix::Determinant(void) const
{
	return Element(0, 0) * (Element(1, 1) * Element(2, 2) - Element(1, 2) * Element(2, 1)) +
		   -Element(0, 1) * (Element(1, 0) * Element(2, 2) - Element(1, 2) * Element(2, 0)) +
		   Element(0, 2) * (Element(1, 0) * Element(2, 1) - Element(1, 1) * Element(2, 0));
}

// -----------------------------------------------------------------------------

Matrix Inverse(const Matrix& m)
{
	// A(-1) = 1/|A| * adjA
	double detA = m.Determinant();
	if(detA == 0.0) THROW_EXCEPTION();

	Matrix r(UNINITIALIZED);
	r.Element(0, 0) = (m.Element(1, 1) * m.Element(2, 2) - m.Element(1, 2) * m.Element(2, 1)) / detA;
	r.Element(0, 1) = -(m.Element(0, 1) * m.Element(2, 2) - m.Element(0, 2) * m.Element(2, 1)) / detA;
	r.Element(0, 2) = (m.Element(0, 1) * m.Element(1, 2) - m.Element(0, 2) * m.Element(1, 1)) / detA;
	r.Element(1, 0) = -(m.Element(1, 0) * m.Element(2, 2) - m.Element(1, 2) * m.Element(2, 0)) / detA;
	r.Element(1, 1) = (m.Element(0, 0) * m.Element(2, 2) - m.Element(0, 2) * m.Element(2, 0)) / detA;
	r.Element(1, 2) = -(m.Element(0, 0) * m.Element(1, 2) - m.Element(0, 2) * m.Element(1, 0)) / detA;
	r.Element(2, 0) = (m.Element(1, 0) * m.Element(2, 1) - m.Element(1, 1) * m.Element(2, 0)) / detA;
	r.Element(2, 1) = -(m.Element(0, 0) * m.Element(2, 1) - m.Element(0, 1) * m.Element(2, 0)) / detA;
	r.Element(2, 2) = (m.Element(0, 0) * m.Element(1, 1) - m.Element(0, 1) * m.Element(1, 0)) / detA;

	return r;
}

// -----------------------------------------------------------------------------

Matrix operator-(const Matrix& m1, const Matrix& m2)
{
	Matrix r(UNINITIALIZED);
	r.Element(0, 0) = m1.Element(0, 0) - m2.Element(0, 0);
	r.Element(0, 1) = m1.Element(0, 1) - m2.Element(0, 1);
	r.Element(0, 2) = m1.Element(0, 2) - m2.Element(0, 2);
	r.Element(1, 0) = m1.Element(1, 0) - m2.Element(1, 0);
	r.Element(1, 1) = m1.Element(1, 1) - m2.Element(1, 1);
	r.Element(1, 2) = m1.Element(1, 2) - m2.Element(1, 2);
	r.Element(2, 0) = m1.Element(2, 0) - m2.Element(2, 0);
	r.Element(2, 1) = m1.Element(2, 1) - m2.Element(2, 1);
	r.Element(2, 2) = m1.Element(2, 2) - m2.Element(2, 2);
	return r;
}

Matrix operator+(const Matrix& m1, const Matrix& m2)
{
	Matrix r(UNINITIALIZED);
	r.Element(0, 0) = m1.Element(0, 0) + m2.Element(0, 0);
	r.Element(0, 1) = m1.Element(0, 1) + m2.Element(0, 1);
	r.Element(0, 2) = m1.Element(0, 2) + m2.Element(0, 2);
	r.Element(1, 0) = m1.Element(1, 0) + m2.Element(1, 0);
	r.Element(1, 1) = m1.Element(1, 1) + m2.Element(1, 1);
	r.Element(1, 2) = m1.Element(1, 2) + m2.Element(1, 2);
	r.Element(2, 0) = m1.Element(2, 0) + m2.Element(2, 0);
	r.Element(2, 1) = m1.Element(2, 1) + m2.Element(2, 1);
	r.Element(2, 2) = m1.Element(2, 2) + m2.Element(2, 2);
	return r;
}

Matrix operator/(const Matrix& m1, float f)
{
	Matrix r(UNINITIALIZED);
	r.Element(0, 0) = m1.Element(0, 0) / f;
	r.Element(0, 1) = m1.Element(0, 1) / f;
	r.Element(0, 2) = m1.Element(0, 2) / f;
	r.Element(1, 0) = m1.Element(1, 0) / f;
	r.Element(1, 1) = m1.Element(1, 1) / f;
	r.Element(1, 2) = m1.Element(1, 2) / f;
	r.Element(2, 0) = m1.Element(2, 0) / f;
	r.Element(2, 1) = m1.Element(2, 1) / f;
	r.Element(2, 2) = m1.Element(2, 2) / f;
	return r;
}

// -----------------------------------------------------------------------------
