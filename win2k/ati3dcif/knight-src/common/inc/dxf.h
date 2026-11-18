/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 * 	AutoCAD (DXF) file handling functionality. 								   *
 *******************************************************************************/
#ifndef DXF_H
#define DXF_H
// -----------------------------------------------------------------------------

#include <stdio.h>
#include "polygon.h"

// -----------------------------------------------------------------------------

BOOL DXFRead(Object** ppObject, FILE *pFile);
BOOL DXFRead(Object** ppObject, char *filename);

// -----------------------------------------------------------------------------
#endif
