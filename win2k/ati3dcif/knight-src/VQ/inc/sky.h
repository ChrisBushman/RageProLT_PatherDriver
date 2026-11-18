/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	The base sequence and set up for rendering of the sky polygon.			   *
 *******************************************************************************/
#ifndef SKY_H
#define SKY_H
// -----------------------------------------------------------------------------

class Sky {
public:
	static void Cleanup(void) ;
	static void Initialize(void) ;

	static void Draw(Clipper& rClipper) ;

protected:
private:
};

// -----------------------------------------------------------------------------
#endif
