/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * geom.h - Header file for GEOM.CPP.                                         *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#ifndef GEOM_H
#define GEOM_H

#include <math.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EPSILON         0.00000001
#define MAX(a, b)      (((a) > (b)) ? (a) : (b))
#define MIN(a, b)      (((a) < (b)) ? (a) : (b))
#define ABS(a)         (((a) > 0) ? (a) : -(a))
#define CLAMP(n, a, b) (MAX (MIN ((n), (b)), (a)))
#define FLT_ZERO(a)    (ABS ((a)) <= EPSILON)

#define SetVector(v, x, y, z, w)  { v[0]=x; v[1]=y; v[2]=z; v[3]=w;}
#define Dot(a, b)       (a[0]*b[0] + a[1]*b[1] + a[2]*b[2])
#define VectorLength(a) ((float)(sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2])))


#define MAP_PER_FACET   0
#define MAP_PER_OBJECT  1

// View functions.

BOOL    SetCameraPos (PCAMERA pcamera);
BOOL    SetCameraView (PFRUSTUM pfrustum);
BOOL    SetViewPort (PVIEWPORT pviewport);
void    CalcViewParams (void);

// Matrix functions.

void    TransposeMatrix (MATRIX origmat, MATRIX tranmat);
void    RotateMatrix (float angle, float x, float y, float z, MATRIX rotmat);
void    XRotateMatrix (float xangle, MATRIX xrotmat);
void    YRotateMatrix (float yangle, MATRIX yrotmat);
void    ZRotateMatrix (float zangle, MATRIX zrotmat);
void    MultMatrix (MATRIX mat0, MATRIX mat1, MATRIX result);
void    CopyMatrix (MATRIX src, MATRIX dst);
void    IdentityMatrix (MATRIX imat);
void    TranslateMatrix (float tx, float ty, float tz, MATRIX tranmat);
void    ScaleMatrix (float sx, float sy, float sz, MATRIX scalmat);
void    LoadMatrixRows (MATRIX R, VECT row1, VECT row2, VECT row3);

// Vector functions.

void    CopyVector (VECT src, VECT dst);
void    AddVector (VECT vect1, VECT vect2, VECT result);
void    SubVector (VECT vect1, VECT vect2, VECT result);
void    VectMultVector (VECT vect1, VECT vect2, VECT result);
void    ScalMultVector (float factor, VECT vect);
void    NormVector (VECT vect);
void    Cross (VECT vect1, VECT vect2, VECT result);

// Object geometry functions.

BOOL    FacetNormal (OBJECT *lpObject);
BOOL    VertexNormal (OBJECT *lpObject);
BOOL    VertexRGB (OBJECT *lpObject);
BOOL    CullBackface (OBJECT *lpObject);
BOOL    TransformVertices (OBJECT *lpObject, MATRIX mat);
BOOL    TransformObject (OBJECT *lpObject);
BOOL    ProjectObject (OBJECT *lpObject);
BOOL    UpdateObject (OBJECT *lpObject, MATRIX mat);
BOOL    TexSetup (POBJECT lpObject, MATRIX mat);
float   *PointMultMatrix (VECT pt, MATRIX mat, VECT result);
BOOL    CalcViewBoundingBox (POBJECT lpObject);

void QSortFacetArray (POBJECT lpObject, int left, int right);
int CompareFacets (POBJECT lpObject, const PFACET pfacet1, const PFACET pfacet2);


#ifdef __cplusplus
}
#endif


#endif
