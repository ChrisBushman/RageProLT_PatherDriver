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
#ifndef LIST_H
#define LIST_H
// -----------------------------------------------------------------------------

#include "util.h"

// -----------------------------------------------------------------------------

template<class T> class ListHead;
template<class T> class ListNode;
template<class T> class ListIterator;
template<class T> class TreeNode;
template<class T> class TreeIterator;

// -----------------------------------------------------------------------------

#define LIST_INVALID		((ListHead<T>*)(0))
#define LISTNODE_INVALID	((ListNode<T>*)(0))
#define LISTNODE_BEFORE		((ListNode<T>*)(-1))
#define LISTNODE_AFTER		((ListNode<T>*)(1))

#define TREE_INVALID		((TreeNode<T>*)(0))
#define TREENODE_INVALID	((TreeNode<T>*)(0))
#define TREENODE_BEFORE		((TreeNode<T>*)(-1))
#define TREENODE_AFTER		((TreeNode<T>*)(1))

// -----------------------------------------------------------------------------

/*
//        A
//       / \
//     B     C
//    / \   / \
//   D   E F   G
*/
enum IteratorType {
	BREADTH_FIRST,		// Breadth first: all siblings then children (A, B, C, D, E, F, G)
	DEPTH_FIRST,		// Depth first: children, then parents (D, E, B, F, G, C, A)
	PARENT_FIRST		// Depth first: parents, then children (A, B, D, E, C, F, G)
};

// -----------------------------------------------------------------------------

template<class T>
class ListHead {
public:
	~ListHead(void) { ASSERT(m_pFirst==NULL); ASSERT(m_pLast==NULL); }
	ListHead(void) : m_pFirst(NULL), m_pLast(NULL) { }

	const ListNode<T>* First(void) const { return m_pFirst; }
	ListNode<T>* First(void) { return m_pFirst; }
	const ListNode<T>* Last(void) const { return m_pLast; }
	ListNode<T>* Last(void) { return m_pLast; }

	const ::ListIterator<T> Iterator(void) const { return ::ListIterator<T>((ListHead<T>&)*this); }
	::ListIterator<T> Iterator(void) { return ::ListIterator<T>(*this); }

	void Insert(T* pNode);
	void InsertAfter(T* pAfter, T* pNode);
	void InsertLast(T* pNode);

	virtual const T* NodeToT(const ListNode<T>* n) const = 0;
	virtual T* NodeToT(ListNode<T>* n) = 0;
	virtual const ListNode<T>* TToNode(const T* t) const = 0;
	virtual ListNode<T>* TToNode(T* t) = 0;

	int Count(void) const;

protected:
	ListNode<T>*	m_pFirst;
	ListNode<T>*	m_pLast;

private:
	friend class ListNode<T>;
};


#define DECLARE_LISTHEAD_MEMBERS(name, T, member)			\
public:														\
	virtual const T* NodeToT(const ListNode<T>* n) const;	\
	virtual T* NodeToT(ListNode<T>* n);						\
	virtual const ListNode<T>* TToNode(const T* t) const;	\
	virtual ListNode<T>* TToNode(T* t)


#define DECLARE_LISTHEAD(name, T, member)	\
class name : public ListHead<T> {			\
DECLARE_LISTHEAD_MEMBERS(name, T, member);	\
}

#define DEFINE_LISTHEAD(name, T, member)		\
const T* name::NodeToT(const ListNode<T>* n) const { return n?(const T*)((const char*)n-((int)&((const T*)0)->member)):NULL; }	\
T* name::NodeToT(ListNode<T>* n) { return n?(T*)((char*)n-((int)&((const T*)0)->member)):NULL; }	\
const ListNode<T>* name::TToNode(const T* t) const { return t?&t->member:NULL; }	\
ListNode<T>* name::TToNode(T* t) { return t?&t->member:NULL; }



template<class T>
class ListNode {
public:
	~ListNode(void) { ASSERT(m_pHead==NULL); ASSERT(m_pPrev==NULL); ASSERT(m_pNext==NULL); }
	ListNode(void) : m_pHead(NULL), m_pPrev(NULL), m_pNext(NULL) { }

	const ListHead<T>* Head(void) const { return m_pHead; }
	ListHead<T>* Head(void) { return m_pHead; }
	const ListNode<T>* Next(void) const { return m_pNext; }
	ListNode<T>* Next(void) { return m_pNext; }
	const ListNode<T>* Prev(void) const { return m_pPrev; }
	ListNode<T>* Prev(void) { return m_pPrev; }

	const ListNode<T>* Node(void) const { return this; }
	ListNode<T>* Node(void) { return this; }

	void Remove(void);

	const T* Object(void) const { return m_pHead->NodeToT(this); }
	T* Object(void) { return m_pHead->NodeToT(this); }

protected:
private:
	ListHead<T>*	m_pHead;
	ListNode<T>*	m_pPrev;
	ListNode<T>*	m_pNext;

	friend class ListNode<T>;
	friend class ListHead<T>;
};

// -----------------------------------------------------------------------------

template<class T>
class ListIterator {
public:
	~ListIterator(void);
	ListIterator(const ListIterator& r);
	ListIterator& operator=(const ListIterator& r);

	const T*	Current(void) const;
	T*			Current(void);

	void GoToBeginning(void);
	void GoToEnd(void);
	void GoTo(T* node);
	void Next(void);
	void Prev(void);
	BOOL Done(void) const;

protected:
private:
	static ListIterator*	m_pFirst;
	ListIterator*			m_pNext;
	ListIterator*			m_pPrev;

	ListHead<T>*	m_pHead;
	ListNode<T>*	m_pCurrent;

	static void __HandleRemove(ListNode<T>* p);
	ListIterator(ListHead<T>& rHead);

	friend class ListHead<T>;
	friend class ListNode<T>;
};

// -----------------------------------------------------------------------------

template<class T>
void ListHead<T>::Insert(T* t)
{
	ASSERT(t);
	ListNode<T>* pNode = TToNode(t);

	ASSERT(pNode->Head() == NULL);
	ASSERT(pNode->Prev() == NULL);
	ASSERT(pNode->Next() == NULL);

	pNode->m_pHead = this;
	pNode->m_pPrev = NULL;
	pNode->m_pNext = m_pFirst;
	if(m_pFirst)
		m_pFirst->m_pPrev = pNode;
	else
		m_pLast = pNode;
	m_pFirst = pNode;
}

template<class T>
void ListHead<T>::InsertAfter(T* tA, T* tB)
{
	ASSERT(tA);
	ASSERT(tB);
	ListNode<T>* pNode = TToNode(tB);
	ListNode<T>* pAfter = TToNode(tA);
	ASSERT(pNode->Head() == NULL);
	ASSERT(pNode->Prev() == NULL);
	ASSERT(pNode->Next() == NULL);
	ASSERT(pAfter->Head() == this);

	pNode->m_pHead = this;
	pNode->m_pPrev = pAfter;
	pNode->m_pNext = pAfter->m_pNext;

	if(m_pLast == pAfter)
		m_pLast = pNode;

	if(pNode->m_pPrev) pNode->m_pPrev->m_pNext = pNode;
	if(pNode->m_pNext) pNode->m_pNext->m_pPrev = pNode;
}


template<class T>
void ListHead<T>::InsertLast(T* t)
{
	ASSERT(t);
	ListNode<T>* pNode = TToNode(t);
	ASSERT(pNode->Head() == NULL);
	ASSERT(pNode->Prev() == NULL);
	ASSERT(pNode->Next() == NULL);

	if(m_pLast)
		InsertAfter(NodeToT(m_pLast), t);
	else
	{
		m_pFirst = pNode;
		m_pLast = pNode;
	}
}

// -----------------------------------------------------------------------------

template<class T>
int ListHead<T>::Count(void) const
{
	int count=0;
	for(ListNode<T>* p=m_pFirst; p; p=p->m_pNext)
		count++;
	return count;
}

// -----------------------------------------------------------------------------

template<class T>
void ListNode<T>::Remove(void)
{
	::ListIterator<T>::__HandleRemove(this);

	if(m_pHead)
	{
		if(m_pHead->m_pFirst == this) m_pHead->m_pFirst = m_pNext;
		if(m_pHead->m_pLast == this) m_pHead->m_pLast = m_pPrev;
		m_pHead = NULL;

		if(m_pNext) m_pNext->m_pPrev = m_pPrev;
		if(m_pPrev) m_pPrev->m_pNext = m_pNext;
		m_pNext = NULL;
		m_pPrev = NULL;
	}
}

// -----------------------------------------------------------------------------

template<class T>
ListIterator<T>::~ListIterator<T>(void)
{
	if(this == m_pFirst) m_pFirst = m_pNext;
	if(m_pNext) m_pNext->m_pPrev = m_pPrev;
	if(m_pPrev) m_pPrev->m_pNext = m_pNext;
	m_pPrev = NULL;
	m_pNext = NULL;
}

template<class T>
ListIterator<T>::ListIterator<T>(ListHead<T>& rHead)
{
	m_pPrev = NULL;
	m_pNext = m_pFirst;
	m_pFirst = this;

	m_pHead = &rHead;
	m_pCurrent = LISTNODE_INVALID;
	
	GoToBeginning();
}

template<class T>
ListIterator<T>::ListIterator<T>(const ListIterator<T>& r)
{
	m_pPrev = NULL;
	m_pNext = m_pFirst;
	m_pFirst = this;

	m_pHead = r.m_pHead;
	m_pCurrent = r.m_pCurrent;
}

template<class T>
ListIterator<T>& ListIterator<T>::operator=(const ListIterator<T>& r)
{
	m_pHead = r.m_pHead;
	m_pCurrent = r.m_pCurrent;
	return *this;
}

// -----------------------------------------------------------------------------

template<class T>
const T* ListIterator<T>::Current(void) const
{
	if(m_pCurrent == LISTNODE_BEFORE) return NULL;
	else if(m_pCurrent == LISTNODE_AFTER) return NULL;
	else if(m_pCurrent == LISTNODE_INVALID) return NULL;
	else return m_pHead->NodeToT(m_pCurrent);
}

template<class T>
T* ListIterator<T>::Current(void)
{
	if(m_pCurrent == LISTNODE_BEFORE) return NULL;
	else if(m_pCurrent == LISTNODE_AFTER) return NULL;
	else if(m_pCurrent == LISTNODE_INVALID) return NULL;
	else return m_pHead->NodeToT(m_pCurrent);
}

// -----------------------------------------------------------------------------

template<class T>
void ListIterator<T>::GoToBeginning(void)
{
	if(m_pHead == LIST_INVALID) THROW_EXCEPTION();

	if(m_pHead->First())
		m_pCurrent = m_pHead->First();
	else
		m_pCurrent = LISTNODE_AFTER;
}

template<class T>
void ListIterator<T>::GoToEnd(void)
{
	if(m_pHead == LIST_INVALID) THROW_EXCEPTION();

	if(m_pHead->Last())
		m_pCurrent = m_pHead->Last();
	else
		m_pCurrent = LISTNODE_BEFORE;
}

template<class T>
void ListIterator<T>::GoTo(T* t)
{
	ASSERT(t);
	if(m_pHead == LIST_INVALID) THROW_EXCEPTION();
	ListNode<T>* pNode = m_pHead->TToNode(t);
	if(pNode->Head() != m_pHead) THROW_EXCEPTION();
	m_pCurrent = pNode;
}

template<class T>
void ListIterator<T>::Next(void)
{
	if(m_pCurrent == LISTNODE_BEFORE) GoToBeginning();
	else if(m_pCurrent == LISTNODE_AFTER) THROW_EXCEPTION();
	else if(m_pCurrent == LISTNODE_INVALID) THROW_EXCEPTION();
	else
	{
		if(m_pCurrent->Next())
			m_pCurrent = m_pCurrent->Next();
		else
			m_pCurrent = LISTNODE_AFTER;
	}
}

template<class T>
void ListIterator<T>::Prev(void)
{
	if(m_pCurrent == LISTNODE_BEFORE) THROW_EXCEPTION();
	else if(m_pCurrent == LISTNODE_AFTER) GoToEnd();
	else if(m_pCurrent == LISTNODE_INVALID) THROW_EXCEPTION();
	else
	{
		if(m_pCurrent->Prev())
			m_pCurrent = m_pCurrent->Prev();
		else
			m_pCurrent = LISTNODE_BEFORE;
	}
}

template<class T>
BOOL ListIterator<T>::Done(void) const
{
	if(m_pCurrent == LISTNODE_BEFORE) return TRUE;
	else if(m_pCurrent == LISTNODE_INVALID) return TRUE;
	else if(m_pCurrent == LISTNODE_AFTER) return TRUE;
	else return FALSE;
}

// -----------------------------------------------------------------------------

template<class T>
void ListIterator<T>::__HandleRemove(ListNode<T>* p)
{
	for(ListIterator<T>* pi=m_pFirst; pi; pi=pi->m_pNext)
	{
		if(p == pi->m_pCurrent)
			pi->Prev();
	}
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

template<class T>
class TreeNode {
public:
	~TreeNode(void) { ASSERT(m_pParent==NULL); ASSERT(m_pPrev==NULL); ASSERT(m_pNext==NULL); ASSERT(m_pChild==NULL); }
	TreeNode(void) : m_pParent(NULL), m_pPrev(NULL), m_pNext(NULL), m_pChild(NULL) { }

	const TreeNode<T>* Parent(void) const { return m_pParent->m_pParent?m_pParent:NULL; }
	TreeNode<T>* Parent(void) { return m_pParent->m_pParent?m_pParent:NULL; }
	const TreeNode<T>* Next(void) const { return m_pNext; }
	TreeNode<T>* Next(void) { return m_pNext; }
	const TreeNode<T>* Prev(void) const { return m_pPrev; }
	TreeNode<T>* Prev(void) { return m_pPrev; }
	const TreeNode<T>* Child(void) const { return m_pChild; }
	TreeNode<T>* Child(void) { return m_pChild; }

	const TreeNode<T>* LastChild(void) const { const TreeNode<T>* p=Child(); if(p) { while(p->Next()) p=p->Next(); } return p; }
	TreeNode<T>* LastChild(void) { TreeNode<T>* p=Child(); if(p) { while(p->Next()) p=p->Next(); } return p; }

	const TreeNode<T>* Node(void) const { return this; }
	TreeNode<T>* Node(void) { return this; }

	const ::TreeIterator<T> Iterator(IteratorType type) const { return ::TreeIterator<T>(type, (TreeNode<T>&)*this); }
	const ::TreeIterator<T> Iterator(void) const { return ::TreeIterator<T>(DEPTH_FIRST, (TreeNode<T>&)*this); }
	::TreeIterator<T> Iterator(IteratorType type) { return ::TreeIterator<T>(type, *this); }
	::TreeIterator<T> Iterator(void) { return ::TreeIterator<T>(DEPTH_FIRST, *this); }

	void InsertChild(T* pChild);
	void Remove(void);

	virtual const T* NodeToT(const TreeNode<T>* n) const = 0;
	virtual T* NodeToT(TreeNode<T>* n) = 0;
	virtual const TreeNode<T>* TToNode(const T* t) const = 0;
	virtual TreeNode<T>* TToNode(T* t) = 0;

	const T* Object(void) const { return this?NodeToT(this):NULL; }
	T* Object(void) { return this?NodeToT(this):NULL; }

protected:
private:
	TreeNode<T>*	m_pParent;
	TreeNode<T>*	m_pPrev;
	TreeNode<T>*	m_pNext;
	TreeNode<T>*	m_pChild;

	friend class TreeIterator<T>;
};



#define DECLARE_TREENODE(name, T, member)					\
class name : public TreeNode<T> {							\
public:														\
	virtual const T* NodeToT(const TreeNode<T>* n) const;	\
	virtual T* NodeToT(TreeNode<T>* n);						\
	virtual const TreeNode<T>* TToNode(const T* t) const;	\
	virtual TreeNode<T>* TToNode(T* t);						\
};



#define DEFINE_TREENODE(name, T, member)		\
const T* name::NodeToT(const TreeNode<T>* n) const { return n?(const T*)((const char*)n-((int)&((const T*)0)->member)):NULL; }	\
T* name::NodeToT(TreeNode<T>* n) { return n?(T*)((char*)n-((int)&((const T*)0)->member)):NULL; }	\
const TreeNode<T>* name::TToNode(const T* t) const { return t?&t->member:NULL; }	\
TreeNode<T>* name::TToNode(T* t) { return t?&t->member:NULL; }

// -----------------------------------------------------------------------------

template<class T>
class TreeIterator {
public:
	~TreeIterator(void);
	TreeIterator(const TreeIterator& r);
	TreeIterator& operator=(const TreeIterator& r);

	const T*	Current(void) const;
	T*			Current(void);

	void GoToBeginning(void);
	void GoToEnd(void);
	void Next(void);
	void Prev(void);
	BOOL Done(void) const;

	void SkipChildren(void);			// ONLY VALID FOR PARENT_FIRST

protected:
	TreeNode<T>* DescendLeft(TreeNode<T>* p, int depth);
	TreeNode<T>* DescendRight(TreeNode<T>* p, int depth);

	void BreadthFirstNext(void);
	void DepthFirstNext(void);
	void ParentFirstNext(void);

	void BreadthFirstPrev(void);
	void DepthFirstPrev(void);
	void ParentFirstPrev(void);

private:
	static TreeIterator*	m_pFirst;
	TreeIterator*			m_pNext;
	TreeIterator*			m_pPrev;

	IteratorType	m_type;
	TreeNode<T>*	m_pHead;
	TreeNode<T>*	m_pCurrent;
	int				m_depth;

	static void __HandleRemove(TreeNode<T>* p);
	TreeIterator(IteratorType type, TreeNode<T>& rHead);

	friend class TreeNode<T>;
};

// -----------------------------------------------------------------------------

template<class T>
void TreeNode<T>::InsertChild(T* t)
{
	ASSERT(t);
	TreeNode<T>* pChild = TToNode(t);
	ASSERT(pChild->m_pParent == NULL);
	ASSERT(pChild->Prev() == NULL);
	ASSERT(pChild->Next() == NULL);

	if(Child())
	{
		TreeNode<T>* p = Child();
		while(p->Next()) p = p->Next();
		p->Node()->m_pNext = pChild;
		pChild->Node()->m_pPrev = p;
		pChild->Node()->m_pParent = this;
	}
	else
	{
		m_pChild = pChild;
		pChild->m_pParent = this;
	}
}

template<class T>
void TreeNode<T>::Remove(void)
{
	::TreeIterator<T>::__HandleRemove(this);

	if(m_pParent)
	{
		if(m_pParent->Child() == this)
			m_pParent->m_pChild = Next();
		m_pParent = NULL;

		if(Next()) Next()->Node()->m_pPrev = Prev();
		if(Prev()) Prev()->Node()->m_pNext = Next();

		m_pPrev = NULL;
		m_pNext = NULL;
	}
}

// -----------------------------------------------------------------------------

template<class T>
TreeIterator<T>::~TreeIterator<T>(void)
{
	if(this == m_pFirst) m_pFirst = m_pNext;
	if(m_pNext) m_pNext->m_pPrev = m_pPrev;
	if(m_pPrev) m_pPrev->m_pNext = m_pNext;
	m_pPrev = NULL;
	m_pNext = NULL;
}

template<class T>
TreeIterator<T>::TreeIterator<T>(IteratorType type, TreeNode<T>& rHead)
{
	m_pPrev = NULL;
	m_pNext = m_pFirst;
	m_pFirst = this;

	m_type = type;
	m_pHead = &rHead;
	m_pCurrent = TREENODE_INVALID;
	m_depth = 0;

	GoToBeginning();
}

template<class T>
TreeIterator<T>::TreeIterator(const TreeIterator& r)	// gcc: ctor name takes no <T>. CWB
{
	m_pPrev = NULL;
	m_pNext = m_pFirst;
	m_pFirst = this;

	m_type = r.m_type;
	m_pHead = r.m_pHead;
	m_pCurrent = r.m_pCurrent;
	m_depth = r.m_depth;
}

template<class T>
TreeIterator<T>& TreeIterator<T>::operator=(const TreeIterator& r)	// was mis-named as ctor. CWB
{
	m_type = r.m_type;
	m_pHead = r.m_pHead;
	m_pCurrent = r.m_pCurrent;
	m_depth = r.m_depth;
	return *this;
}

// -----------------------------------------------------------------------------

template<class T>
BOOL TreeIterator<T>::Done(void) const
{
	if(m_pCurrent == TREENODE_BEFORE) return TRUE;
	else if(m_pCurrent == TREENODE_AFTER) return TRUE;
	else if(m_pCurrent == TREENODE_INVALID) return TRUE;
	else return FALSE;
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::GoToBeginning(void)
{
	if(m_pHead == TREE_INVALID) THROW_EXCEPTION();

	switch(m_type)
	{
	case BREADTH_FIRST:
		m_pCurrent = m_pHead->Child();
		m_depth = 1;
		break;

	case DEPTH_FIRST:
		m_pCurrent = m_pHead->Child();
		if(m_pCurrent)
			while(m_pCurrent->Child())
				m_pCurrent = m_pCurrent->Child();
		break;

	case PARENT_FIRST:
		m_pCurrent = m_pHead->Child();
		break;
	}

	if(m_pCurrent == NULL)
		m_pCurrent = TREENODE_AFTER;
}

template<class T>
void TreeIterator<T>::GoToEnd(void)
{
	if(m_pHead == TREE_INVALID) THROW_EXCEPTION();

	switch(m_type)
	{
	case BREADTH_FIRST:
		{
			int depth = 1;
			m_pCurrent = TREENODE_BEFORE;
			TreeNode<T>* p;
			do {
				p = DescendRight(m_pHead, depth);
				if(p)
					m_pCurrent = p;
				depth++;
			} while(p != NULL);
			depth--;
		}
		break;

	case DEPTH_FIRST:
		m_pCurrent = m_pHead->LastChild();
		break;

	case PARENT_FIRST:
		m_pCurrent = m_pHead->LastChild();
		if(m_pCurrent)
			while(m_pCurrent->Child())
				m_pCurrent = m_pCurrent->LastChild();
		break;
	}

	if(m_pCurrent == NULL)
		m_pCurrent = TREENODE_BEFORE;
}

// -----------------------------------------------------------------------------

template<class T>
const T* TreeIterator<T>::Current(void) const
{
	if(m_pCurrent == TREENODE_BEFORE) return NULL;
	else if(m_pCurrent == TREENODE_AFTER) return NULL;
	else if(m_pCurrent == TREENODE_INVALID) return NULL;
	else return NodeToT(m_pCurrent);
}



template<class T>
T* TreeIterator<T>::Current(void)
{
	if(m_pCurrent == TREENODE_BEFORE) return NULL;
	else if(m_pCurrent == TREENODE_AFTER) return NULL;
	else if(m_pCurrent == TREENODE_INVALID) return NULL;
	else return m_pCurrent->NodeToT(m_pCurrent);
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::Next(void)
{
	if(m_pCurrent == TREENODE_BEFORE) GoToBeginning();
	else if(m_pCurrent == TREENODE_AFTER) THROW_EXCEPTION();
	else if(m_pCurrent == TREENODE_INVALID) THROW_EXCEPTION();
	else
	{
		switch(m_type)
		{
		case BREADTH_FIRST: BreadthFirstNext(); break;
		case DEPTH_FIRST: DepthFirstNext(); break;
		case PARENT_FIRST: ParentFirstNext(); break;
		}
	}
}

template<class T>
void TreeIterator<T>::Prev(void)
{
	if(m_pCurrent == TREENODE_BEFORE) THROW_EXCEPTION();
	else if(m_pCurrent == TREENODE_AFTER) GoToEnd();
	else if(m_pCurrent == TREENODE_INVALID) THROW_EXCEPTION();
	else
	{
		switch(m_type)
		{
		case BREADTH_FIRST: BreadthFirstPrev(); break;
		case DEPTH_FIRST: DepthFirstPrev(); break;
		case PARENT_FIRST: ParentFirstPrev(); break;
		}
	}
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::BreadthFirstNext(void)
{
	if(m_pCurrent->Next())
	{
		m_pCurrent = m_pCurrent->Next();
		return;
	}

	m_pCurrent = m_pCurrent->m_pParent;
	int depth = m_depth-1;

	while(depth >= 0)
	{
		if(m_pCurrent->Next())
		{
			m_pCurrent = m_pCurrent->Next();
			TreeNode<T>* tmp = DescendLeft(m_pCurrent, m_depth - depth);
			if(tmp)
			{
				m_pCurrent = tmp;
				return;
			}
		}
		else
		{
			m_pCurrent = m_pCurrent->m_pParent;
			depth--;
		}
	}

	m_depth++;
	m_pCurrent = DescendLeft(m_pHead, m_depth);
	if(m_pCurrent == NULL)
		m_pCurrent = TREENODE_AFTER;
}

template<class T>
void TreeIterator<T>::BreadthFirstPrev(void)
{
	if(m_pCurrent->Prev())
	{
		m_pCurrent = m_pCurrent->Prev();
		return;
	}

	m_pCurrent = m_pCurrent->m_pParent;
	int depth = m_depth-1;

	while(depth >= 0)
	{
		if(m_pCurrent->Prev())
		{
			m_pCurrent = m_pCurrent->Prev();
			TreeNode<T>* tmp = DescendRight(m_pCurrent, m_depth - depth);
			if(tmp)
			{
				m_pCurrent = tmp;
				return;
			}
		}
		else
		{
			m_pCurrent = m_pCurrent->m_pParent;
			depth--;
		}
	}

	m_depth--;
	m_pCurrent = DescendRight(m_pHead, m_depth);
	if(m_pCurrent == NULL)
		m_pCurrent = TREENODE_BEFORE;
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::DepthFirstNext(void)
{
	if(m_pCurrent->Next())
	{
		m_pCurrent = m_pCurrent->Next();
		while(m_pCurrent->Child())
			m_pCurrent = m_pCurrent->Child();
	}
	else
	{
		m_pCurrent = m_pCurrent->m_pParent;
		if(m_pCurrent == m_pHead)
			m_pCurrent = TREENODE_AFTER;
	}
}

template<class T>
void TreeIterator<T>::DepthFirstPrev(void)
{
	if(m_pCurrent->Child())
		m_pCurrent = m_pCurrent->LastChild();
	else
	{
		if(m_pCurrent->Prev())
			m_pCurrent = m_pCurrent->Prev();
		else
		{
			while(m_pCurrent != m_pHead)
			{
				m_pCurrent = m_pCurrent->m_pParent;
				if(m_pCurrent->Prev())
				{
					m_pCurrent = m_pCurrent->Prev();
					break;
				}
			}

			if(m_pCurrent == m_pHead)
				m_pCurrent = TREENODE_BEFORE;
		}
	}
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::ParentFirstNext(void)
{
	if(m_pCurrent->Child())
		m_pCurrent = m_pCurrent->Child();
	else
		SkipChildren();
}

template<class T>
void TreeIterator<T>::ParentFirstPrev(void)
{
	if(m_pCurrent->Prev())
	{
		m_pCurrent = m_pCurrent->Prev();
		while(m_pCurrent->Child())
			m_pCurrent = m_pCurrent->LastChild();
	}
	else
	{
		m_pCurrent = m_pCurrent->m_pParent;
		if(m_pCurrent == m_pHead)
			m_pCurrent = TREENODE_BEFORE;
	}
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::SkipChildren(void) // ONLY VALID FOR PARENT_FIRST
{
	ASSERT(m_type == PARENT_FIRST);

	if(m_pCurrent == TREENODE_BEFORE) GoToBeginning();
	else if(m_pCurrent == TREENODE_INVALID) THROW_EXCEPTION();
	else if(m_pCurrent == TREENODE_AFTER) THROW_EXCEPTION();
	else
	{
		if(m_pCurrent->Next())
			m_pCurrent = m_pCurrent->Next();
		else
		{
			while(m_pCurrent != m_pHead)
			{
				m_pCurrent = m_pCurrent->m_pParent;
				if((m_pCurrent != m_pHead) && m_pCurrent->Next())
				{
					m_pCurrent = m_pCurrent->Next();
					break;
				}
			}
			
			if(m_pCurrent == m_pHead)
				m_pCurrent = TREENODE_AFTER;
		}
	}
}

// -----------------------------------------------------------------------------

template<class T>
void TreeIterator<T>::__HandleRemove(TreeNode<T>* p)
{
	for(TreeIterator<T>* pi=m_pFirst; pi; pi=pi->m_pNext)
	{
		if(p == pi->m_pHead)
		{
			pi->m_pHead = TREE_INVALID;
			pi->m_pCurrent = TREENODE_INVALID;
			pi->m_depth = 0;
		}

		if(p == pi->m_pCurrent)
			pi->Prev();
	}
}

// -----------------------------------------------------------------------------

template<class T>
TreeNode<T>* TreeIterator<T>::DescendLeft(TreeNode<T>* p, int depth)
{
	if(depth == 0) return p;
	p = p->Child();
	if(!p) return NULL;
	if(depth == 1) return p;
	while(p)
	{
		if(p->Child())
		{
			TreeNode<T>* tmp = DescendLeft(p, depth-1);
			if(tmp)
				return tmp;
		}

		p=p->Next();
	}

	return NULL;
}

template<class T>
TreeNode<T>* TreeIterator<T>::DescendRight(TreeNode<T>* p, int depth)
{
	if(depth == 0) return p;
	p = p->LastChild();
	if(!p) return NULL;
	if(depth == 1) return p;
	while(p)
	{
		if(p->Child())
		{
			TreeNode<T>* tmp = DescendRight(p, depth-1);
			if(tmp)
				return tmp;
		}

		p=p->Prev();
	}

	return NULL;
}

// -----------------------------------------------------------------------------
#endif
