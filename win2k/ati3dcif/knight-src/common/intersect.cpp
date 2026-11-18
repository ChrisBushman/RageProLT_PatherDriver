/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Simple intersection functionality.										   *
 *******************************************************************************/
#include "stdwin.h"
#include "intersect.h"
#include "xform.h"

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#define SIGN(x) ((x)<0?-1:1)

// -----------------------------------------------------------------------------

BOOL IntersectTriangleSegment(float* pDistance,
							  const Vector& triA,
							  const Vector& triB,
							  const Vector& triC,
							  const Vector& a,
							  const Vector& b)
{
	ASSERT(pDistance);

	Normal n(triA, triB, triC);
	double tN = -n.W() - n.X() * a.X() - n.Y() * a.Y() - n.Z() * a.Z();
	double tD = n.X()*(b.X()-a.X()) + n.Y()*(b.Y()-a.Y()) + n.Z()*(b.Z()-a.Z());
	if(tD == 0.0) return FALSE;
	double t =  tN / tD;
	(*pDistance) = t;

	Vector p = (b-a)*t + a;

	Vector nAB = CrossProduct((triB - triA), n.GetVector());
	Vector nBC = CrossProduct((triC - triB), n.GetVector());
	Vector nCA = CrossProduct((triA - triC), n.GetVector());
	
	float AB = nAB.X()*(p.X()-triA.X())+nAB.Y()*(p.Y()-triA.Y())+nAB.Z()*(p.Z()-triA.Z());
	float BC = nBC.X()*(p.X()-triB.X())+nBC.Y()*(p.Y()-triB.Y())+nBC.Z()*(p.Z()-triB.Z());
	float CA = nCA.X()*(p.X()-triC.X())+nCA.Y()*(p.Y()-triC.Y())+nCA.Z()*(p.Z()-triC.Z());

	if((SIGN(AB) == SIGN(BC)) && (SIGN(BC) == SIGN(CA)) && (t >= 0.0) && (t <= 1.0))
		return TRUE;
	else
		return FALSE;
}

// -----------------------------------------------------------------------------

BOOL IntersectLine(float& tA, float& tB, const Vector2& a0, const Vector2& a1, const Vector2& b0, const Vector2& b1)
{
	float a10X = a1.X() - a0.X();
	float a10Y = a1.Y() - a0.Y();
	float b10X = b1.X() - b0.X();
	float b10Y = b1.Y() - b0.Y();

	double den = a10X*b10Y-a10Y*b10X;

	if(den == 0)
		return FALSE;

	float a0b0X = a0.X() - b0.X();
	float a0b0Y = a0.Y() - b0.Y();

	tA = (a0b0Y*b10X-a0b0X*b10Y) / den;
	tB = (a0b0Y*a10X-a0b0X*a10Y) / den;

	return TRUE;
}

// -----------------------------------------------------------------------------
