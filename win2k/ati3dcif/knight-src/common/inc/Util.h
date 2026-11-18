/*******************************************************************************
 *	ATI 3D RAGE SDK sample code												   *	
 *																			   *
 *  Knight Demo																   *
 *																			   *
 *  Copyright (c) 1996-1997 ATI Technologies, Inc.  All rights reserved.	   *	
 *  																		   *
 * Written by Aaron Orenstein												   *
 *  																		   *
 *	Miscellaneous utilities for file-handling, linked-list management and	   *
 *	error-handling.															   *
 *******************************************************************************/
#ifndef UTIL_H
#define UTIL_H
// -----------------------------------------------------------------------------

#include <stdio.h>
#include "types.h"

// -----------------------------------------------------------------------------

#pragma warning(disable:4290) // C++ Exception Specification ignored

// -----------------------------------------------------------------------------

#define M_PI 3.14159265359

#define DEG2RAD(d) ((d) * 3.1415927 / 180.0)
#define RAD2DEG(r) ((r) * 180.0 / 3.1415927)

// -----------------------------------------------------------------------------

class List {
public:

protected:
	virtual ~List();
	List();

	void Unlink(void);
	void Link(List** ppHead);

	List* Next(void) const { ASSERT(m_ppHead); return m_pNext; }
	List* Prev(void) const { ASSERT(m_ppHead); return m_pPrev; }
	List** Head(void) const { ASSERT(m_ppHead); return m_ppHead; }

private:
	List** m_ppHead;
	List* m_pNext;
	List* m_pPrev;
};

// -----------------------------------------------------------------------------

extern void ErrorBox(char* string, ...);

// -----------------------------------------------------------------------------

template <class T>
class ExceptionTemplate {
public:
	~ExceptionTemplate() { }
	ExceptionTemplate(const char* type, const char* file, int line, const char* error, T result)
	{
		m_file = file;
		m_line = line;
		char buf1[256];	sprintf(buf1, "%s@%d: %s", file, line, error);
		char buf2[256];	sprintf(buf2, "%s Exception", type);
		TRACE("%s: %s\n", buf2, buf1);
		FILE* pFile = fopen("exceptions.err", "a+");
		if(pFile)
		{
			fprintf(pFile, "%s: %s\n", buf2, buf1);
			fclose(pFile);
		}
//		MessageBox(NULL, buf1, buf2, MB_OK | MB_APPLMODAL | MB_ICONERROR);
		m_result = result;
	}

	ExceptionTemplate(const char* type, const char* file, int line)
	{
		m_file = file;
		m_line = line;
		char buf1[256]; sprintf(buf1, "%s@%d", file, line);
		char buf2[256];	sprintf(buf2, "%s Exception", type);
		TRACE("%s: %s\n", buf2, buf1);
//		MessageBox(NULL, buf1, buf2, MB_OK | MB_APPLMODAL | MB_ICONERROR);
	}

	const T& Reason(void) const { return m_result; }

protected:
private:
	T			m_result;
	const char* m_file;
	int			m_line;
};


typedef ExceptionTemplate<int> Exception;
#define THROW_EXCEPTION() throw Exception("Internal Program", __FILE__, __LINE__)

// -----------------------------------------------------------------------------

enum FHERROR {
	FHERROR_NOERROR,
	FHERROR_UNKNOWN,
	FHERROR_CANNOTOPEN,
	FHERROR_ALREADYOPEN,
	FHERROR_NOTOPEN,
	FHERROR_EOF,
	FHERROR_CANNOTREAD,
	FHERROR_CANNOTWRITE,
	FHERROR_CANNOTSEEK
};

typedef ExceptionTemplate<FHERROR> FH_Exception;


class fileHandle {
public:
	virtual ~fileHandle(void) /*  */;
	fileHandle(void) /*  */;
	fileHandle(const char* filename, const char* mode) /*  */;

	virtual void open(const char* filename, const char* mode) ;
	virtual void close(void) ;
	virtual void read(void* pDst, int nChars) const ;
	virtual int  readAtMost(void* pDst, int nChars) const ;
	virtual void write(const void* pSrc, int nChars) const ;
	virtual void seek(int offset, int whence) const ;
	virtual int  tell(void) const ;

	nonvirtual void readInt8(int8* pDst) const      { read(pDst, sizeof(*pDst)); }
	nonvirtual void readUint8(uint8* pDst) const    { read(pDst, sizeof(*pDst)); }
	nonvirtual void readInt16(int16* pDst) const    { read(pDst, sizeof(*pDst)); }
	nonvirtual void readUint16(uint16* pDst) const  { read(pDst, sizeof(*pDst)); }
	nonvirtual void readInt32(int32* pDst) const    { read(pDst, sizeof(*pDst)); }
	nonvirtual void readUint32(uint32* pDst) const  { read(pDst, sizeof(*pDst)); }
	nonvirtual void readReal32(real32* pDst) const  { read(pDst, sizeof(*pDst)); }
	nonvirtual void readReal64(real64* pDst) const  { read(pDst, sizeof(*pDst)); }

	nonvirtual void writeInt8(int8 src) const      { write(&src, sizeof(src)); }
	nonvirtual void writeUint8(uint8 src) const    { write(&src, sizeof(src)); }
	nonvirtual void writeInt16(int16 src) const    { write(&src, sizeof(src)); }
	nonvirtual void writeUint16(uint16 src) const  { write(&src, sizeof(src)); }
	nonvirtual void writeInt32(int32 src) const    { write(&src, sizeof(src)); }
	nonvirtual void writeUint32(uint32 src) const  { write(&src, sizeof(src)); }
	nonvirtual void writeReal32(real32 src) const  { write(&src, sizeof(src)); }
	nonvirtual void writeReal64(real64 src) const  { write(&src, sizeof(src)); }

	nonvirtual int8   readInt8(void) const    { int8 r;  read(&r, sizeof(r)); return r; }
	nonvirtual uint8  readUint8(void) const   { uint8 r; read(&r, sizeof(r)); return r; }
	nonvirtual int16  readInt16(void) const   { int16 r;  read(&r, sizeof(r)); return r; }
	nonvirtual uint16 readUint16(void) const  { uint16 r; read(&r, sizeof(r)); return r; }
	nonvirtual int32  readInt32(void) const   { int32 r;  read(&r, sizeof(r)); return r; }
	nonvirtual uint32 readUint32(void) const  { uint32 r; read(&r, sizeof(r)); return r; }
	nonvirtual real32 readReal32(void) const  { real32 r; read(&r, sizeof(r)); return r; }
	nonvirtual real64 readReal64(void) const  { real64 r; read(&r, sizeof(r)); return r; }

	nonvirtual void rewind(void) const  { seek(0, SEEK_SET); }

	static const char* ErrString(FHERROR reason);

	nonvirtual FILE* HANDLE(void) const { return m_pFile; }

private:
	FILE* m_pFile;

	fileHandle(const fileHandle& fh); /* NOT ALLOWED */
	fileHandle& operator=(const fileHandle& fh); /* NOT ALLOWED */
};

#define THROW_FH_EXCEPTION(r) throw FH_Exception("FileHandle", __FILE__, __LINE__, fileHandle::ErrString(r), r)

// -----------------------------------------------------------------------------

//#ifdef _DEBUG
extern __int64 RDTSC(void);
//#endif

// -----------------------------------------------------------------------------
#endif
