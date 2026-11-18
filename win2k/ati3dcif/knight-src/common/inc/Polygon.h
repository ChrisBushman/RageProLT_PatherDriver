/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *  Polygon face and vertex creation functionality							   *
 *******************************************************************************/
#ifndef POLYGON_H
#define POLYGON_H
// -----------------------------------------------------------------------------

#include "Vertex.h"
#include "list.h"

class Clipper;
class Group;
class Object;

// -----------------------------------------------------------------------------

#define MAX_VERTICES_IN_POLYGON 100
#define DEFAULT_GROUP_NAME	"Default Group"

// -----------------------------------------------------------------------------

enum PolyType {
	POLYGON_LIST,
	POLYGON_STRIP
};



struct PolyFace {
	Normal	normal;
	int		vertices[3];
	DWORD	selected:1;
};



class Poly {
public:
	~Poly(void);
	Poly(PolyType type, int nVertices, int nFaces);

	PolyType		Type(void) const { return m_type; }
	int				VertexCount(void) const { return m_nVertices; }
	int				FaceCount(void) const { return m_nFaces; }

	void SetType(PolyType t) { m_type = t; }

	Vertex&			GetVertex(int n) { ASSERT((n>=0)&&(n<m_nVertices)); return m_pVertices[n]; }
	const Vertex&	GetVertex(int n) const { ASSERT((n>=0)&&(n<m_nVertices)); return m_pVertices[n]; }

	PolyFace&		GetFace(int n) { ASSERT((n>=0)&&(n<m_nFaces)); return m_pFaces[n]; }
	const PolyFace&	GetFace(int n) const { ASSERT((n>=0)&&(n<m_nFaces)); return m_pFaces[n]; }

	Vertex&			GetFaceVertex(int f, int v) { ASSERT((v>=0)&&(v<3)); return GetVertex(GetFace(f).vertices[v]); }
	const Vertex&	GetFaceVertex(int f, int v) const { ASSERT((v>=0)&&(v<3)); return GetVertex(GetFace(f).vertices[v]); }

	Normal&			GetFaceNormal(int n) { return GetFace(n).normal; }
	const Normal&	GetFaceNormal(int n) const { return GetFace(n).normal; }

	ListNode<Poly>& Node(void) { return m_node; }
	const ListNode<Poly>& Node(void) const { return m_node; }

	void	Read(fileHandle& fh) ;
	void	Write(fileHandle& fh) ;

protected:
private:
	ListNode<Poly>	m_node;

	PolyType	m_type;

	int			m_nVertices;
	Vertex*		m_pVertices;

	int			m_nFaces;
	PolyFace*	m_pFaces;

	Poly(const Poly&);
	Poly& operator=(const Poly&);

	friend class PolyHead;
};

typedef ListIterator<Poly>	PolyIterator;
DECLARE_LISTHEAD(PolyHead, Poly, m_node);

// -----------------------------------------------------------------------------

DECLARE_TREENODE(GroupNode, Group, m_node);
typedef TreeIterator<Group>	GroupIterator;
typedef GroupNode GroupHead;



class Group {
public:
	~Group(void);
	Group(char* name);

	int PolyCount(void) const;
	int FaceCount(void) const;
	int VertexCount(void) const;

	void Draw(Clipper& clipper, VertexType type);

	BOOL Select(float* pT, Group** ppGroup, Poly** ppPoly, int* pFace, const POINT& pt, XFormStack& gfxStack, BOOL facingOnly);

	const char*	Name(void) const { return m_name; }
	char*		Name(void) { return m_name; }

	BOOL	Visible(void) const { return m_visible; }
	BOOL&	Visible(void) { return m_visible; }

	BOOL	Hidden(void) const { return m_hidden; }
	BOOL&	Hidden(void) { return m_hidden; }

	BOOL	Contains(Poly* pPoly) const;

	const ::PolyHead& PolyHead(void) const { return m_polyHead; }
	::PolyHead& PolyHead(void) { return m_polyHead; }

	void	Read(fileHandle& fh) ;
	void	Write(fileHandle& fh) ;

	const Vector& Translation(void) const { return m_translation; }
	Vector& Translation(void) { return m_translation; }

	const Vector& Rotation(void) const { return m_rotation; }
	Vector& Rotation(void) { return m_rotation; }

	const Vector& Center(void) const { return m_center; }
	Vector& Center(void) { return m_center; }

	const Matrix& RotationCopy(void) const { return m_rotationCopy; }
	const Vector& TranslationCopy(void) const { return m_translationCopy; }

	void Latch(void)
	{
		m_rotationCopy = Matrix(ROTATION, m_rotation);
		m_translationCopy = Center() + m_translation + m_rotationCopy * (-Center());
	}

	const ::GroupNode& Node(void) const { return m_node; }
	::GroupNode& Node(void) { return m_node; }

protected:
private:
	GroupNode		m_node;
	::PolyHead		m_polyHead;

	char			m_name[65];
	BOOL			m_visible;
	BOOL			m_hidden;

	Vector			m_center;
	Vector			m_translation;
	Vector			m_translationCopy;
	Vector			m_rotation;
	Matrix			m_rotationCopy;

	Group(const Group&);
	Group& operator=(const Group&);

	friend class GroupNode;	// GroupHead is a typedef of class GroupNode; befriend the real class. CWB
};

// -----------------------------------------------------------------------------

class Object {
public:
	~Object(void);
	Object(void);

	::GroupHead& GroupHead(void) { return m_groupHead; }
	const ::GroupHead& GroupHead(void) const { return m_groupHead; }

	Group* GroupNumber(int index);
	const Group* GroupNumber(int index) const;

	Group*	FindGroup(char* name) const;
	Group*	FindGroup(Poly* pPoly) const;

	const Vector& Translation(void) const { return m_translation; }
	Vector& Translation(void) { return m_translation; }

	void Latch(void);

	const Vector& TranslationCopy(void) const { return m_translationCopy; }

	void Draw(Clipper& clipper, VertexType type);

	BOOL Select(float* pT, Group** ppGroup, Poly** ppPoly, int* pFace, const POINT& pt, XFormStack& gfxStack, BOOL facingOnly);

	int	GroupCount(void) const;
	int PolyCount(void) const;
	int FaceCount(void) const;
	int VertexCount(void) const;

	void	Read(fileHandle& fh) ;
	void	Write(fileHandle& fh) ;

	static Object* ObjectFromGroupHead(const ListHead<Group>* h) { return h?(Object*)((char*)(h)-((int)&((Object*)0)->m_groupHead)):NULL; }

protected:
private:
	::GroupHead	m_groupHead;
	Vector		m_translation;
	Vector		m_translationCopy;
};

// -----------------------------------------------------------------------------
#endif
