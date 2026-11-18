/******************************************************************************
 * ATI 3D RAGE SDK sample code                                                *
 *                                                                            *
 * parseasc.h - Header file for PARSEASC.CPP.                                 *
 *                                                                            *
 * Copyright (c) 1995-1996 ATI Technologies Inc.  All rights reserved.        *
 ******************************************************************************/

#ifndef PARSEASC_H
#define PARSEASC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

int GetAscTokenCode (char *token);
BOOL LoadAscObject (POBJECT lpObject, char *filename);
BOOL ReadVertex (POBJECT lpObject);
BOOL ReadFacet (POBJECT lpObject);
BOOL OpenAscFile (char *filename);
void CloseAscFile (void);
int SkipSpace (void);
int GetAscLine (char *str);
int GetAscToken (char *str, char *delim);

#ifdef __cplusplus
}
#endif

#endif
