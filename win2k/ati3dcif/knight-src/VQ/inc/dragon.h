/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *******************************************************************************/
#ifndef DRAGON_H
#define DRAGON_H
// -----------------------------------------------------------------------------

#include "physics.h"
#include "normalmode.h"

// -----------------------------------------------------------------------------

class Dragon {
public:
	static void Cleanup(void) ;
	static void Initialize(void) ;

	static void Physics(PhysicsType type) throw();

	static void SetMode(void);

	static void DrawShadow(Clipper& rClipper);
	static void Draw(Clipper& rClipper) ;

private:
};

// -----------------------------------------------------------------------------
#endif
