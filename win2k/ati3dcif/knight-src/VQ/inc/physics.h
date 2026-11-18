/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *																			   *
 *	Physics timing control. 												   *
 *******************************************************************************/
#ifndef PHYSICS_H
#define PHYSICS_H
// -----------------------------------------------------------------------------

#include "matrix.h"

// -----------------------------------------------------------------------------

enum PhysicsType {
	PHYSICS_RUN,
	PHYSICS_COPY
};



class Physics {
public:
	Physics(void);

	void Copy(void);
	void Tick(void);

	BOOL ready;

	void SetPhysicsMode(void (*pMode)(PhysicsType type));

	static void PhysicsModeNoop(PhysicsType type);

private:
	void	(*m_pMode)(PhysicsType type);

	int		tickNumber;

	Physics(const Physics&);
	Physics& operator=(const Physics&);
};



extern Physics g_physics;

#ifdef KEYBOARD_FLIGHT
extern BOOL g_keyboardFlight;
#endif

// -----------------------------------------------------------------------------

#define TIME(secs, ticks) ((secs) * 30 + (ticks))

// -----------------------------------------------------------------------------
#endif
