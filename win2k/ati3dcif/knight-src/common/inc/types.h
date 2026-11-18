/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *																			   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Standard and useful type declarations.									   *
 *******************************************************************************/
#ifndef ATI_TYPES_H
#define ATI_TYPES_H
// -----------------------------------------------------------------------------

#include <crtdbg.h>
#include <stdarg.h>
#include <stdio.h>

// -----------------------------------------------------------------------------

typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed long int32;
typedef unsigned long uint32;
typedef float real32;
typedef double real64;

#define nonvirtual

// -----------------------------------------------------------------------------

#define ASSERT(x)	_ASSERTE(x)
#define TRACE		AfxTrace

#ifdef _DEBUG
#define VERIFY(x) _ASSERTE(x)
#else
#define VERIFY(x) (x)
#endif

inline void AfxTrace(char* fmt, ...)
{
#ifdef _DEBUG
	char string[256];
	va_list alist;
	va_start(alist, fmt);
	vsprintf(string, fmt, alist);
	_RPT0(_CRT_WARN, string);
#else
	(void)fmt;
#endif
}

// -----------------------------------------------------------------------------
#endif
