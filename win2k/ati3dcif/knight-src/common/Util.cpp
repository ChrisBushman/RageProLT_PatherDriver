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
#include "stdwin.h"
#include <stdarg.h>
#include "util.h"

// -----------------------------------------------------------------------------

#ifdef _DEBUG
#define CHECK_DOS_FILENAMES
#endif

// -----------------------------------------------------------------------------

List::~List()
{
	ASSERT(m_ppHead == NULL);
	ASSERT(m_pNext == NULL);
	ASSERT(m_pPrev == NULL);
}



List::List()
{
	m_ppHead = NULL;
	m_pNext = NULL;
	m_pPrev = NULL;
}

// -----------------------------------------------------------------------------

void List::Unlink(void)
{
	ASSERT(m_ppHead);

	if((*m_ppHead) == this)
	{
		(*m_ppHead) = m_pNext;
		ASSERT(m_pPrev == NULL);
	}

	m_ppHead = NULL;

	if(m_pNext) m_pNext->m_pPrev = m_pPrev;
	if(m_pPrev) m_pPrev->m_pNext = m_pNext;
	m_pPrev = NULL;
	m_pNext = NULL;
}



void List::Link(List** ppHead)
{
	ASSERT(ppHead);
	ASSERT(m_ppHead == NULL);

	m_ppHead = ppHead;
	m_pPrev = NULL;
	m_pNext = (*m_ppHead);
	if(m_pNext) m_pNext->m_pPrev = this;
	(*m_ppHead) = this;
}

// -----------------------------------------------------------------------------

void ErrorBox(char* string, ...)
{
	va_list ap;
	char buffer[1024];
	va_start(ap, string);
	vsprintf(buffer, string, ap);
	va_end(ap);

	MessageBox(NULL, buffer, "Error", MB_OK);
	PostQuitMessage(0);
}

// -----------------------------------------------------------------------------

fileHandle::~fileHandle(void) /*  */
{
	if(m_pFile)
		fclose(m_pFile);
	m_pFile = NULL;
}

// -----------------------------------------------------------------------------

fileHandle::fileHandle(void) /*  */
{
	m_pFile = NULL;
}



fileHandle::fileHandle(const char* filename, const char* mode) /*  */
{
	m_pFile = NULL;
	open(filename, mode);
}

// -----------------------------------------------------------------------------

void fileHandle::open(const char* filename, const char* mode) 
{
	ASSERT(filename);
	ASSERT(mode);

#ifdef CHECK_DOS_FILENAMES
	{
		BOOL compliant = FALSE;
		const char* ptr = strrchr(filename, '\\');
		if(ptr)
			ptr++;
		else
			ptr = filename;
		const char* dot = strchr(ptr, '.');
		if(dot)
		{
			if(((dot - ptr) <= 8) && (strlen(dot+1) <= 3))
				compliant = TRUE;
		}
		else
			if(strlen(ptr) <= 8)
				compliant = TRUE;

		if(!compliant)
			TRACE("WARNING: File \"%s\" is not 8.3 compliant\n", filename);
	}
#endif

	if(m_pFile) THROW_FH_EXCEPTION(FHERROR_ALREADYOPEN);
	m_pFile = fopen(filename, mode);
	if(m_pFile == NULL)
	{
		FILE* pFile = fopen("exceptions.err", "a+");
		if(pFile)
		{
			fprintf(pFile, "Cannot open file: \"%s\"\n", filename);
			fclose(pFile);
		}

		THROW_FH_EXCEPTION(FHERROR_CANNOTOPEN);
	}
}



void fileHandle::close(void) 
{
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = fclose(m_pFile);
	m_pFile = NULL;
	if(result != 0) THROW_FH_EXCEPTION(FHERROR_UNKNOWN);
}



void fileHandle::read(void* pDst, int nChars) const 
{
	ASSERT(pDst);
	ASSERT(nChars >= 0);
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = fread(pDst, 1, nChars, m_pFile);
	if((result == 0) && feof(m_pFile)) THROW_FH_EXCEPTION(FHERROR_EOF);
	if(result != nChars) THROW_FH_EXCEPTION(FHERROR_CANNOTREAD);
}



int fileHandle::readAtMost(void* pDst, int nChars) const 
{
	ASSERT(pDst);
	ASSERT(nChars >= 0);
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = fread(pDst, 1, nChars, m_pFile);
	if((result == 0) && feof(m_pFile)) THROW_FH_EXCEPTION(FHERROR_EOF);
	return result;
}



void fileHandle::write(const void* pSrc, int nChars) const 
{
	ASSERT(pSrc);
	ASSERT(nChars >= 0);
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = fwrite(pSrc, 1, nChars, m_pFile);
	if(result != nChars) THROW_FH_EXCEPTION(FHERROR_CANNOTWRITE);
}



void fileHandle::seek(int offset, int whence) const 
{
	ASSERT((whence==SEEK_SET) || (whence==SEEK_CUR) || (whence==SEEK_END));
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = fseek(m_pFile, offset, whence);
	if(result != 0) THROW_FH_EXCEPTION(FHERROR_CANNOTSEEK);
}



int fileHandle::tell(void) const 
{
	if(!m_pFile) THROW_FH_EXCEPTION(FHERROR_NOTOPEN);
	int result = ftell(m_pFile);
	if(result == -1) THROW_FH_EXCEPTION(FHERROR_UNKNOWN);
	return result;
}

// -----------------------------------------------------------------------------

const char* fileHandle::ErrString(FHERROR err)
{
	switch(err)
	{
#define REGISTER_ERROR(e) case e : return #e
		REGISTER_ERROR(FHERROR_NOERROR);
		REGISTER_ERROR(FHERROR_UNKNOWN);
		REGISTER_ERROR(FHERROR_CANNOTOPEN);
		REGISTER_ERROR(FHERROR_ALREADYOPEN);
		REGISTER_ERROR(FHERROR_NOTOPEN);
		REGISTER_ERROR(FHERROR_EOF);
		REGISTER_ERROR(FHERROR_CANNOTREAD);
		REGISTER_ERROR(FHERROR_CANNOTWRITE);
		REGISTER_ERROR(FHERROR_CANNOTSEEK);
#undef REGISTER_ERROR
	default:
		return "fileHandle::DDErrString(): Unknown Error Number";
	}
}

// -----------------------------------------------------------------------------

//#ifdef _DEBUG
// Rewritten from MSVC naked "__asm _emit 0x0F/0x31" (the rdtsc opcode) to GCC inline asm. CWB
__int64 RDTSC(void)
{
	unsigned int lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return (((__int64)hi) << 32) | lo;
}
//#endif

// -----------------------------------------------------------------------------
