/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *******************************************************************************/

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, 
// but are changed infrequently

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRALEAN
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>

#pragma warning(disable:4244) // conversion from 'T' to 'T2', possible loss of data

#define ALLOW_LOCKSTEP		// #define to allow interleaving physics/graphics based on win.ini var

#define KEYBOARD_FLIGHT
#ifdef KEYBOARD_FLIGHT
#endif
