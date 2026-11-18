/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein                                                  *
 *  																		   *
 *******************************************************************************/
#ifndef SCRIPT_H
#define SCRIPT_H
// -----------------------------------------------------------------------------

#include "matrix.h"

// -----------------------------------------------------------------------------

enum CameraPathType {
	PATHTYPE_CURVED,
	PATHTYPE_LINEAR
};

class CameraScript {
public:
	~CameraScript(void);
	CameraScript(char* pFilename);

	void Init(void);
	void Tick(void);
	void Copy(void);

protected:
	void SetupPath(int from, int to);

private:
	struct CameraControl {
		Vector			position;
		Vector			lookAt;
		int				count;
		CameraPathType	pathtype;
	};

	CameraControl*	m_pControl;
	int				m_nControl;

	int				m_camera;

	Vector			m_positionA;
	Vector			m_lookAtA;
	float			m_radiusA;

	Vector			m_positionB;
	Vector			m_lookAtB;
	float			m_radiusB;

	CameraPathType	m_pathtype;
	Vector			m_center;

	int				m_tick;
	int				m_count;
	
	BOOL	ReadScriptLine(CameraControl& camera, fileHandle& fh);
};

// -----------------------------------------------------------------------------
#endif
