/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	Title screen functionality. Includes initialization, surface creation,     *
 *	image loading, blitting and cleanup.									   *
 *******************************************************************************/
#ifndef ATI_TITLE_H
#define ATI_TITLE_H
// -----------------------------------------------------------------------------

#include "AtiDemo.h"

// -----------------------------------------------------------------------------

class AtiTitle {
public:
	static void Cleanup(void) ;
	static void Initialize(void) ;

	static void Paint(IDirectDrawSurface* pSurface, IDirectDrawSurface* pZBuffer) ;

private:
};

// -----------------------------------------------------------------------------
#endif
