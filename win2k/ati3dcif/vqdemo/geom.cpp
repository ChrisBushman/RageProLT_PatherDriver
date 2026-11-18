/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * geom.cpp - This module contains the geometry engine routines.              *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#include <stdio.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include "types.h"
#include "geom.h"

/* win2k port: MSVC's <windef.h> supplied min()/max() macros that this file uses
 * bare (CalcViewBoundingBox). MinGW's g++ does not (they would clash with
 * libstdc++'s std::min/std::max templates), so define them here AFTER all the
 * system headers are pulled in -- no clash, and geom.cpp's bare calls resolve. */
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

VIEW   gView;
MATRIX gViewport;
int gMapMethod = MAP_PER_FACET;

float gHeight = 480.0f;
float gWidth = 640.0f;
float gXScreenOffset = (640.0f - 480.0f)/2.0f;

const MATRIX IdentityMatrix4x4 = {1.0f, 0.0f, 0.0f, 0.0f,
                                  0.0f, 1.0f, 0.0f, 0.0f,
                                  0.0f, 0.0f, 1.0f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 1.0f};

/******************************************************************************
 * SetCameraPos                                                               *
 *  Function: set the view camera position parameters                         *
 *    Inputs: pcamera - pointer to camera descriptor                          *
 *   Outputs: TRUE - camera position set                                      *
 *            FALSE - unable to set camera position                           *
 ******************************************************************************/

BOOL SetCameraPos (PCAMERA pcamera)
{
    if (!pcamera) return FALSE;

    // Copy new camera parameters to gView.

    memcpy (&gView.camera, pcamera, sizeof (CAMERA));

    // Recalculate view matrices.

    CalcViewParams ();

    return TRUE;
} // SetCameraPos


/******************************************************************************
 * SetCameraView                                                              *
 *  Function: set the view camera frustum parameters                          *
 *    Inputs: pfrustum - pointer to view frustum descriptor                   *
 *   Outputs: TRUE - view frustum set                                         *
 *            FALSE - unable to set view frustum                              *
 ******************************************************************************/

BOOL SetCameraView (PFRUSTUM pfrustum)
{
    if (!pfrustum) return FALSE;

    // Copy new camera parameters to gView.

    memcpy (&gView.frustum, pfrustum, sizeof (FRUSTUM));

    // Recalculate view matrices.

    CalcViewParams ();

    return TRUE;
} // SetCameraView


/******************************************************************************
 * SetViewPort                                                                *
 *  Function: set the view camera viewport parameters                         *
 *    Inputs: pviewport - pointer to viewport descriptor                      *
 *   Outputs: TRUE - viewport set                                             *
 *            FALSE - unable to set viewport                                  *
 ******************************************************************************/

BOOL SetViewPort (PVIEWPORT pviewport)
{
    if (!pviewport) return FALSE;

    // Copy new camera parameters to gView.

    memcpy (&gView.viewport, pviewport, sizeof (VIEWPORT));

    // Recalculate view matrices.

    CalcViewParams ();

    return TRUE;
} // SetViewPort


/******************************************************************************
 * TransposeMatrix                                                            *
 *  Function: compute the transpose of the source matrix                      *
 *    Inputs: origmat - original matrix to be transposed                      *
 *            tranmat - matrix to store transposed result                     *
 *   Outputs: none                                                            *
 ******************************************************************************/

void TransposeMatrix (MATRIX origmat, MATRIX tranmat)
{
    int i, j;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            tranmat[j][i] = origmat[i][j];
        } // for
    } // for
} // TransposeMatrix


/******************************************************************************
 * XRotateMatrix                                                              *
 *  Function: compute x axis rotation matrix                                  *
 *    Inputs: xangle - rotation angle about x axis in radians                 *
 *            tranmat - matrix to store rotation result                       *
 *   Outputs: none                                                            *
 ******************************************************************************/

void XRotateMatrix (float xangle, MATRIX xrotmat)
{
    float cosxangle, sinxangle;

    IdentityMatrix (xrotmat);

    cosxangle = (float) cos (xangle);
    sinxangle = (float) sin (xangle);

    xrotmat[1][1] = cosxangle;
    xrotmat[1][2] = -sinxangle;
    xrotmat[2][1] = sinxangle;
    xrotmat[2][2] = cosxangle;
} // XRotateMatrix


/******************************************************************************
 * YRotateMatrix                                                              *
 *  Function: compute y axis rotation matrix                                  *
 *    Inputs: yangle - rotation angle about y axis in radians                 *
 *            tranmat - matrix to store rotation result                       *
 *   Outputs: none                                                            *
 ******************************************************************************/

void YRotateMatrix (float yangle, MATRIX yrotmat)
{
    float cosyangle, sinyangle;

    cosyangle = (float) cos (yangle);
    sinyangle = (float) sin (yangle);

    IdentityMatrix (yrotmat);

    yrotmat[0][0] = cosyangle;
    yrotmat[0][2] = sinyangle;
    yrotmat[2][0] = -sinyangle;
    yrotmat[2][2] = cosyangle;
} // YRotateMatrix


/******************************************************************************
 * ZRotateMatrix                                                              *
 *  Function: compute z axis rotation matrix                                  *
 *    Inputs: zangle - rotation angle about z axis in radians                 *
 *            tranmat - matrix to store rotation result                       *
 *   Outputs: none                                                            *
 ******************************************************************************/

void ZRotateMatrix (float zangle, MATRIX zrotmat)
{
    float coszangle, sinzangle;

    coszangle = (float) cos (zangle);
    sinzangle = (float) sin (zangle);

    IdentityMatrix (zrotmat);

    zrotmat[0][0] = coszangle;
    zrotmat[0][1] = -sinzangle;
    zrotmat[1][0] = sinzangle;
    zrotmat[1][1] = coszangle;
} // ZRotateMatrix


/******************************************************************************
 * MultMatrix                                                                 *
 *  Function: multiply matrices                                               *
 *    Inputs: mat0 - multiplicand matrix                                      *
 *            mat1 - multiplier matrix                                        *
 *            result - matrix to store multiplication result                  *
 *   Outputs: none                                                            *
 ******************************************************************************/

void MultMatrix (MATRIX mat0, MATRIX mat1, MATRIX result)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        result[i][0] = mat0[i][0] * mat1[0][0] +
                       mat0[i][1] * mat1[1][0] +
                       mat0[i][2] * mat1[2][0] +
                       mat0[i][3] * mat1[3][0];

        result[i][1] = mat0[i][0] * mat1[0][1] +
                       mat0[i][1] * mat1[1][1] +
                       mat0[i][2] * mat1[2][1] +
                       mat0[i][3] * mat1[3][1];

        result[i][2] = mat0[i][0] * mat1[0][2] +
                       mat0[i][1] * mat1[1][2] +
                       mat0[i][2] * mat1[2][2] +
                       mat0[i][3] * mat1[3][2];

        result[i][3] = mat0[i][0] * mat1[0][3] +
                       mat0[i][1] * mat1[1][3] +
                       mat0[i][2] * mat1[2][3] +
                       mat0[i][3] * mat1[3][3];
    } // for
} // MultMatrix


/******************************************************************************
 * CopyMatrix                                                                 *
 *  Function: copy source to destination matrix                               *
 *    Inputs: src - source matrix                                             *
 *            dst - destination matrix                                        *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CopyMatrix (MATRIX src, MATRIX dst)
{
    memcpy (dst, src, sizeof (MATRIX));
} // CopyMatrix


/******************************************************************************
 * IdentityMatrix                                                             *
 *  Function: set identity matrix                                             *
 *    Inputs: imat - matrix to store 4x4 identity matrix                      *
 *   Outputs: none                                                            *
 ******************************************************************************/

void IdentityMatrix (MATRIX imat)
{
    memcpy (imat, IdentityMatrix4x4, sizeof (MATRIX));
} // IdentityMatrix


/******************************************************************************
 * TranslateMatrix                                                            *
 *  Function: set translation matrix                                          *
 *    Inputs: tx - translation direction along the x axis                     *
 *            ty - translation direction along the y axis                     *
 *            tz - translation direction along the z axis                     *
 *            tranmat - matrix to store translation result                    *
 *   Outputs: none                                                            *
 ******************************************************************************/

void TranslateMatrix (float tx, float ty, float tz, MATRIX tranmat)
{
    IdentityMatrix (tranmat);

    tranmat[0][3] = tx;
    tranmat[1][3] = ty;
    tranmat[2][3] = tz;
} // TranslateMatrix


/******************************************************************************
 * ScaleMatrix                                                                *
 *  Function: set scale matrix                                                *
 *    Inputs: sx - scaling factor along the x axis                            *
 *            sy - scaling factor along the y axis                            *
 *            sz - scaling factor along the z axis                            *
 *            tranmat - matrix to store scale result                          *
 *   Outputs: none                                                            *
 ******************************************************************************/

void ScaleMatrix (float sx, float sy, float sz, MATRIX scalmat)
{
    IdentityMatrix (scalmat);

    scalmat[0][0] = sx;
    scalmat[1][1] = sy;
    scalmat[2][2] = sz;
} // ScaleMatrix


/******************************************************************************
 * LoadMatrixRows                                                             *
 *  Function: load matrix rows with x, y, and z vectors                       *
 *    Inputs: R - matrix to store result                                      *
 *            row1 - vector to be stored in row 1 of matrix                   *
 *            row2 - vector to be stored in row 2 of matrix                   *
 *            row3 - vector to be stored in row 3 of matrix                   *
 *   Outputs: none                                                            *
 ******************************************************************************/

void LoadMatrixRows (MATRIX R, VECT row1, VECT row2, VECT row3)
{
    int i;

    for (i = 0; i < 3; i++)
    {
        R[0][i] = row1[i];
        R[1][i] = row2[i];
        R[2][i] = row3[i];
        R[3][i] = 0.0f;
    } // for

    R[0][3] = 0.0f;
    R[1][3] = 0.0f;
    R[2][3] = 0.0f;
    R[3][3] = 1.0f;
} // LoadMatrixRows


// Vector functions.

/******************************************************************************
 * CopyVector                                                                 *
 *  Function: copy source vector to destination vector                        *
 *    Inputs: src - source vector                                             *
 *            dst - destination vector                                        *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CopyVector (VECT src, VECT dst)
{
    memcpy (dst, src, sizeof (VECT));
} // CopyVector


/******************************************************************************
 * AddVector                                                                  *
 *  Function: add vectors                                                     *
 *    Inputs: vect1 - first vector                                            *
 *            vect2 - second vector                                           *
 *            result - vector to store result of vect1 + vect2                *
 *   Outputs: none                                                            *
 ******************************************************************************/

void AddVector (VECT vect1, VECT vect2, VECT result)
{
    result[0] = vect1[0] + vect2[0];
    result[1] = vect1[1] + vect2[1];
    result[2] = vect1[2] + vect2[2];
    result[3] = vect1[3] + vect2[3];
} // AddVector


/******************************************************************************
 * SubVector                                                                  *
 *  Function: subtract vectors                                                *
 *    Inputs: vect1 - first vector                                            *
 *            vect2 - second vector                                           *
 *            result - vector to store result of vect1 - vect2                *
 *   Outputs: none                                                            *
 ******************************************************************************/

void SubVector (VECT vect1, VECT vect2, VECT result)
{
    result[0] = vect1[0] - vect2[0];
    result[1] = vect1[1] - vect2[1];
    result[2] = vect1[2] - vect2[2];
    result[3] = vect1[3] - vect2[3];
} // SubVector


/******************************************************************************
 * VectMultVector                                                             *
 *  Function: multiply two vectors                                            *
 *    Inputs: vect1 - first vector                                            *
 *            vect2 - second vector                                           *
 *            result - vector to store multiplication result                  *
 *   Outputs: none                                                            *
 ******************************************************************************/

void VectMultVector (VECT vect1, VECT vect2, VECT result)
{
    result[0] = vect1[0] * vect2[0];
    result[1] = vect1[1] * vect2[1];
    result[2] = vect1[2] * vect2[2];
    result[3] = vect1[3] * vect2[3];
} // VectMultVector


/******************************************************************************
 * ScaleMultVector                                                            *
 *  Function: multiply vector by scalar                                       *
 *    Inputs: factor - scaling factor                                         *
 *            vect - vector to be scaled                                      *
 *   Outputs: none                                                            *
 ******************************************************************************/

void ScalMultVector (float factor, VECT vect)
{
    vect[0] *= factor;
    vect[1] *= factor;
    vect[2] *= factor;
    vect[3] *= factor;
} // ScalMultVector


/******************************************************************************
 * NormVector                                                                 *
 *  Function: normalize vector (i.e. scale vector so it becomes unit length)  *
 *    Inputs: vect - vector to be normalized                                  *
 *   Outputs: none                                                            *
 ******************************************************************************/

void NormVector (VECT vect)
{
    float len, invlen;

    len = VectorLength (vect);

    invlen = 1.0f/len;

    vect[0] *= invlen;
    vect[1] *= invlen;
    vect[2] *= invlen;
    vect[3] *= invlen;
} // NormVector


/******************************************************************************
 * Cross                                                                      *
 *  Function: compute cross product of vectors                                *
 *    Inputs: vect1 - first vector                                            *
 *            vect2 - second vector                                           *
 *            result - vector to store result of cross product                *
 *   Outputs: none                                                            *
 ******************************************************************************/

void Cross (VECT vect1, VECT vect2, VECT result)
{
    result[0] = vect1[1]*vect2[2] - vect1[2]*vect2[1];
    result[1] = vect1[2]*vect2[0] - vect1[0]*vect2[2];
    result[2] = vect1[0]*vect2[1] - vect1[1]*vect2[0];
    result[3] = 1.0f;
} // Cross


/******************************************************************************
 * FacetNormal                                                                *
 *  Function: compute facet normals for 3D object                             *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - facet normals successfully computed                      *
 *            FALSE - unable to compute facet normals                         *
 ******************************************************************************/

BOOL FacetNormal (OBJECT *lpObject)
{
    int     i;
    VERTEX  *pv1, *pv2, *pv3;
    VECT    vect1, vect2, n;
    FACET   *lpfacet;

    if (!lpObject) return FALSE;

    lpfacet = &lpObject->facets[0];

    // Calculate facet normal for every facet in render list.

    for (i = 0; i < (int) lpObject->num_facets; i++, lpfacet++)
    {
        // Facets that are really just points or line have no real normal.

        if (lpfacet->num_verts < 3)
        {
            lpfacet->normal[0] = 0.0f;
            lpfacet->normal[1] = 0.0f;
            lpfacet->normal[2] = 0.0f;
            continue;
        } // if

        // Use indices to get pointers to vertices.
        // The vertices are in the order:
        //
        //   v3 <-------- v2
        //                ^
        //                |
        //                |
        //                |
        //                v1

        pv1 = &lpObject->vertices[lpfacet->vertices[0]];
        pv2 = &lpObject->vertices[lpfacet->vertices[1]];
        pv3 = &lpObject->vertices[lpfacet->vertices[2]];

        // Generate vectors between points.

        SubVector (pv3->world_coords, pv2->world_coords, vect1);
        SubVector (pv2->world_coords, pv1->world_coords, vect2);

        // Normalize vectors and compute normal to them from their cross product.

        Cross (vect2, vect1, n);
        NormVector (n);
        n[3] = 0.0f;

        if (lpObject->flip_normals)
        {
            ScalMultVector (-1.0f, n);
        } // if

        CopyVector (n, lpfacet->normal);
    } // for

    return TRUE;
} // FacetNormal


/******************************************************************************
 * VertexNormal                                                               *
 *  Function: compute vertex normals for 3D object                            *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - vertex normals successfully computed                     *
 *            FALSE - unable to compute vertex normals                        *
 ******************************************************************************/

BOOL VertexNormal (OBJECT *lpObject)
{
    VECT    sumvect;
    int     i, v;
    VERTEX  *lpvertex;

    if (!lpObject) return FALSE;

    lpvertex = &lpObject->vertices[0];

    for (v = 0; v < (int) lpObject->num_verts; v++, lpvertex++)
    {
        // Init summation vector.

        sumvect[0] = 0.0f;
        sumvect[1] = 0.0f;
        sumvect[2] = 0.0f;
        sumvect[3] = 0.0f;

        for (i = 0; i < lpvertex->num_facets; i++)
        {
            sumvect[0] += lpObject->facets[lpvertex->facets[i]].normal[0];
            sumvect[1] += lpObject->facets[lpvertex->facets[i]].normal[1];
            sumvect[2] += lpObject->facets[lpvertex->facets[i]].normal[2];
        } // for

        // Normalize vector.

        NormVector (sumvect);

        // Set vertex normal.

        CopyVector (sumvect, lpvertex->normal);
    } // for
    return TRUE;
} // VertexNormal


/******************************************************************************
 * VertexRGB                                                                  *
 *  Function: compute vertex RGB colors for 3D object                         *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - vertex color successfully computed                       *
 *            FALSE - unable to compute vertex color                          *
 ******************************************************************************/

BOOL VertexRGB (OBJECT *lpObject)
{
    int     i, numverts;
    VERTEX  *v;
    int     n;
    VECT    light_vect, lightloc;
    VECT    Ia, Id, Is;
    float   LdotN, NnH;

    if (!lpObject) return FALSE;

    numverts = lpObject->num_verts;
    v = &lpObject->vertices[0];

    // Project vertices by current matrix.

    for (i = 0; i < numverts; i++, v++)
    {
        // Initialize RGB.

        v->render_data.r = 0.0f;
        v->render_data.g = 0.0f;
        v->render_data.b = 0.0f;

        for (n = 0; n < gView.num_lights; n++)
        {
            // Get light's position & ambient, diffuse and specular cooefficients.

            Ia[0] = gView.lights[n].light.ambient.x;
            Ia[1] = gView.lights[n].light.ambient.y;
            Ia[2] = gView.lights[n].light.ambient.z;

            Id[0] = gView.lights[n].light.diffuse.x;
            Id[1] = gView.lights[n].light.diffuse.y;
            Id[2] = gView.lights[n].light.diffuse.z;

            Is[0] = gView.lights[n].light.specular.x;
            Is[1] = gView.lights[n].light.specular.y;
            Is[2] = gView.lights[n].light.specular.z;

            // ambient intensity = ambient light * surface ambient term

            v->render_data.r += lpObject->material.ambient.x * Ia[0];
            v->render_data.g += lpObject->material.ambient.y * Ia[1];
            v->render_data.b += lpObject->material.ambient.z * Ia[2];

            // Calculate vector from vertex to light.

            lightloc[0] = gView.lights[n].light.world_coords.x;
            lightloc[1] = gView.lights[n].light.world_coords.y;
            lightloc[2] = gView.lights[n].light.world_coords.z;
            lightloc[3] = 0.0f;

            // SubVector (lightloc, v->world_coords, light_vect);

            light_vect[0] = lightloc[0] - v->world_coords[0];
            light_vect[1] = lightloc[1] - v->world_coords[1];
            light_vect[2] = lightloc[2] - v->world_coords[2];
            light_vect[3] = lightloc[3] - v->world_coords[3];

            NormVector (light_vect);

            // Calculate dot product of normal and vector to light.

            if ((LdotN = Dot (light_vect, v->normal)) <= 0.0) continue;

            // Scale diffuse intensity by dot product and distance factors and add to RGB.

            Id[0] *= LdotN;
            Id[1] *= LdotN;
            Id[2] *= LdotN;
            Id[3] *= LdotN;

            v->render_data.r += lpObject->material.diffuse.x * Id[0];
            v->render_data.g += lpObject->material.diffuse.y * Id[1];
            v->render_data.b += lpObject->material.diffuse.z * Id[2];

            // Scale specular intensity by dot product raised to power
            // and distance factors and add to RGB.

            NnH = (float) pow (LdotN, lpObject->material.shinyness);
            Is[0] *= NnH;
            Is[1] *= NnH;
            Is[2] *= NnH;
            Is[3] *= NnH;

            v->render_data.r += lpObject->material.specular.x * Is[0];
            v->render_data.g += lpObject->material.specular.y * Is[1];
            v->render_data.b += lpObject->material.specular.z * Is[2];
        } // for

        // Ensure that 0.0 <= rgb <= 1.0.

        v->render_data.r = CLAMP (v->render_data.r, 0.0f, 1.0f);
        v->render_data.g = CLAMP (v->render_data.g, 0.0f, 1.0f);
        v->render_data.b = CLAMP (v->render_data.b, 0.0f, 1.0f);
        v->render_data.r *= 255.0f;
        v->render_data.g *= 255.0f;
        v->render_data.b *= 255.0f;
    } // for

    return TRUE;
} // VertexRGB


/******************************************************************************
 * CullBackface                                                               *
 *  Function: mark back facing facets                                         *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - successfully marked                                      *
 *            FALSE - unsuccessful                                            *
 ******************************************************************************/

BOOL CullBackface (OBJECT *lpObject)
{
    C3D_UINT16  i;
    VERTEX  *pv1;
    VECT    camloc, camtovert;
    FACET   *lpfacet;
    float   dot;

    if (!lpObject) return FALSE;

    lpObject->nNumPrimListVerts = 0;
    lpObject->num_facetlist = 0;

    lpfacet = &lpObject->facets[0];

    if (gView.projmode == PROJ_PERSPECTIVE)
    {
        for (i = 0; i < lpObject->num_facets; i++, lpfacet++)
        {
            // All visibility bits get reset except EXCLUDE
            // so that if the user has explicitly excluded
            // facets they will stay invisible until the
            // user explicity includes them.

            lpfacet->flags &= ~(BACKFACE | ACCEPT | REJECT);

            // Points and lines cannot be back facing
            // so just accept them.

            if (lpfacet->num_verts < 3)
            {
                lpfacet->flags |= ACCEPT;
                //lpObject->facetlist[lpObject->num_facetlist++] = i;
                lpObject->facetlist[lpObject->num_facetlist++] = lpfacet;
                continue;
            } // if

            pv1 = &lpObject->vertices[lpfacet->vertices[0]];
            camloc[0] = gView.camera.location.x;
            camloc[1] = gView.camera.location.y;
            camloc[2] = gView.camera.location.z;
            camloc[3] = 0.0f;
            SubVector (camloc, pv1->world_coords, camtovert);

            // Dot product.

            dot = (camtovert[0] * lpfacet->normal[0]) +
                  (camtovert[1] * lpfacet->normal[1]) +
                  (camtovert[2] * lpfacet->normal[2]);

            if (dot <= 0.0f)
            {
                lpfacet->flags |= BACKFACE;
            }
            else
            {
                lpfacet->flags |= ACCEPT;
                //lpObject->facetlist[lpObject->num_facetlist++] = i;
                lpObject->facetlist[lpObject->num_facetlist++] = lpfacet;
            } // if
        } // for
    }
    else
    {
        // Handle parallel projection case.

        for (i = 0; i < lpObject->num_facets; i++, lpfacet++)
        {
            // All visibility bits get reset except EXCLUDE
            // so that if the user has explicitly excluded
            // facets they will stay invisible until the
            // user explicity includes them.

            lpfacet->flags &= ~(BACKFACE | ACCEPT | REJECT);

            // Points and lines cannot be back facing
            // so just accept them.

            if (lpfacet->num_verts < 3)
            {
                lpfacet->flags |= ACCEPT;
                //lpObject->facetlist[lpObject->num_facetlist++] = i;
                lpObject->facetlist[lpObject->num_facetlist++] = lpfacet;
                continue;
            } // if

            if (lpfacet->normal[2] <= 0.0)
            {
                lpfacet->flags |= BACKFACE;
            }
            else
            {
                lpfacet->flags |= ACCEPT;
                //lpObject->facetlist[lpObject->num_facetlist++] = i;
                lpObject->facetlist[lpObject->num_facetlist++] = lpfacet;
            } // if
        } // for
    } // if

    return TRUE;
} // CullBackface


/******************************************************************************
 * TransformVertices                                                          *
 *  Function: compute world coordinates by transforming model coordinates     *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *            mat - matrix containing transformation                          *
 *   Outputs: TRUE - object successfully transformed                          *
 *            FALSE - unable to transform object                              *
 ******************************************************************************/

BOOL TransformVertices (OBJECT *lpObject, MATRIX mat)
{
    int i;

    if (!lpObject) return FALSE;

    for (i = 0; i < (int) lpObject->num_verts; i++)
    {
/* the following loop has been unrolled to improve performance
        int    i, k, j;
        for (k = 0; k < 4; k++)
        {
            lpObject->vertices[i].world_coords[k] = 0.0;
            for (j = 0; j < 4; j++)
            {
                lpObject->vertices[i].world_coords[k] +=
                    mat[k][j] * lpObject->vertices[i].model_coords[j];
            } // for
        } // for
*/
        // k = 0

        lpObject->vertices[i].world_coords[0] =
            mat[0][0] * lpObject->vertices[i].model_coords[0] +
            mat[0][1] * lpObject->vertices[i].model_coords[1] +
            mat[0][2] * lpObject->vertices[i].model_coords[2] +
            mat[0][3] * lpObject->vertices[i].model_coords[3];

        // k = 1

        lpObject->vertices[i].world_coords[1] =
            mat[1][0] * lpObject->vertices[i].model_coords[0] +
            mat[1][1] * lpObject->vertices[i].model_coords[1] +
            mat[1][2] * lpObject->vertices[i].model_coords[2] +
            mat[1][3] * lpObject->vertices[i].model_coords[3];

        // k = 2

        lpObject->vertices[i].world_coords[2] =
            mat[2][0] * lpObject->vertices[i].model_coords[0] +
            mat[2][1] * lpObject->vertices[i].model_coords[1] +
            mat[2][2] * lpObject->vertices[i].model_coords[2] +
            mat[2][3] * lpObject->vertices[i].model_coords[3];

        // k = 3

        lpObject->vertices[i].world_coords[3] =
            mat[3][0] * lpObject->vertices[i].model_coords[0] +
            mat[3][1] * lpObject->vertices[i].model_coords[1] +
            mat[3][2] * lpObject->vertices[i].model_coords[2] +
            mat[3][3] * lpObject->vertices[i].model_coords[3];
    } // for

    return TRUE;
} // TransformVertices


/******************************************************************************
 * TransformObject                                                            *
 *  Function: map transformed vertices in world coordinates into canonical    *
 *            (normalized) view volume                                                    *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - object successfully transformed                          *
 *            FALSE - unable to transform object                              *
 ******************************************************************************/

BOOL TransformObject (OBJECT *lpObject)
{
    int     i;
    VERTEX  *pv;

    if (!lpObject) return FALSE;

    pv = &lpObject->vertices[0];

    // Transform vertices by current matrix.

    for (i = 0; i < (int) lpObject->num_verts; i++, pv++)
    {
/* the following loop has been unrolled to improve performance
    int     i, k, j;
        for (k = 0; k < 4; k++)
        {
            pv->norm_volume_coords[k] = 0.0f;
            for (j = 0; j < 4; j++)
            {
                pv->norm_volume_coords[k] += gView.N[k][j] * pv->world_coords[j];
            } // for
        } // for
*/
        pv->norm_volume_coords[0] = gView.N[0][0] * pv->world_coords[0] +
                                    gView.N[0][1] * pv->world_coords[1] +
                                    gView.N[0][2] * pv->world_coords[2] +
                                    gView.N[0][3] * pv->world_coords[3];

        pv->norm_volume_coords[1] = gView.N[1][0] * pv->world_coords[0] +
                                    gView.N[1][1] * pv->world_coords[1] +
                                    gView.N[1][2] * pv->world_coords[2] +
                                    gView.N[1][3] * pv->world_coords[3];

        pv->norm_volume_coords[2] = gView.N[2][0] * pv->world_coords[0] +
                                    gView.N[2][1] * pv->world_coords[1] +
                                    gView.N[2][2] * pv->world_coords[2] +
                                    gView.N[2][3] * pv->world_coords[3];

        pv->norm_volume_coords[3] = gView.N[3][0] * pv->world_coords[0] +
                                    gView.N[3][1] * pv->world_coords[1] +
                                    gView.N[3][2] * pv->world_coords[2] +
                                    gView.N[3][3] * pv->world_coords[3];
    } // for

    return TRUE;
} // TransformObject


/******************************************************************************
 * ProjectObject                                                              *
 *  Function: map vertices from canonical volume to screen coordinates        *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - object successfully transformed                          *
 *            FALSE - unable to transform object                              *
 ******************************************************************************/

BOOL ProjectObject (OBJECT *lpObject)
{
    int     i;
    VERTEX  *pv;
    VECT    tmp;
    float   invtmp3;

    if (!lpObject) return FALSE;

    pv = &lpObject->vertices[0];

    // Project vertices by current matrix.

    for (i = 0; i < (int) lpObject->num_verts; i++, pv++)
    {
/* the following loop has been unrolled to improve performance
    long    i, k, j;
        for (k = 0; k < 4; k++)
        {
            tmp[k] = 0.0f;
            for (j = 0; j < 4; j++)
            {
                tmp[k] += gView.VP[k][j] * pv->norm_volume_coords[j];
            } // for
        } // for
        invtmp3 = 1.0f / tmp[3];
        pv->render_data.x = tmp[0] * invtmp3;
        pv->render_data.y = tmp[1] * invtmp3;
        pv->render_data.z = tmp[2] * invtmp3;
*/
        tmp[0] = gView.VP[0][0] * pv->norm_volume_coords[0] +
                 gView.VP[0][1] * pv->norm_volume_coords[1] +
                 gView.VP[0][2] * pv->norm_volume_coords[2] +
                 gView.VP[0][3] * pv->norm_volume_coords[3];

        tmp[1] = gView.VP[1][0] * pv->norm_volume_coords[0] +
                 gView.VP[1][1] * pv->norm_volume_coords[1] +
                 gView.VP[1][2] * pv->norm_volume_coords[2] +
                 gView.VP[1][3] * pv->norm_volume_coords[3];

        tmp[2] = gView.VP[2][0] * pv->norm_volume_coords[0] +
                 gView.VP[2][1] * pv->norm_volume_coords[1] +
                 gView.VP[2][2] * pv->norm_volume_coords[2] +
                 gView.VP[2][3] * pv->norm_volume_coords[3];

        tmp[3] = gView.VP[3][0] * pv->norm_volume_coords[0] +
                 gView.VP[3][1] * pv->norm_volume_coords[1] +
                 gView.VP[3][2] * pv->norm_volume_coords[2] +
                 gView.VP[3][3] * pv->norm_volume_coords[3];

        invtmp3 = 1.0f / tmp[3];
        pv->render_data.x = (tmp[0] * invtmp3) + gXScreenOffset;
        pv->render_data.y = gHeight - (tmp[1] * invtmp3);
        pv->render_data.z = tmp[2] * invtmp3;
        pv->render_data.w = invtmp3;
        pv->render_data.s = pv->texture_coords[0] * pv->render_data.w;
        pv->render_data.t = pv->texture_coords[1] * pv->render_data.w;
    } // for

    return TRUE;
} // ProjectObject


/******************************************************************************
 * UpdateObject                                                               *
 *  Function: update object geometry and color parameters (called when        *
 *            updating frames)                                                *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *            mat - matrix containing transformation                          *
 *   Outputs: TRUE - object successfully updated                              *
 *            FALSE - unable to update object                                 *
 ******************************************************************************/

BOOL UpdateObject (OBJECT *lpObject, MATRIX mat)
{
    PFACET lpfacet;
    int i, j;

    if (!lpObject) return FALSE;

    TransformVertices (lpObject, mat);
    FacetNormal (lpObject);
    VertexNormal (lpObject);
    VertexRGB (lpObject);
    TransformObject (lpObject);
    CullBackface (lpObject);
    ProjectObject (lpObject);
    QSortFacetArray (lpObject, 0, lpObject->num_facetlist - 1);

    lpObject->nNumPrimListVerts = 0;

    for (i = 0; i < (int) lpObject->num_facetlist; i++)
    {
        lpfacet = lpObject->facetlist[i];
        for (j = 0; j < (int) lpfacet->num_verts; j++)
        {
            lpObject->vlstPrimList[lpObject->nNumPrimListVerts++] =
                &(lpObject->vertices[lpfacet->vertices[j]].render_data);
        } // for
    } // for

    return TRUE;
} // UpdateObject


/******************************************************************************
 * CalcViewParams                                                             *
 *  Function: initialize camera view model parameters (represented by gView)  *
 *    Inputs: none                                                            *
 *   Outputs: none                                                            *
 ******************************************************************************/

void CalcViewParams (void)
{
    VECT n, u, v;
    VECT camlookat, camloc, camup;
    MATRIX T, S;
    MATRIX gViewport,           // projection to screen coords
           M_per_to_par,        // map perspective volume to parallel volume
           T_viewport_origin,   // translate (-1, -1) corner of frustum to origin
           S_viewport,          // Scale volume face to viewport
           T_viewport_corner,   // translate volume corner relative to viewport origin
           TMP;

    float dist, front, back, new_dist;
    float umin, umax, vmin, vmax;
    float umid, vmid;
    float xscale, yscale, zscale;
    float scr_xmin, scr_ymin;
    float scr_xmax, scr_ymax;
    float scr_xdelta, scr_ydelta;

    // The values in the file specify the distance of the view plane and
    // clipping planes from the eye point.  We negate all of them, because once
    // the eye is at the origin they will all be in the negative z range.

    dist = -gView.frustum.dist;
    front = -gView.frustum.front;
    back = -gView.frustum.back;

    umin = gView.frustum.left;
    umax = gView.frustum.right;
    vmin = gView.frustum.bottom;
    vmax = gView.frustum.top;
    umid = (umin + umax)/2.0f;
    vmid = (vmin + vmax)/2.0f;

    // Initialize screen viewport min, max x,y values
    // as well as screen viewport x, y delta.

    scr_xmin = gView.viewport.left;
    scr_xmax = gView.viewport.right;
    scr_ymin = gView.viewport.top;
    scr_ymax = gView.viewport.bottom;
    scr_xdelta = (scr_xmax - scr_xmin) - 1;
    scr_ydelta = (scr_ymax - scr_ymin) - 1;

    // Derive vector normal to view plane.

    camloc[0] = gView.camera.location.x;
    camloc[1] = gView.camera.location.y;
    camloc[2] = gView.camera.location.z;
    camloc[3] = 0.0f;

    camlookat[0] = gView.camera.lookat.x;
    camlookat[1] = gView.camera.lookat.y;
    camlookat[2] = gView.camera.lookat.z;
    camlookat[3] = 0.0f;

    camup[0] = gView.camera.upvector.x;
    camup[1] = gView.camera.upvector.y;
    camup[2] = gView.camera.upvector.z;
    camup[3] = 0.0f;

    SubVector (camloc, camlookat, n);
    NormVector (n);
    NormVector (camup);

    // Generate view plane horizontal vector from cross product of n and up vector.

    Cross (camup, n, u);
    NormVector (u);

    // Generate true view plane up vector from cross product of n and u vectors.

    Cross (n, u, v);
    NormVector (v);

    // Build rotation matrix from u, v, n which are the vectors that will rotate
    // to become the world coordinate axes (equation 6.27 Foley & van Dam p.261)

    //IdentityMatrix (gView.R_axis_align);
    LoadMatrixRows (gView.R_axis_align, u, v, n);

    TransposeMatrix (gView.R_axis_align, gView.invR);

    // Initialize translation of camera to world origin
    // and combine rotation, translation.

    TranslateMatrix (-gView.camera.location.x, -gView.camera.location.y,
                     -gView.camera.location.z, T);

    MultMatrix (gView.R_axis_align, T, TMP);

    if (back > -0.00000001) back = -1.0f;

    if (dist > -0.00000001) dist = -1.0f;

    // Set up scale matrix for converting to normalized coordinates for clipping
    // against canonical view frustum (equation 6.39 Foley & van Dam p.269)

    xscale = (2.0f * dist)/((umax - umin)*back);
    yscale = (2.0f * dist)/((vmax - vmin)*back);
    zscale = -1.0f/back;

    ScaleMatrix (xscale, yscale, zscale, S);

    // Multiply scale matrix by comination of axis rotation matrix and camera
    // location translation matrix to generate normalizing clipping matrix
    // (view orientation matrix equation 6.41 Foly & van Dam p.270)

    MultMatrix (S, TMP, gView.N);

    // dist and back are both negative so result will be positive.

    new_dist = dist/back;

    // Calculate new zmin since it has been scaled by the normalization
    // transformation.

    gView.zmin = front * zscale;

    // Set up matrix to transform to parallel projection canonical clip volume
    // (equation 6.48 Foley & van Dam p.275)

    IdentityMatrix (M_per_to_par);
    M_per_to_par[2][2] = 1.0f/(1.0f + gView.zmin);
    M_per_to_par[2][3] = -gView.zmin/(1.0f + gView.zmin);
    M_per_to_par[3][2] = -1.0f;
    M_per_to_par[3][3] = 0.0f;

    // Set up view transformation.  Here we are transforming from normalized
    // clip coordinates to screen coordinates.  The clip coordinates range
    // from -1 to +1 because this is what the view plane normalized to.
    // (equation 6.54 Foley & van Dam p.278)

    TranslateMatrix (1.0f, 1.0f, 0.0f, T_viewport_origin);
    ScaleMatrix (scr_xdelta/2.0f, scr_ydelta/2.0f, 1.0f, S_viewport);
    TranslateMatrix (scr_xmin, scr_ymin, 0.0f, T_viewport_corner);

    MultMatrix (S_viewport, T_viewport_origin, TMP);
    MultMatrix (T_viewport_corner, TMP, gViewport);
    MultMatrix (gViewport, M_per_to_par, gView.VP);

    MultMatrix (gView.VP, gView.N, gView.VPN);

} // CalcViewParams


/******************************************************************************
 * TexSetup                                                                   *
 *  Function: initialize texture coordinates (s, t and w). Coordinates may be *
 *            initialized to map textures over the entire object or per facet *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *            mat - matrix containing transformation                          *
 *   Outputs: TRUE - texture setup successful                                 *
 *            FALSE - unable to setup texture                                 *
 ******************************************************************************/

BOOL TexSetup (POBJECT lpObject, MATRIX mat)
{
    ULONG   i, j;
    float   xscale, yscale, xtmp, ytmp;
    MATRIX  tmpmat;
    int     face_compare;
    VECT    s_vect, t_vect, f_norm[6], facet_norm;
    float   dot_result;
    VERTEX  *v1, *v2, *v3, v4;
    PFACET  lpfacet;
    float   tmp_x, tmp_y, tmp_z, dist1, dist2;

    // Inverse-scale the original model matrix.

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 4; j++)
        {
            if (mat[i][j] != 0.0f)
            {
                tmpmat[i][j] = 1.0f / mat[i][j];
            }
            else
            {
                tmpmat[i][j] = 0.0f;
            } // if
        } // for
    } // for

    if (!CalcViewBoundingBox (lpObject)) return FALSE;

    // Apply the inverse-scaled matrix.

    for (i = 0; i < 8; i++)
    {
        PointMultMatrix (gView.bounds.world_pts[i], tmpmat,
                         gView.bounds.world_pts[i]);
    } // for

    xscale = gView.bounds.world_pts[5][0] - gView.bounds.world_pts[4][0];
    yscale = gView.bounds.world_pts[7][1] - gView.bounds.world_pts[4][1];

    if (gMapMethod == MAP_PER_OBJECT)
    {
        for (i = 0; i < (ULONG) lpObject->num_verts; i++)
        {
            xtmp = lpObject->vertices[i].model_coords[0] - gView.bounds.world_pts[4][0];
            ytmp = lpObject->vertices[i].model_coords[1] - gView.bounds.world_pts[4][1];

            lpObject->vertices[i].texture_coords[0] = xtmp / xscale;
            lpObject->vertices[i].texture_coords[1] = ytmp / yscale;
            lpObject->vertices[i].texture_coords[2] = 1.0f;
        } // for
    }
    else if (gMapMethod == MAP_PER_FACET)
    {
        // Face 0.

        SubVector (gView.bounds.world_pts[6], gView.bounds.world_pts[5], s_vect);
        SubVector (gView.bounds.world_pts[1], gView.bounds.world_pts[5], t_vect);
        Cross (s_vect, t_vect, f_norm[0]);
        NormVector (f_norm[0]);

        // Face 1.

        SubVector (gView.bounds.world_pts[5], gView.bounds.world_pts[4], s_vect);
        SubVector (gView.bounds.world_pts[0], gView.bounds.world_pts[4], t_vect);
        Cross (s_vect, t_vect, f_norm[1]);
        NormVector (f_norm[1]);

        // Face 2.

        SubVector (gView.bounds.world_pts[1], gView.bounds.world_pts[0], s_vect);
        SubVector (gView.bounds.world_pts[3], gView.bounds.world_pts[0], t_vect);
        Cross (s_vect, t_vect, f_norm[2]);
        NormVector (f_norm[2]);

        // Face 3.

        SubVector (gView.bounds.world_pts[6], gView.bounds.world_pts[2], s_vect);
        SubVector (gView.bounds.world_pts[3], gView.bounds.world_pts[2], t_vect);
        Cross (s_vect, t_vect, f_norm[3]);
        NormVector (f_norm[3]);

        // Face 4.

        SubVector (gView.bounds.world_pts[7], gView.bounds.world_pts[3], s_vect);
        SubVector (gView.bounds.world_pts[0], gView.bounds.world_pts[3], t_vect);
        Cross (s_vect, t_vect, f_norm[4]);
        NormVector (f_norm[4]);

        // Face 5.

        SubVector (gView.bounds.world_pts[5], gView.bounds.world_pts[6], s_vect);
        SubVector (gView.bounds.world_pts[7], gView.bounds.world_pts[6], t_vect);
        Cross (s_vect, t_vect, f_norm[5]);
        NormVector (f_norm[5]);

        for (i = 0; i < (ULONG) lpObject->num_verts; i++)
        {
            for (j = 0; j < 4; j++)
            {
                facet_norm[j] = lpObject->vertices[i].normal[j];
            } // for

            for (j = 0; j < 6; j++)
            {
                dot_result = Dot (facet_norm, f_norm[j]);

                if (dot_result >= 0.5f)
                {
                    face_compare = j;
                    break;
                } // if
            } // for

            switch (face_compare)
            {
                case 0:
                    xscale = gView.bounds.world_pts[6][1] - gView.bounds.world_pts[5][1];
                    yscale = gView.bounds.world_pts[1][2] - gView.bounds.world_pts[5][2];

                    xtmp = lpObject->vertices[i].model_coords[1] - gView.bounds.world_pts[5][1];
                    ytmp = lpObject->vertices[i].model_coords[2] - gView.bounds.world_pts[5][2];
                    break;

                case 1:
                    xscale = gView.bounds.world_pts[5][0] - gView.bounds.world_pts[4][0];
                    yscale = gView.bounds.world_pts[0][2] - gView.bounds.world_pts[4][2];

                    xtmp = lpObject->vertices[i].model_coords[0] - gView.bounds.world_pts[4][0];
                    ytmp = lpObject->vertices[i].model_coords[2] - gView.bounds.world_pts[4][2];
                    break;

                case 2:
                    xscale = gView.bounds.world_pts[1][0] - gView.bounds.world_pts[0][0];
                    yscale = gView.bounds.world_pts[3][1] - gView.bounds.world_pts[0][1];

                    xtmp = lpObject->vertices[i].model_coords[0] - gView.bounds.world_pts[0][0];
                    ytmp = lpObject->vertices[i].model_coords[1] - gView.bounds.world_pts[0][1];
                    break;

                case 3:
                    xscale = gView.bounds.world_pts[6][2] - gView.bounds.world_pts[2][2];
                    yscale = gView.bounds.world_pts[3][0] - gView.bounds.world_pts[2][0];

                    xtmp = lpObject->vertices[i].model_coords[2] - gView.bounds.world_pts[2][2];
                    ytmp = lpObject->vertices[i].model_coords[0] - gView.bounds.world_pts[2][0];
                    break;

                case 4:
                    xscale = gView.bounds.world_pts[7][2] - gView.bounds.world_pts[3][2];
                    yscale = gView.bounds.world_pts[0][1] - gView.bounds.world_pts[3][1];

                    xtmp = lpObject->vertices[i].model_coords[2] - gView.bounds.world_pts[3][2];
                    ytmp = lpObject->vertices[i].model_coords[1] - gView.bounds.world_pts[3][1];
                    break;

                case 5:
                    xscale = gView.bounds.world_pts[5][1] - gView.bounds.world_pts[6][1];
                    yscale = gView.bounds.world_pts[7][0] - gView.bounds.world_pts[6][0];

                    xtmp = lpObject->vertices[i].model_coords[1] - gView.bounds.world_pts[6][1];
                    ytmp = lpObject->vertices[i].model_coords[0] - gView.bounds.world_pts[6][0];
                    break;

                default:
                    break;
            } // switch

            lpObject->vertices[i].texture_coords[0] = xtmp / xscale;
            lpObject->vertices[i].texture_coords[1] = ytmp / yscale;
            lpObject->vertices[i].texture_coords[2] = 1.0f;
        } // for
    }
    else
    {
        // Alternate per-facet method.

        lpfacet = &lpObject->facets[0];

        // Calculate s and t for every facet in render_list.

        for (i = 0; i < (ULONG) lpObject->num_facets; i++, lpfacet++)
        {
            if (lpfacet->num_verts < 3) continue;

            v1 = &lpObject->vertices[lpfacet->vertices[0]];
            v2 = &lpObject->vertices[lpfacet->vertices[1]];
            v3 = &lpObject->vertices[lpfacet->vertices[2]];

            SubVector (v2->world_coords, v1->world_coords, s_vect);
            Cross (lpfacet->normal, s_vect, t_vect);

            v4.world_coords[0] = v3->world_coords[0];
            for (j = 1; j < 4; j++)
            {
                v4.world_coords[j] = v2->world_coords[j];
            } // for

            tmp_x = v2->world_coords[0] - v1->world_coords[0];
            tmp_y = v2->world_coords[1] - v1->world_coords[1];
            tmp_z = v2->world_coords[2] - v1->world_coords[2];

            dist1 = (float) sqrt (tmp_x*tmp_x + tmp_y*tmp_y + tmp_z*tmp_z);

            tmp_x = v4.world_coords[0] - v1->world_coords[0];
            tmp_y = v4.world_coords[1] - v1->world_coords[1];
            tmp_z = v4.world_coords[2] - v1->world_coords[2];

            dist2 = (float) sqrt (tmp_x*tmp_x + tmp_y*tmp_y + tmp_z*tmp_z);

            if (dist1 >= dist2)
            {
                lpObject->vertices[lpfacet->vertices[0]].texture_coords[0] = 0.0f;
                lpObject->vertices[lpfacet->vertices[1]].texture_coords[1] = 1.0f;
                lpObject->vertices[lpfacet->vertices[2]].texture_coords[2] = dist2 / dist1;
            }
            else
            {
                // dist1 < dist2

                lpObject->vertices[lpfacet->vertices[0]].texture_coords[0] = 0.0f;
                lpObject->vertices[lpfacet->vertices[1]].texture_coords[1] = dist1 / dist2;
                lpObject->vertices[lpfacet->vertices[2]].texture_coords[2] = 1.0f;
            } // if

            v4.world_coords[1] = v3->world_coords[1];
            for (j = 1; j < 4; j++)
            {
                v4.world_coords[j] = v1->world_coords[j];
            } // for

            tmp_x = v2->world_coords[0] - v1->world_coords[0];
            tmp_y = v2->world_coords[1] - v1->world_coords[1];
            tmp_z = v2->world_coords[2] - v1->world_coords[2];

            dist1 = (float) sqrt (tmp_x*tmp_x + tmp_y*tmp_y + tmp_z*tmp_z);

            tmp_x = v4.world_coords[0] - v1->world_coords[0];
            tmp_y = v4.world_coords[1] - v1->world_coords[1];
            tmp_z = v4.world_coords[2] - v1->world_coords[2];

            dist2 = (float) sqrt (tmp_x*tmp_x + tmp_y*tmp_y + tmp_z*tmp_z);

            if (dist1 >= dist2)
            {
                lpObject->vertices[lpfacet->vertices[0]].texture_coords[0] = 0.0f;
                lpObject->vertices[lpfacet->vertices[1]].texture_coords[1] = 1.0f;
                lpObject->vertices[lpfacet->vertices[2]].texture_coords[2] = dist2 / dist1;
            }
            else
            {
                // dist1 < dist2

                lpObject->vertices[lpfacet->vertices[0]].texture_coords[0] = 0.0f;
                lpObject->vertices[lpfacet->vertices[1]].texture_coords[1] = dist1 / dist2;
                lpObject->vertices[lpfacet->vertices[2]].texture_coords[2] = 1.0f;
            } // if

            lpObject->vertices[lpfacet->vertices[0]].texture_coords[0] = 1.0f;
            lpObject->vertices[lpfacet->vertices[1]].texture_coords[1] = 1.0f;
            lpObject->vertices[lpfacet->vertices[2]].texture_coords[2] = 1.0f;

        } // for
    } // if

    return TRUE;
} // TexSetup


/******************************************************************************
 * PointMultMatrix                                                            *
 *  Function: multiplies a vector by a matrix                                 *
 *    Inputs: pt - vector to be multiplied                                    *
 *            mat - matrix to multiply to vector                              *
 *            result - vector to store result of multiplication               *
 *   Outputs: same as result above                                            *
 ******************************************************************************/

float *PointMultMatrix (VECT pt, MATRIX mat, VECT result)
{
    int i, j;
    VECT r;

    for (i = 0; i < 4; i++)
    {
        r[i] = 0.0f;
        for (j = 0; j < 4; j++)
        {
            r[i] += mat[i][j] * pt[j];
        } // for
    } // for

    memcpy (result, r, sizeof (VECT));

    return ((float *) result);
} // PointMultMatrix


/******************************************************************************
 * CalcViewBoundingBox                                                        *
 *  Function: computes the bounding box for the view                          *
 *    Inputs: lpObject - pointer to object descriptor                         *
 *   Outputs: TRUE - bounding box computed                                    *
 *            FALSE - unable to determine bounding box                        *
 ******************************************************************************/

BOOL CalcViewBoundingBox (POBJECT lpObject)
{
    long    i, j, vid;
    float   minx, miny, minz;
    float   maxx, maxy, maxz;
    PFACET  fptr;
    PVERTEX v;

    if (!lpObject) return FALSE;

    // Initialize the view bounding box.

    SetVector (gView.bounds.world_pts[0],  FLT_MAX,  FLT_MAX, -FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[1], -FLT_MAX,  FLT_MAX, -FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[2], -FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[3],  FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);

    SetVector (gView.bounds.world_pts[4],  FLT_MAX,  FLT_MAX,  FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[5], -FLT_MAX,  FLT_MAX,  FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[6], -FLT_MAX, -FLT_MAX,  FLT_MAX, 1.0f);
    SetVector (gView.bounds.world_pts[7],  FLT_MAX, -FLT_MAX,  FLT_MAX, 1.0f);

    fptr = &lpObject->facets[0];

    // Get current min, max values.

    minx = gView.bounds.world_pts[0][0];
    miny = gView.bounds.world_pts[0][1];
    minz = gView.bounds.world_pts[4][2];

    maxx = gView.bounds.world_pts[2][0];
    maxy = gView.bounds.world_pts[2][1];
    maxz = gView.bounds.world_pts[0][2];

    // Update min, max to include current list of vertices.

    for (i = 0; i < (long) lpObject->num_facets; i++, fptr++)
    {
        for (j = 0; j < fptr->num_verts; j++)
        {
            vid = fptr->vertices[j];
            v = &lpObject->vertices[vid];

            minx = min (minx, v->world_coords[0]);
            miny = min (miny, v->world_coords[1]);
            minz = min (minz, v->world_coords[2]);

            maxx = max (maxx, v->world_coords[0]);
            maxy = max (maxy, v->world_coords[1]);
            maxz = max (maxz, v->world_coords[2]);
        } // for
    } // for

    // Save min, max values in 8 vertices of bounding box.

    SetVector (gView.bounds.world_pts[0], minx, miny, maxz, 1.0f);
    SetVector (gView.bounds.world_pts[1], maxx, miny, maxz, 1.0f);
    SetVector (gView.bounds.world_pts[2], maxx, maxy, maxz, 1.0f);
    SetVector (gView.bounds.world_pts[3], minx, maxy, maxz, 1.0f);

    SetVector (gView.bounds.world_pts[4], minx, miny, minz, 1.0f);
    SetVector (gView.bounds.world_pts[5], maxx, miny, minz, 1.0f);
    SetVector (gView.bounds.world_pts[6], maxx, maxy, minz, 1.0f);
    SetVector (gView.bounds.world_pts[7], minx, maxy, minz, 1.0f);

    return TRUE;
} // CalcViewBoundingBox


/******************************************************************************
 * QSortFacetArray                                                            *
 *  Function: performs a recursive quicksort of object array                  *
 *    Inputs: lpObject - pointer to object array                              *
 *            left - first element in subarray to sort                        *
 *            right - last element in subarray to sort                        *
 *   Outputs: none                                                            *
 ******************************************************************************/

void QSortFacetArray (POBJECT lpObject, int left, int right)
{
    int i, last;
    PFACET pfacet1;
    PFACET pfacet2;
    PFACET temp;
    int r;

    if (left >= right) return;

    r = (left + right) >> 1;
    temp = lpObject->facetlist[left];
    lpObject->facetlist[left] = lpObject->facetlist[r];
    lpObject->facetlist[r] = temp;

    last = left;
    for (i = left + 1; i <= right; i++)
    {
        pfacet1 = lpObject->facetlist[i];
        pfacet2 = lpObject->facetlist[left];
        if (CompareFacets (lpObject, pfacet1, pfacet2) == 1)
        {
            ++last;
            temp = lpObject->facetlist[last];
            lpObject->facetlist[last] = lpObject->facetlist[i];
            lpObject->facetlist[i] = temp;
        } // if
    } // for

    temp = lpObject->facetlist[left];
    lpObject->facetlist[left] = lpObject->facetlist[last];
    lpObject->facetlist[last] = temp;

    QSortFacetArray (lpObject, left, last-1);
    QSortFacetArray (lpObject, last+1, right);
} // QSortFacetArray


/******************************************************************************
 * CompareFacets                                                              *
 *  Function: compares the relative ordering of two facets                    *
 *    Inputs: lpObject - pointer to object array                              *
 *            pfacet1 - first facet of object                                 *
 *            pfacet2 - second facet of object                                *
 *   Outputs: 1 - facet 1 is closer than facet 2                              *
 *            0 - facet 2 is closer than facet 1                              *
 ******************************************************************************/

int CompareFacets (POBJECT lpObject, const PFACET pfacet1, const PFACET pfacet2)
{
    float z1 = 0.0f;
    float z2 = 0.0f;
    int index0, index1, index2;
    C3D_UINT16 *pvindex;
    float zv0, zv1, zv2;
    PVERTEX vp;

    // Get pointer to vertices.

    vp = lpObject->vertices;

    // Get pointer to facet vertex 1 list.

    pvindex = pfacet1->vertices;

    // Read facet vertex indices.

    index0 = *pvindex++;
    index1 = *pvindex++;
    index2 = *pvindex;

    // Read z values and sum.

    zv0 = vp[index0].world_coords[2];
    zv1 = vp[index1].world_coords[2];
    zv2 = vp[index2].world_coords[2];
    z1 = zv0 + zv1 + zv2;

    // Get pointer to facet 2 vertex list.

    pvindex = pfacet2->vertices;
    index0 = *pvindex++;
    index1 = *pvindex++;
    index2 = *pvindex;

    // Read z values and sum.

    zv0 = vp[index0].world_coords[2];
    zv1 = vp[index1].world_coords[2];
    zv2 = vp[index2].world_coords[2];
    z2 = zv0 + zv1 + zv2;

    // Compare z values.

    if (z2 < z1) return 0;

    return 1;
} // CompareFacets
