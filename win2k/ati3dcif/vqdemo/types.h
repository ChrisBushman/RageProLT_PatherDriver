/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * types.h - Header file defining various data structures.                    *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#ifndef TYPES_H
#define TYPES_H

#include <windows.h>
#include "ati3dcif.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FACET_PER_VERTEX 50
#define MAX_VERTEX_PER_FACET 4
#define MAX_LIGHTS           8

#define BACKFACE             1
#define ACCEPT               2
#define REJECT               4
#define CLIPPED              8
#define EXCLUDE              16

typedef struct {                        
    C3D_FLOAT32 x, y, z;                
} C3D_VF, * C3D_PVF;                    

typedef struct {
    C3D_VF  ambient;
    C3D_VF  diffuse;
    C3D_VF  specular;
    C3D_VF  world_coords;
} LIGHT, *PLIGHT;

typedef struct {
    LIGHT light;
    BOOL  enable;
} LIGHT_INFO, *PLIGHT_INFO;

typedef struct {
    C3D_VF  ambient;
    C3D_VF  diffuse;
    C3D_VF  specular;
    float   shinyness;
    float   opacity;
} MATERIAL, *PMATERIAL;

typedef float VECT[4];
typedef float MATRIX[4][4];

typedef enum {
    PROJ_PERSPECTIVE = 0,   // perspective projection
    PROJ_PARALLEL    = 1,   // parallel projection
    PROJ_MAX         = 2    // max enumeration; invalid number
} PROJ;

typedef struct {
    float  left;
    float  top;
    float  front;
    float  right;
    float  bottom;
    float  back;
    float  dist;
} FRUSTUM, *PFRUSTUM;

typedef struct {
    C3D_VF  location;
    C3D_VF  upvector;
    C3D_VF  direction;
    C3D_VF  lookat;
} CAMERA, *PCAMERA;

typedef struct {
    float  left;
    float  top;
    float  right;
    float  bottom;
} VIEWPORT, *PVIEWPORT;

typedef struct {
    VECT world_pts[8];
} BOUNDS;

typedef struct {
    PROJ        projmode;
    CAMERA      camera;
    FRUSTUM     frustum;
    VIEWPORT    viewport;
    LIGHT_INFO  lights[MAX_LIGHTS];
    int         num_lights;
    MATRIX      R_axis_align;       // camera coord alignment row vectors
    MATRIX      invR;               // inverse of R
    MATRIX      N;                  // view orientation matrix
    MATRIX      VP;
    MATRIX      VPN;
    float       zmin;
    BOUNDS      bounds;
} VIEW, *PVIEW;

typedef struct {
    VECT        model_coords;
    VECT        world_coords;
    VECT        texture_coords;
    VECT        norm_volume_coords;
    VECT        normal;
    C3D_VTCF    render_data;
    int         num_facets;
    C3D_UINT16  facets[MAX_FACET_PER_VERTEX];
    C3D_UINT32  field_mask;
} VERTEX, *PVERTEX;

typedef struct {
    int         num_verts;
    C3D_UINT16  vertices[MAX_VERTEX_PER_FACET];
    VECT        normal;
    C3D_UINT32  flags;
} FACET, *PFACET;

typedef struct {
    float xmin;
    float xmax;
    float ymin;
    float ymax;
    float zmin;
    float zmax;
    float amin;
    float amax;
} VERTBOUNDS, *PVERTBOUNDS;

typedef struct {
    BOOL        flip_normals;
    MATERIAL    material;
    C3D_UINT32  num_verts;
    C3D_UINT32  num_facets;
    C3D_UINT32  max_verts;
    C3D_UINT32  max_facets;
    VERTEX      *vertices;
    FACET       *facets;
    PFACET      *facetlist;
    C3D_UINT16  num_facetlist;
    C3D_VLIST   vlstPrimList;
    int         nNumPrimListVerts;
    MATRIX      mat;                // New.  Added for .ASC file.
    VERTBOUNDS  bounds;
} OBJECT, *POBJECT;


#ifdef __cplusplus
}
#endif


#endif
