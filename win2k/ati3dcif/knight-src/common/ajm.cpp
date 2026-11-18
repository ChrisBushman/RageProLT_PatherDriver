/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  File handling utilities for joint motion data. 							   *
 *******************************************************************************/
#include "stdwin.h"
#include <stdarg.h>
#include <ctype.h>
#include "ajm.h"
#include "util.h"
#include "Matrix.h"
#include "Polygon.h"
#include "Watchers.h"

// -----------------------------------------------------------------------------

#define AJM_TAG_0	'AJM0'

static char GROUP_GLOBAL_TRANSLATION_NAME[64] = "GLOBAL TRANSLATION (Not a group)";

// -----------------------------------------------------------------------------

BOOL IsAJM(char* filename)
{
	ASSERT(filename);
	try
	{
		fileHandle fh(filename, "rb");
		return IsAJM(fh);
	}
	catch(...)
	{
		return FALSE;
	}
}

BOOL IsAJM(fileHandle& fh)
{
	try
	{
		uint32 code = fh.readUint32();
		if((code != AJM_TAG_0))
		   throw 0;
		return TRUE;
	}
	catch(...)
	{
		return FALSE;
	}
}

// -----------------------------------------------------------------------------

JointMotion* JointMotion::Read(const char* filename, Object* pObject) 
{
	ASSERT(filename);
	ASSERT(pObject);

	fileHandle fh(filename, "rb");
	return Read(fh, pObject);
}

// -----------------------------------------------------------------------------

JointMotion* JointMotion::Read(fileHandle& fh, Object* pObject) 
{
	BOOL success = FALSE;

	uint32 tag = fh.readUint32();
	if(tag != AJM_TAG_0) THROW_EXCEPTION();

	uint32 tickCount = fh.readUint32()+1;
	uint32 groupCount = fh.readUint32();

	uint32 groupStart = fh.tell();

	uint32 count = 0;
	for(uint32 group = 0; group < groupCount; group++)
	{
		char name[65];
		fh.read(name, 64);
		name[65] = 0;

		if(!strcmp(GROUP_GLOBAL_TRANSLATION_NAME, name))
			;
		else
		{
			::Group* pGroup = pObject->FindGroup(name);
			if(pGroup != NULL)
				count++;
		}

		uint32 jointCount = fh.readUint32();

		fh.seek(4*4*jointCount, SEEK_CUR);
	}

	JointMotion* pMotion = new JointMotion(count, tickCount);
	DECLARE_POINTER_WATCHER(JointMotion*, pMotion, success, pMotion);

	pMotion->m_pObject = pObject;

	fh.seek(groupStart, SEEK_SET);

	count = 0;
	for(group = 0; group < groupCount; group++)
	{
		char name[65];
		fh.read(name, 64);
		name[65] = 0;

		uint32 jointCount = fh.readUint32();

		if(!strcmp(GROUP_GLOBAL_TRANSLATION_NAME, name))
		{
			uint32 nextTick = 0;
			Vector lastValue = Vector(0, 0, 0);

			while(jointCount--)
			{
				uint32 tick = fh.readUint32();
				real32 x = fh.readReal32();
				real32 y = fh.readReal32();
				real32 z = fh.readReal32();
				if(tick > tickCount) THROW_EXCEPTION();

				for(; nextTick<tick; nextTick++)
					pMotion->Translation(nextTick) = lastValue;

				lastValue = Vector(x, y, z);
				pMotion->Translation(nextTick) = lastValue;
				nextTick++;
			}

			for(; nextTick<tickCount; nextTick++)
				pMotion->Translation(nextTick) = lastValue;
		}
		else
		{
			::Group* pGroup = pObject->FindGroup(name);
			if(pGroup)
			{
				uint32 nextTick = 0;
				Vector lastValue = Vector(0, 0, 0);

				pMotion->Group(count) = pGroup;
				while(jointCount--)
				{
					uint32 tick = fh.readUint32();
					real32 x = fh.readReal32();
					real32 y = fh.readReal32();
					real32 z = fh.readReal32();
					if(tick > tickCount) THROW_EXCEPTION();

					for(; nextTick<tick; nextTick++)
						pMotion->Rotation(count, nextTick) = lastValue;

					lastValue = Vector(x, y, z);
					pMotion->Rotation(count, nextTick) = lastValue;
					nextTick++;
				}

				for(; nextTick<tickCount; nextTick++)
					pMotion->Rotation(count, nextTick) = lastValue;
				
				count++;
			}
			else
				fh.seek(4*4*jointCount, SEEK_CUR);
		}
	}

	success = TRUE;
	return pMotion;
}

// -----------------------------------------------------------------------------

JointMotion::~JointMotion(void)
{
	if(m_pTranslation)
		delete [] m_pTranslation;
	m_pTranslation = NULL;

	if(m_ppRotation)
	{
		for(int i=0; i<m_nGroups; i++)
		{
			if(m_ppRotation[i])
				delete [] m_ppRotation[i];
			m_ppRotation[i] = NULL;
		}

		delete [] m_ppRotation;
	}
	m_ppRotation = NULL;

	if(m_ppGroups)
		delete [] m_ppGroups;
	m_ppGroups = NULL;
}

class ZeroVector : public Vector { public: ZeroVector() : Vector(ZERO) { } };
class IdentityMatrix : public Matrix { public: IdentityMatrix() : Matrix(IDENTITY) { } };

JointMotion::JointMotion(uint32 groupCount, uint32 tickCount) /* ; */
{
	BOOL success = FALSE;

	m_nGroups = groupCount;
	m_nTicks = tickCount;

	m_pTranslation = new ZeroVector[m_nTicks];
	m_ppRotation = new Vector*[m_nGroups];
	for(int i=0; i<m_nGroups; i++)
		m_ppRotation[i] = new ZeroVector[m_nTicks];
	m_ppGroups = new ::Group*[m_nGroups];

	for(i=0; i<m_nGroups; i++)
		m_ppGroups[i] = NULL;

	success = TRUE;
}

// -----------------------------------------------------------------------------

void JointMotion::SetTick(int tick)
{
	ASSERT((tick>=0) && (tick<m_nTicks));

	m_pObject->Translation() = Translation(tick);

	for(int group=0; group<m_nGroups; group++)
		m_ppGroups[group]->Rotation() = m_ppRotation[group][tick];
}

// -----------------------------------------------------------------------------
