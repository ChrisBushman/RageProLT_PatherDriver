/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *  File handling utilities for joint motion data. 							   *
 *******************************************************************************/
#ifndef AJM_H
#define AJM_H
// -----------------------------------------------------------------------------

#include "Polygon.h"

// -----------------------------------------------------------------------------

BOOL IsAJM(char* filename);
BOOL IsAJM(fileHandle& fh);

// -----------------------------------------------------------------------------

class JointMotion {
public:
	~JointMotion(void);
	
	static JointMotion* Read(const char* filename, Object* pObject) ;
	static JointMotion* Read(fileHandle& fh, Object* pObject) ;

	uint32 TickCount(void) const { return m_nTicks; }
	uint32 GroupCount(void) const { return m_nGroups; }

	const Vector& Translation(int tick) const  { ASSERT(tick < m_nTicks); return m_pTranslation[tick]; }
	Vector& Translation(int tick)  { ASSERT(tick < m_nTicks); return m_pTranslation[tick]; }

	const Vector& Rotation(int group, int tick) const  { ASSERT(group < m_nGroups); ASSERT(tick < m_nTicks); return m_ppRotation[group][tick]; }
	Vector& Rotation(int group, int tick)  { ASSERT(group < m_nGroups); ASSERT(tick < m_nTicks); return m_ppRotation[group][tick]; }

	::Group*	Group(int group) const  { ASSERT(group < m_nGroups); return m_ppGroups[group]; }
	::Group*&	Group(int group)  { ASSERT(group < m_nGroups); return m_ppGroups[group]; }

	void SetTick(int tick);

protected:
private:
	int			m_nGroups;
	int			m_nTicks;
	
	Object*		m_pObject;
	Vector*		m_pTranslation;
	Vector**	m_ppRotation;
	::Group**	m_ppGroups;

	JointMotion(uint32 groupCount, uint32 tickCount) /* ; */ ;
};

// -----------------------------------------------------------------------------
#endif
