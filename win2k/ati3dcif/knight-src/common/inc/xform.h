/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 * 	Screen, world and view tranform functionality.							   *
 *******************************************************************************/
#ifndef XFORM_H
#define XFORM_H
// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#include "util.h"
#include "matrix.h"
#include "vertex.h"

// -----------------------------------------------------------------------------

#define NUMBER_OF_XFORMS 101

// -----------------------------------------------------------------------------

class XFormStack {
public:
	~XFormStack(void);
	XFormStack(void);

	void TransformView(const Matrix& rotation, const Vector& translation);
	void TransformObject(const Matrix& rotation, const Vector& translation);
	void TransformDone(void);

	void SetWindow(int x, int y, int width, int height, float fov);

	void SetFarClipPlane(float farPlane) { m_farPlane = farPlane; m_zScalar = m_farPlane * 65535.0 / (m_farPlane - 1); }
	float FarClipPlane(void) const { return m_farPlane; }

	BOOL IsFacing(const Normal& normal) { return (DotProduct(normal, m_current.view) > 0); }
	BOOL IsFacingLight(const Normal& normal) { return (DotProduct(normal, ObjectLight()) > 0); }

	const Vector& ObjectLight(void) const { return m_pCurrentXForm->m_light; }
	Vector& ObjectLight(void) { return m_pCurrentXForm->m_light; }

	const float ZBias(void) const { return m_zBias; }
	float& ZBias(void) { return m_zBias; }

	void DepthCueOn(BOOL on) { m_depthCue = on; }
	void SetDepthCue(float scale) { m_depthCueScale = scale; m_invDepthCueScale = 1.0 / scale; }

	void LightingOn(BOOL on) { m_lighting = on; }
	void SetLighting(float scale, float ambient) { m_lightScale = scale; m_lightAmbient = ambient; }
	float LightScale(void) const { return m_lightScale; }
	float LightAmbient(void) const { return m_lightAmbient; }

	TLVertex* TransformPoint(TLVertex* pTL, Vertex* pV, VertexType type);
	Normal	TransformNormal(const Normal& rSrc) const;
	Normal	UntransformNormal(const Normal& rSrc) const;

	void ComputeScreenCoordinate(TLVertex *pVertex, VertexType type);
	void InvertScreenCoordinate(Vector* a, Vector* b, const POINT& point);

	const Vector& CurrentTranslation(void) const { return m_current.translation; }
	const Matrix& CurrentRotation(void) const { return m_current.rotation; }

protected:
private:
	class XForm {
	public:
		void Free(void);
		static XForm* Alloc(void) ;

	protected:
	private:
		Matrix		m_rotation;
		Vector		m_translation;
		Vector		m_view;
		Vector		m_light;
		XForm*		m_pPrevious;
		TLVertex*	m_pSavedVertex;

		static XForm	m_XFormArray[NUMBER_OF_XFORMS];
		static XForm*	m_pFreeList;

		~XForm(void) { }
		XForm(void) : m_rotation(UNINITIALIZED), m_translation(UNINITIALIZED), m_view(UNINITIALIZED), m_light(UNINITIALIZED) { }
		XForm(const XForm&);			// Not allowed
		XForm& operator=(const XForm&);	// Not allowed

		friend class XFormStack;
	};

	XForm*	m_pCurrentXForm;
	Matrix	m_prescalar;	// For things like FOV, etc...

	struct CurrentXForm {
		CurrentXForm() : rotation(IDENTITY), translation(ZERO), view(ZERO) { }

		Matrix	rotation;
		Vector	translation;
		Vector	view;
	}		m_current;

	float	m_farPlane;
	float	m_zScalar;
	float	m_halfWidth, m_halfHeight;
	float	m_centerX, m_centerY;

	BOOL	m_depthCue;
	float	m_depthCueScale;
	float	m_invDepthCueScale;

	BOOL	m_lighting;
	float	m_lightScale;
	float	m_lightAmbient;

	float	m_zBias;

	void RecomputeCurrent(void);

	XFormStack(XFormStack&);					// Not allowed
	XFormStack& operator=(XFormStack&);			// Not allowed
};

// -----------------------------------------------------------------------------
#endif
