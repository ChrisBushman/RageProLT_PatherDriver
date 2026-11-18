/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *******************************************************************************/
#ifndef ATI_WATCHERS_H
#define ATI_WATCHERS_H
// -----------------------------------------------------------------------------

#define DECLARE_POINTER_WATCHER(type, ptr, success, unique) _PointerWatcher<type> _WATCHER_ ## unique (ptr, success)
#define DECLARE_POINTER_WATCHER_ARRAY(type, ptr, success, unique) _PointerWatcherArray<type> _WATCHER_ ## unique (ptr, success)
#define DECLARE_POINTER_DOER(type, ptr, unique) _PointerDoer<type> _DOER_ ## unique (ptr)

#define DECLARE_MEMBER_WATCHER(type, result, var, fn, success, unique) _MemberWatcher<type, result> _WATCHER_ ## unique (var, &type::fn, success)
#define DECLARE_MEMBER_WATCHER_WINAPI(type, result, var, fn, success, unique) _MemberWatcher_WINAPI<type, result> _WATCHER_ ## unique (var, &type::fn, success)
#define DECLARE_MEMBER_DOER(type, result, var, fn, unique) _MemberDoer<type, result> _DOER_ ## unique (var, &type::fn)
#define DECLARE_MEMBER_DOER_WINAPI(type, result, var, fn, unique) _MemberDoer_WINAPI<type, result> _DOER_ ## unique (var, &type::fn)
#define DECLARE_MEMBER_DOER_DATA1(type, result, var, fn, dtype, data, unique) _MemberDoerData1<type, result, dtype> _DOER_ ## unique (var, &type::fn, data)

#define DECLARE_FUNCTION_WATCHER(type, result, var, fn, success, unique) _FunctionWatcher<type, result> _WATCHER_ ## unique (var, fn, success)
#define DECLARE_FUNCTION_WATCHER_WINAPI(type, result, var, fn, success, unique) _FunctionWatcher_WINAPI<type, result> _WATCHER_ ## unique (var, fn, success)
#define DECLARE_FUNCTION_DOER(type, result, var, fn, unique) _FunctionDoer<type, result> _DOER_ ## unique (var, fn)
#define DECLARE_FUNCTION_DOER_WINAPI(type, result, var, fn, unique) _FunctionDoer_WINAPI<type, result> _WATCHER_ ## unique (var, fn)

#define DECLARE_FUNCTION_WATCHER_NODATA(result, fn, success, unique) _FunctionWatcherNoData<result> _WATCHER_ ## unique (fn, success)
#define DECLARE_FUNCTION_DOER_NODATA(result, fn, unique) _FunctionDoerNoData<result> _DOER_ ## unique (fn)

// -----------------------------------------------------------------------------

template <class T>
class _PointerWatcher {
public:
	inline ~_PointerWatcher()
	{
		if((!m_rSuccess) && m_rT)
		{
			T p=m_rT;
			m_rT=NULL;
			delete p;
		}
	}

	inline _PointerWatcher(T& rT, BOOL& rSuccess) : m_rT(rT), m_rSuccess(rSuccess) { }

private:
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T>
class _PointerWatcherArray {
public:
	inline ~_PointerWatcherArray()
	{
		if((!m_rSuccess) && m_rT)
		{
			T p=m_rT;
			m_rT=NULL;
			delete [] p;
		}
	}

	inline _PointerWatcherArray(T& rT, BOOL& rSuccess) : m_rT(rT), m_rSuccess(rSuccess) { }

private:
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T>
class _PointerDoer {
public:
	inline ~_PointerDoer()
	{
		if(m_rT)
		{
			T p=m_rT;
			m_rT=NULL;
			delete p;
		}
	}

	inline _PointerDoer(T& rT) : m_rT(rT) { }

private:
	T&		m_rT;
};

// -----------------------------------------------------------------------------

template <class T, class R>
class _MemberWatcher {
public:
	inline ~_MemberWatcher()
	{
		if(!m_rSuccess)
		{
			(m_rT.*m_pFn)();
		}
	}

	inline _MemberWatcher(T& rT, R (T::* const pFn)(void), BOOL& rSuccess) : m_rT(rT), m_pFn(pFn), m_rSuccess(rSuccess) { }

private:
	R		(T::* const m_pFn)(void);
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T, class R>
class _MemberWatcher_WINAPI {
public:
	// gcc 3.4.5 rejects "R (WINAPI T::* const m_pFn)(void)" (stdcall keyword in a
	// pointer-to-member declarator inside a template); a typedef takes the same
	// __stdcall type and parses fine, preserving the calling convention. CWB 2026-09.
	typedef R (WINAPI T::* PFN)(void);
	inline ~_MemberWatcher_WINAPI()
	{
		if(!m_rSuccess)
		{
			(m_rT.*m_pFn)();
		}
	}

	inline _MemberWatcher_WINAPI(T& rT, PFN pFn, BOOL& rSuccess) : m_pFn(pFn), m_rT(rT), m_rSuccess(rSuccess) { }

private:
	PFN		m_pFn;
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T, class R>
class _MemberDoer {
public:
	inline ~_MemberDoer()
	{
		(m_rT.*m_pFn)();
	}

	inline _MemberDoer(T& rT, R (T::* const pFn)(void)) : m_rT(rT), m_pFn(pFn) { }

private:
	R		(T::* const m_pFn)(void);
	T&		m_rT;
};



template <class T, class R>
class _MemberDoer_WINAPI {
public:
	typedef R (WINAPI T::* PFN)(void);	// see _MemberWatcher_WINAPI note. CWB 2026-09.
	inline ~_MemberDoer_WINAPI()
	{
		(m_rT.*m_pFn)();
	}

	inline _MemberDoer_WINAPI(T& rT, PFN pFn) : m_pFn(pFn), m_rT(rT) { }

private:
	PFN		m_pFn;
	T&		m_rT;
};



template <class T, class R, class D>
class _MemberDoerData1 {
public:
	inline ~_MemberDoerData1()
	{
		(m_rT.*m_pFn)(m_rData);
	}

	inline _MemberDoerData1(T& rT, R (T::* const pFn)(D), D& rData) : m_rT(rT), m_pFn(pFn), m_rData(rData) { }

private:
	R		(T::* const m_pFn)(D);
	T&		m_rT;
	D&		m_rData;
};

// -----------------------------------------------------------------------------

template <class T, class R>
class _FunctionWatcher {
public:
	inline ~_FunctionWatcher()
	{
		if(!m_rSuccess)
		{
			(*m_pFn)(m_rT);
		}
	}

	inline _FunctionWatcher(T& rT, R (* const pFn)(T), BOOL& rSuccess) : m_rT(rT), m_pFn(pFn), m_rSuccess(rSuccess) { }

private:
	R		(* const m_pFn)(T);
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T, class R>
class _FunctionWatcher_WINAPI {
public:
	typedef R (WINAPI * PFN)(T);	// see _MemberWatcher_WINAPI note. CWB 2026-09.
	inline ~_FunctionWatcher_WINAPI()
	{
		if(!m_rSuccess)
		{
			(*m_pFn)(m_rT);
		}
	}

	inline _FunctionWatcher_WINAPI(T& rT, PFN pFn, BOOL& rSuccess) : m_pFn(pFn), m_rT(rT), m_rSuccess(rSuccess) { }

private:
	PFN		m_pFn;
	T&		m_rT;
	BOOL&	m_rSuccess;
};



template <class T, class R>
class _FunctionDoer {
public:
	inline ~_FunctionDoer()
	{
		(*m_pFn)(m_rT);
	}

	inline _FunctionDoer(T& rT, R (* const pFn)(T)) : m_rT(rT), m_pFn(pFn) { }

private:
	R		(* const m_pFn)(T);
	T&		m_rT;
};



template <class T, class R>
class _FunctionDoer_WINAPI {
public:
	typedef R (WINAPI * PFN)(T);	// see _MemberWatcher_WINAPI note. CWB 2026-09.
	inline ~_FunctionDoer_WINAPI()
	{
		(*m_pFn)(m_rT);
	}

	inline _FunctionDoer_WINAPI(T& rT, PFN pFn) : m_pFn(pFn), m_rT(rT) { }

private:
	PFN		m_pFn;
	T&		m_rT;
};

// -----------------------------------------------------------------------------

template<class R>
class _FunctionWatcherNoData {
public:
	inline ~_FunctionWatcherNoData()
	{
		if(!m_rSuccess)
		{
			(*m_pFn)();
		}
	}

	inline _FunctionWatcherNoData(void (*pFn)(void), BOOL& rSuccess) : m_pFn(pFn), m_rSuccess(rSuccess) { }

private:
	R		(*m_pFn)(void);
	BOOL&	m_rSuccess;
};



template<class R>
class _FunctionDoerNoData {
public:
	inline ~_FunctionDoerNoData()
	{
		(*m_pFn)();
	}

	inline _FunctionDoerNoData(void (*pFn)(void)) : m_pFn(pFn) { }

private:
	R		(*m_pFn)(void);
};

// -----------------------------------------------------------------------------
#endif
