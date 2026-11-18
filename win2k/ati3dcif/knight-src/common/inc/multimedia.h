/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 * 	Joystick and sound functionality. 										   *
 *******************************************************************************/
#ifndef JOYSTICK_H
#define JOYSTICK_H
// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#include "mmsystem.h"

#include "util.h"
#include "matrix.h"

// -----------------------------------------------------------------------------

typedef ExceptionTemplate<MMRESULT> MM_Exception;

// -----------------------------------------------------------------------------

#define MAX_JOYSTICKS 4

// -----------------------------------------------------------------------------

struct MoveData {
	MoveData(void) : oldTranslation(ZERO), oldRotation(ZERO), oldButtons(0) { }

	Vector	oldTranslation;
	Vector	oldRotation;
	DWORD	oldButtons;
};

struct FlightData : MoveData {
	FlightData(void) : MoveData() { }
};

// -----------------------------------------------------------------------------

class Multimedia {
public:
	static void End(void) ;
	static void Begin(void) ;

	static void PollJoysticks(void) ;

	static Vector JoyTranslation(int index) { ASSERT((index>=0)&&(index<MAX_JOYSTICKS)); return m_joystick[index].translation; }
	static Vector JoyRotation(int index) { ASSERT((index>=0)&&(index<MAX_JOYSTICKS)); return m_joystick[index].rotation; }
	static DWORD  JoyButtons(int index) { ASSERT((index>=0)&&(index<MAX_JOYSTICKS)); return m_joystick[index].buttons; }
	static BOOL   JoyHatCentered(int index) { ASSERT((index>=0)&&(index<MAX_JOYSTICKS)); return m_joystick[index].hatCentered; }
	static float  JoyHat(int index) { ASSERT((index>=0)&&(index<MAX_JOYSTICKS)); return m_joystick[index].hat; }

	static float ProcessDead(float value, float deadzone);
	static BOOL  FlightControl(FlightData& flightData, Matrix* pRotation, Vector* pTranslation, float scale);

	static const char *ErrString(MMRESULT result);

	static void PlayTrack(int track) ;

protected:
private:
	static int		m_nJoysticks;
	static struct JoyInfo {
		JoyInfo() : translation(UNINITIALIZED), rotation(UNINITIALIZED) { }
		Vector	translation;
		Vector	rotation;
		float	hat;
		BOOL	hatCentered;
		DWORD	buttons;
	}				m_joystick[MAX_JOYSTICKS];
	static JOYCAPS	m_joyCaps[MAX_JOYSTICKS];
	static BOOL		m_joyExists[MAX_JOYSTICKS];

	static MCIDEVICEID	m_cdDevID;

	~Multimedia(void);
	Multimedia(void);
	Multimedia(const Multimedia&);
	Multimedia& operator=(const Multimedia&);
};

// -----------------------------------------------------------------------------

#define THROW_MM_EXCEPTION(r) throw MM_Exception("Multimedia", __FILE__, __LINE__, Multimedia::ErrString(r), r)

// -----------------------------------------------------------------------------
#endif
