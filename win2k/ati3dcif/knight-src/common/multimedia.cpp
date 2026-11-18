/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Joystick and sound functionality. 										   *
 *******************************************************************************/
#include "stdwin.h"
#include <math.h>
#include "mmsystem.h"

#include "multimedia.h"

// -----------------------------------------------------------------------------

#define DEAD_TX	0.10
#define DEAD_TY	0.10
#define DEAD_TZ	0.10
#define DEAD_RX	0.10
#define DEAD_RY	0.10
#define DEAD_RZ	0.10

// -----------------------------------------------------------------------------

Multimedia::JoyInfo	Multimedia::m_joystick[MAX_JOYSTICKS];
JOYCAPS				Multimedia::m_joyCaps[MAX_JOYSTICKS];
int					Multimedia::m_nJoysticks = 0;
BOOL				Multimedia::m_joyExists[MAX_JOYSTICKS];

MCIDEVICEID	Multimedia::m_cdDevID;

// -----------------------------------------------------------------------------

void Multimedia::End(void) 
{
	if(m_cdDevID != (UINT)-1)
		mciSendCommand(m_cdDevID, MCI_CLOSE, 0, NULL);
	m_cdDevID = (UINT)-1;
}



void Multimedia::Begin(void) 
{
	m_nJoysticks = joyGetNumDevs();
	if(m_nJoysticks > MAX_JOYSTICKS)
		m_nJoysticks = MAX_JOYSTICKS;
	
	for(int i=0; i<m_nJoysticks; i++)
	{
		MMRESULT result = joyGetDevCaps(i, &m_joyCaps[i], sizeof(m_joyCaps[i]));
		if(result != JOYERR_NOERROR)
			m_joyExists[i] = FALSE;
		else
			m_joyExists[i] = TRUE;
	}
}

// -----------------------------------------------------------------------------

void Multimedia::PollJoysticks(void) 
{
	m_joystick[0].translation = Vector(0, 0, 0);
	m_joystick[0].rotation = Vector(0, 0, 0);
	m_joystick[0].hatCentered = TRUE;
	m_joystick[0].hat = 0;
	m_joystick[0].buttons = 0;

	for(int index=0; index<m_nJoysticks; index++)
	{
		m_joystick[index].translation = Vector(0, 0, 0);
		m_joystick[index].rotation = Vector(0, 0, 0);
		m_joystick[index].hatCentered = TRUE;
		m_joystick[index].hat = 0;
		m_joystick[index].buttons = 0;

		if(m_joyExists[index])
		{
			JOYINFOEX ji;
			memset(&ji, 0, sizeof(ji));
			ji.dwSize = sizeof(ji);
			ji.dwFlags = JOY_RETURNBUTTONS | JOY_RETURNX | JOY_RETURNY | JOY_USEDEADZONE;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASZ)	ji.dwFlags |= JOY_RETURNZ;
			if(m_joyCaps[index].wCaps & JOYCAPS_HASR)	ji.dwFlags |= JOY_RETURNR;
			if(m_joyCaps[index].wCaps & JOYCAPS_HASU)	ji.dwFlags |= JOY_RETURNU;
			if(m_joyCaps[index].wCaps & JOYCAPS_HASV)	ji.dwFlags |= JOY_RETURNV;
			if(m_joyCaps[index].wCaps & JOYCAPS_HASPOV)	ji.dwFlags |= JOY_RETURNPOV;

			MMRESULT result = joyGetPosEx(index, &ji);
			if(result != MMSYSERR_NOERROR) continue;

			m_joystick[index].translation.X() = (((float)ji.dwXpos - (float)m_joyCaps[index].wXmin) / (float)(m_joyCaps[index].wXmax - m_joyCaps[index].wXmin)) * 2.0 - 1.0;
			m_joystick[index].translation.Y() = (((float)ji.dwYpos - (float)m_joyCaps[index].wYmin) / (float)(m_joyCaps[index].wYmax - m_joyCaps[index].wYmin)) * 2.0 - 1.0;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASZ)
				m_joystick[index].translation.Z() = (((float)ji.dwZpos - (float)m_joyCaps[index].wZmin) / (float)(m_joyCaps[index].wZmax - m_joyCaps[index].wZmin)) * 2.0 - 1.0;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASR)
				m_joystick[index].rotation.X() = (((float)ji.dwRpos - (float)m_joyCaps[index].wRmin) / (float)(m_joyCaps[index].wRmax - m_joyCaps[index].wRmin)) * 2.0 - 1.0;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASU)
				m_joystick[index].rotation.Y() = (((float)ji.dwUpos - (float)m_joyCaps[index].wUmin) / (float)(m_joyCaps[index].wUmax - m_joyCaps[index].wUmin)) * 2.0 - 1.0;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASV)
				m_joystick[index].rotation.Z() = (((float)ji.dwVpos - (float)m_joyCaps[index].wVmin) / (float)(m_joyCaps[index].wVmax - m_joyCaps[index].wVmin)) * 2.0 - 1.0;

			if(m_joyCaps[index].wCaps & JOYCAPS_HASPOV)
				if(ji.dwPOV < 36000)
				{
					m_joystick[index].hatCentered = FALSE;
					m_joystick[index].hat = (float)ji.dwPOV * 3.1415927 / 18000;
				}

			m_joystick[index].buttons = ji.dwButtons;
		}
	}

	if(GetAsyncKeyState(VK_NUMPAD4) & 0x8000)  { m_joystick[0].translation.X() = -1.0; }
	if(GetAsyncKeyState(VK_NUMPAD6) & 0x8000)  { m_joystick[0].translation.X() =  1.0; }
	if(GetAsyncKeyState(VK_NUMPAD8) & 0x8000)  { m_joystick[0].translation.Y() = -1.0; }
	if(GetAsyncKeyState(VK_NUMPAD2) & 0x8000)  { m_joystick[0].translation.Y() =  1.0; }
	if(GetAsyncKeyState(VK_NUMPAD0) & 0x8000)  { m_joystick[0].rotation.X() = -1.0; }
	if(GetAsyncKeyState(VK_DECIMAL) & 0x8000)  { m_joystick[0].rotation.X() =  1.0; }
	if(GetAsyncKeyState(VK_NUMPAD9) & 0x8000)  { m_joystick[0].buttons |= 1; }
	if(GetAsyncKeyState(VK_NUMPAD3) & 0x8000)  { m_joystick[0].buttons |= 2; }
	if(GetAsyncKeyState(VK_SUBTRACT) & 0x8000) { m_joystick[0].buttons |= 4; }
	if(GetAsyncKeyState(VK_ADD) & 0x8000)      { m_joystick[0].buttons |= 8; }
	if(GetAsyncKeyState(VK_UP) & 0x8000)	{ m_joystick[0].hatCentered = FALSE; m_joystick[0].hat = 0; }
	if(GetAsyncKeyState(VK_RIGHT) & 0x8000)	{ m_joystick[0].hatCentered = FALSE; m_joystick[0].hat = 1.570796326795; }
	if(GetAsyncKeyState(VK_DOWN) & 0x8000)	{ m_joystick[0].hatCentered = FALSE; m_joystick[0].hat = 3.14159265359; }
	if(GetAsyncKeyState(VK_LEFT) & 0x8000)	{ m_joystick[0].hatCentered = FALSE; m_joystick[0].hat = 4.712388980385; }
}

// -----------------------------------------------------------------------------

const char *Multimedia::ErrString(MMRESULT err)
{
	switch(err)
	{
#define REGISTER_ERROR(e) case e : return #e
	REGISTER_ERROR(MMSYSERR_NOERROR);
	REGISTER_ERROR(MMSYSERR_ERROR);
	REGISTER_ERROR(MMSYSERR_BADDEVICEID);
	REGISTER_ERROR(MMSYSERR_NOTENABLED);
	REGISTER_ERROR(MMSYSERR_ALLOCATED);
	REGISTER_ERROR(MMSYSERR_INVALHANDLE);
	REGISTER_ERROR(MMSYSERR_NODRIVER);
	REGISTER_ERROR(MMSYSERR_NOMEM);
	REGISTER_ERROR(MMSYSERR_NOTSUPPORTED);
	REGISTER_ERROR(MMSYSERR_BADERRNUM);
	REGISTER_ERROR(MMSYSERR_INVALFLAG);
	REGISTER_ERROR(MMSYSERR_INVALPARAM);
	REGISTER_ERROR(MMSYSERR_HANDLEBUSY);
	REGISTER_ERROR(MMSYSERR_INVALIDALIAS);
	REGISTER_ERROR(MMSYSERR_BADDB);
	REGISTER_ERROR(MMSYSERR_KEYNOTFOUND);
	REGISTER_ERROR(MMSYSERR_READERROR);
	REGISTER_ERROR(MMSYSERR_WRITEERROR);
	REGISTER_ERROR(MMSYSERR_DELETEERROR);
	REGISTER_ERROR(MMSYSERR_VALNOTFOUND);
	REGISTER_ERROR(MMSYSERR_NODRIVERCB);
	REGISTER_ERROR(WAVERR_BADFORMAT);
	REGISTER_ERROR(WAVERR_STILLPLAYING);
	REGISTER_ERROR(WAVERR_UNPREPARED);
	REGISTER_ERROR(WAVERR_SYNC);
	REGISTER_ERROR(MIDIERR_UNPREPARED);
	REGISTER_ERROR(MIDIERR_STILLPLAYING);
	REGISTER_ERROR(MIDIERR_NOMAP);
	REGISTER_ERROR(MIDIERR_NOTREADY);
	REGISTER_ERROR(MIDIERR_NODEVICE);
	REGISTER_ERROR(MIDIERR_INVALIDSETUP);
	REGISTER_ERROR(MIDIERR_BADOPENMODE);
	REGISTER_ERROR(MIDIERR_DONT_CONTINUE);
	REGISTER_ERROR(TIMERR_NOCANDO);
	REGISTER_ERROR(TIMERR_STRUCT);
	REGISTER_ERROR(JOYERR_PARMS);
	REGISTER_ERROR(JOYERR_NOCANDO);
	REGISTER_ERROR(JOYERR_UNPLUGGED);
	REGISTER_ERROR(MCIERR_INVALID_DEVICE_ID);
	REGISTER_ERROR(MCIERR_UNRECOGNIZED_KEYWORD);
	REGISTER_ERROR(MCIERR_UNRECOGNIZED_COMMAND);
	REGISTER_ERROR(MCIERR_HARDWARE);
	REGISTER_ERROR(MCIERR_INVALID_DEVICE_NAME);
	REGISTER_ERROR(MCIERR_OUT_OF_MEMORY);
	REGISTER_ERROR(MCIERR_DEVICE_OPEN);
	REGISTER_ERROR(MCIERR_CANNOT_LOAD_DRIVER);
	REGISTER_ERROR(MCIERR_MISSING_COMMAND_STRING);
	REGISTER_ERROR(MCIERR_PARAM_OVERFLOW);
	REGISTER_ERROR(MCIERR_MISSING_STRING_ARGUMENT);
	REGISTER_ERROR(MCIERR_BAD_INTEGER);
	REGISTER_ERROR(MCIERR_PARSER_INTERNAL);
	REGISTER_ERROR(MCIERR_DRIVER_INTERNAL);
	REGISTER_ERROR(MCIERR_MISSING_PARAMETER);
	REGISTER_ERROR(MCIERR_UNSUPPORTED_FUNCTION);
	REGISTER_ERROR(MCIERR_FILE_NOT_FOUND);
	REGISTER_ERROR(MCIERR_DEVICE_NOT_READY);
	REGISTER_ERROR(MCIERR_INTERNAL);
	REGISTER_ERROR(MCIERR_DRIVER);
	REGISTER_ERROR(MCIERR_CANNOT_USE_ALL);
	REGISTER_ERROR(MCIERR_MULTIPLE);
	REGISTER_ERROR(MCIERR_EXTENSION_NOT_FOUND);
	REGISTER_ERROR(MCIERR_OUTOFRANGE);
	REGISTER_ERROR(MCIERR_FLAGS_NOT_COMPATIBLE);
	REGISTER_ERROR(MCIERR_FILE_NOT_SAVED);
	REGISTER_ERROR(MCIERR_DEVICE_TYPE_REQUIRED);
	REGISTER_ERROR(MCIERR_DEVICE_LOCKED);
	REGISTER_ERROR(MCIERR_DUPLICATE_ALIAS);
	REGISTER_ERROR(MCIERR_BAD_CONSTANT);
	REGISTER_ERROR(MCIERR_MUST_USE_SHAREABLE);
	REGISTER_ERROR(MCIERR_MISSING_DEVICE_NAME);
	REGISTER_ERROR(MCIERR_BAD_TIME_FORMAT);
	REGISTER_ERROR(MCIERR_NO_CLOSING_QUOTE);
	REGISTER_ERROR(MCIERR_DUPLICATE_FLAGS);
	REGISTER_ERROR(MCIERR_INVALID_FILE);
	REGISTER_ERROR(MCIERR_NULL_PARAMETER_BLOCK);
	REGISTER_ERROR(MCIERR_UNNAMED_RESOURCE);
	REGISTER_ERROR(MCIERR_NEW_REQUIRES_ALIAS);
	REGISTER_ERROR(MCIERR_NOTIFY_ON_AUTO_OPEN);
	REGISTER_ERROR(MCIERR_NO_ELEMENT_ALLOWED);
	REGISTER_ERROR(MCIERR_NONAPPLICABLE_FUNCTION);
	REGISTER_ERROR(MCIERR_ILLEGAL_FOR_AUTO_OPEN);
	REGISTER_ERROR(MCIERR_FILENAME_REQUIRED);
	REGISTER_ERROR(MCIERR_EXTRA_CHARACTERS);
	REGISTER_ERROR(MCIERR_DEVICE_NOT_INSTALLED);
	REGISTER_ERROR(MCIERR_GET_CD);
	REGISTER_ERROR(MCIERR_SET_CD);
	REGISTER_ERROR(MCIERR_SET_DRIVE);
	REGISTER_ERROR(MCIERR_DEVICE_LENGTH);
	REGISTER_ERROR(MCIERR_DEVICE_ORD_LENGTH);
	REGISTER_ERROR(MCIERR_NO_INTEGER);
	REGISTER_ERROR(MCIERR_WAVE_OUTPUTSINUSE);
	REGISTER_ERROR(MCIERR_WAVE_SETOUTPUTINUSE);
	REGISTER_ERROR(MCIERR_WAVE_INPUTSINUSE);
	REGISTER_ERROR(MCIERR_WAVE_SETINPUTINUSE);
	REGISTER_ERROR(MCIERR_WAVE_OUTPUTUNSPECIFIED);
	REGISTER_ERROR(MCIERR_WAVE_INPUTUNSPECIFIED);
	REGISTER_ERROR(MCIERR_WAVE_OUTPUTSUNSUITABLE);
	REGISTER_ERROR(MCIERR_WAVE_SETOUTPUTUNSUITABLE);
	REGISTER_ERROR(MCIERR_WAVE_INPUTSUNSUITABLE);
	REGISTER_ERROR(MCIERR_WAVE_SETINPUTUNSUITABLE);
	REGISTER_ERROR(MCIERR_SEQ_DIV_INCOMPATIBLE);
	REGISTER_ERROR(MCIERR_SEQ_PORT_INUSE);
	REGISTER_ERROR(MCIERR_SEQ_PORT_NONEXISTENT);
	REGISTER_ERROR(MCIERR_SEQ_PORT_MAPNODEVICE);
	REGISTER_ERROR(MCIERR_SEQ_PORT_MISCERROR);
	REGISTER_ERROR(MCIERR_SEQ_TIMER);
	REGISTER_ERROR(MCIERR_SEQ_PORTUNSPECIFIED);
	REGISTER_ERROR(MCIERR_SEQ_NOMIDIPRESENT);
	REGISTER_ERROR(MCIERR_NO_WINDOW);
	REGISTER_ERROR(MCIERR_CREATEWINDOW);
	REGISTER_ERROR(MCIERR_FILE_READ);
	REGISTER_ERROR(MCIERR_FILE_WRITE);
	REGISTER_ERROR(MCIERR_NO_IDENTITY);
	REGISTER_ERROR(MIXERR_INVALLINE);
	REGISTER_ERROR(MIXERR_INVALCONTROL);
	REGISTER_ERROR(MIXERR_INVALVALUE);
#undef REGISTER_ERROR
	default:
		return "DirectDraw::DDErrString(): Unknown Error Number";
	}
}

// -----------------------------------------------------------------------------

float Multimedia::ProcessDead(float value, float deadzone)
{
	BOOL negative = (value < 0);
	value = (float)fabs(value);	// gcc: abs(float) not in scope; fabs is (math.h). CWB

	if(value <= deadzone)
		value = 0;
	else
		value = (value - deadzone) / (1 - deadzone);

	return negative?-value:value;
}

// -----------------------------------------------------------------------------

BOOL Multimedia::FlightControl(FlightData& data, Matrix* pRotation, Vector* pTranslation, float scale)
{
	BOOL redraw = FALSE;

	Vector joyXYZ(UNINITIALIZED);
	joyXYZ.X() = ProcessDead(Multimedia::JoyTranslation(0).X(), DEAD_TX);
	joyXYZ.Y() = ProcessDead(Multimedia::JoyTranslation(0).Y(), DEAD_TY);
	joyXYZ.Z() = ProcessDead(Multimedia::JoyTranslation(0).Z(), DEAD_TZ);

	Vector joyYPR(UNINITIALIZED);
	joyYPR.X() = ProcessDead(Multimedia::JoyRotation(0).X(), DEAD_TX);
	joyYPR.Y() = ProcessDead(Multimedia::JoyRotation(0).Y(), DEAD_TY);
	joyYPR.Z() = ProcessDead(Multimedia::JoyRotation(0).Z(), DEAD_TZ);

	Vector joyR(ZERO);
	joyR.X() += joyXYZ.Y() * M_PI * scale / 30;
	joyR.Y() += joyXYZ.X() * M_PI * scale / 30;
	joyR.Z() += joyYPR.X() * M_PI * scale / 30;

	if(joyR != Vector(ZERO))
	{
		(*pRotation) = (*pRotation) * Matrix(ROTATION, joyR.X(), joyR.Y(), joyR.Z());
		redraw = TRUE;
	}
	
	Vector joyT(ZERO);
	DWORD buttons = Multimedia::JoyButtons(0);
	DWORD pressed = (data.oldButtons ^ buttons) & buttons;
	data.oldButtons = buttons;

	if(buttons & 1) joyT.Z() += 12 * scale;
	if(buttons & 2) joyT.Z() -= 12 * scale;

	if(joyT != Vector(ZERO))
	{
		(*pTranslation) += (*pRotation) * joyT;
		redraw = TRUE;
	}

	if(!Multimedia::JoyHatCentered(0))
	{
		float direction = Multimedia::JoyHat(0);
		joyT.X() = sin(direction) * 12 * scale;
		joyT.Y() = -cos(direction) * 12 * scale;
		joyT.Z() = 0;
		(*pTranslation) += (*pRotation) * joyT;
		redraw = TRUE;
	}

	data.oldRotation = joyR;
	data.oldTranslation = joyT;

	return redraw;
}

// -----------------------------------------------------------------------------

void Multimedia::PlayTrack(int track) 
{
	MCI_PLAY_PARMS mciPlayParms;
	mciPlayParms.dwFrom = MCI_MAKE_TMSF(track, 0, 0, 0);
	mciPlayParms.dwTo = MCI_MAKE_TMSF(track+1, 0, 0, 0);
	MCIERROR result = mciSendCommand(m_cdDevID, MCI_PLAY, MCI_FROM | MCI_TO, (DWORD)(LPVOID)&mciPlayParms);
	if(result == MCIERR_HARDWARE) return; // CD not present
	if(result != MMSYSERR_NOERROR) THROW_MM_EXCEPTION(result);
}

// -----------------------------------------------------------------------------
