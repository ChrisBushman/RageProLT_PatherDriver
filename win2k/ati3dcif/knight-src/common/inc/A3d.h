/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *  File handling utilities for reading and writing polygon data. 			   *
 *******************************************************************************/
#ifndef A3D_H
#define A3D_H
// -----------------------------------------------------------------------------

#include "Polygon.h"

// -----------------------------------------------------------------------------

BOOL IsA3D(char* filename);
BOOL IsA3D(fileHandle& fh);
void A3DWrite(fileHandle& fh, const Object* pObject) ;
void A3DWrite(char* filename, const Object* pObject) ;
void A3DRead(Object** ppObject, char* filename) ;	// match def; gcc enforces spec. CWB
void A3DRead(Object** ppObject, fileHandle& fh) ;

// -----------------------------------------------------------------------------
#endif
