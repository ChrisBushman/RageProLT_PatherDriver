/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *	Simple intersection functionality.										   *
 *******************************************************************************/
#ifndef INTERSECT_H
#define INTERSECT_H
// -----------------------------------------------------------------------------

#include "matrix.h"

// -----------------------------------------------------------------------------

BOOL IntersectTriangleSegment(float* pDistance, const Vector& triA, const Vector& triB, const Vector& triC, const Vector& lineA, const Vector& lineB);
BOOL IntersectLine(float& tA, float& tB, const Vector2& a0, const Vector2& a1, const Vector2& b0, const Vector2& b1);

// -----------------------------------------------------------------------------
#endif
