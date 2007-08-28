// file.cpp

#include "stdafx.h"
#include "Error.h"

#include "File.h"
#include "InterpretProp.h"
#include "MAPIFunctions.h"
#include "FileDialogEx.h"
#include "MAPIProgress.h"
#include "guids.h"
#include "ImportProcs.h"
#include "MFCUtilityFunctions.h"

#include <shlobj.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

// Add current Entry ID to file name
HRESULT AppendEntryID(LPTSTR szFileName, size_t cchFileName, LPSBinary lpBin, size_t cchMaxAppend)
{
	HRESULT hRes = S_OK;
	LPTSTR szBin = NULL;

	if (!lpBin || !lpBin->cb || !szFileName || cchMaxAppend <= 1) return MAPI_E_INVALID_PARAMETER;

	MyHexFromBin(
		lpBin->lpb,
		lpBin->cb,
		&szBin);

	if (szBin)
	{
		EC_H(StringCchCatN(szFileName, cchFileName, _T("_"),1));// STRING_OK
		EC_H(StringCchCatN(szFileName, cchFileName, szBin,cchMaxAppend-1));
		delete[] szBin;
	}

	return hRes;
}

HRESULT GetDirectoryPath(LPTSTR szPath)
{
	BROWSEINFO BrowseInfo;
	LPITEMIDLIST lpItemIdList = NULL;
	HRESULT hRes = S_OK;

	if (!szPath) return MAPI_E_INVALID_PARAMETER;

    LPMALLOC lpMalloc = NULL;

    EC_H(SHGetMalloc(&lpMalloc));

	if (!lpMalloc) return hRes;

	memset(&BrowseInfo,NULL,sizeof(BROWSEINFO));

	szPath[0] = NULL;

	CString szInputString;
	szInputString.LoadString(IDS_DIRPROMPT);

	BrowseInfo.hwndOwner = NULL;
	BrowseInfo.lpszTitle = szInputString;
	BrowseInfo.pszDisplayName = szPath;
	BrowseInfo.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;

	//Note - I don't initialize COM for this call because MAPIInitialize does this
	lpItemIdList = SHBrowseForFolder(&BrowseInfo);
	if (lpItemIdList)
	{
		EC_B(SHGetPathFromIDList(lpItemIdList,szPath));
		lpMalloc->Free(lpItemIdList);
	}
	else
	{
		hRes = MAPI_E_USER_CANCEL;
	}

	lpMalloc->Release();
	return hRes;
}

//Creates an LPMESSAGE on top of the MSG file
HRESULT LoadMSGToMessage(LPCTSTR szMessageFile, LPMESSAGE* lppMessage)
{
	HRESULT		hRes = S_OK;
	LPSTORAGE	pStorage = NULL;
	LPMALLOC	lpMalloc = NULL;

	// get memory allocation function
	lpMalloc = MAPIGetDefaultMalloc();

	if (lpMalloc)
	{
		// Open the compound file
#ifdef _UNICODE
		EC_H(::StgOpenStorage(
			szMessageFile,
			NULL,
			STGM_READ | STGM_TRANSACTED,
			NULL,
			0,
			&pStorage));
#else
		{
			LPWSTR	szWideCharStr = NULL;
			EC_H(AnsiToUnicode(
				szMessageFile,
				&szWideCharStr));
			if (szWideCharStr)
			{
				EC_H(::StgOpenStorage(
					szWideCharStr,
					NULL,
					STGM_READ | STGM_TRANSACTED,
					NULL,
					0,
					&pStorage));
				delete[] szWideCharStr;
			}
		}
#endif

		if (pStorage)
		{
			// Open an IMessage interface on an IStorage object
			EC_H(OpenIMsgOnIStg(NULL,
				MAPIAllocateBuffer,
				MAPIAllocateMore,
				MAPIFreeBuffer,
				lpMalloc,
				NULL,
				pStorage,
				NULL,
				0,
				0,
				lppMessage));
		}
	}

	if (pStorage) pStorage->Release();
	return hRes;
}//LoadMSGToMessage

//Loads the MSG file into an LPMESSAGE pointer, then copies it into the passed in message
//lpMessage must be created first
HRESULT LoadFromMSG(LPCTSTR szMessageFile, LPMESSAGE lpMessage, HWND hWnd)
{
	HRESULT				hRes = S_OK;
	LPMESSAGE			pIMsg = NULL;
	SizedSPropTagArray	(18, excludeTags);

	// Specify properties to exclude in the copy operation. These are
	// the properties that Exchange excludes to save bits and time.
	// Should not be necessary to exclude these, but speeds the process
	// when a lot of messages are being copied.
	excludeTags.cValues = 18;
	excludeTags.aulPropTag[0] = PR_REPLICA_VERSION;
	excludeTags.aulPropTag[1] = PR_DISPLAY_BCC;
	excludeTags.aulPropTag[2] = PR_DISPLAY_CC;
	excludeTags.aulPropTag[3] = PR_DISPLAY_TO;
	excludeTags.aulPropTag[4] = PR_ENTRYID;
	excludeTags.aulPropTag[5] = PR_MESSAGE_SIZE;
	excludeTags.aulPropTag[6] = PR_PARENT_ENTRYID;
	excludeTags.aulPropTag[7] = PR_RECORD_KEY;
	excludeTags.aulPropTag[8] = PR_STORE_ENTRYID;
	excludeTags.aulPropTag[9] = PR_STORE_RECORD_KEY;
	excludeTags.aulPropTag[10] = PR_MDB_PROVIDER;
	excludeTags.aulPropTag[11] = PR_ACCESS;
	excludeTags.aulPropTag[12] = PR_HASATTACH;
	excludeTags.aulPropTag[13] = PR_OBJECT_TYPE;
	excludeTags.aulPropTag[14] = PR_ACCESS_LEVEL;
	excludeTags.aulPropTag[15] = PR_HAS_NAMED_PROPERTIES;
	excludeTags.aulPropTag[16] = PR_REPLICA_SERVER;
	excludeTags.aulPropTag[17] = PR_HAS_DAMS;

	EC_H(LoadMSGToMessage(szMessageFile,&pIMsg));

	if (pIMsg)
	{
		LPSPropProblemArray lpProblems = NULL;

		LPMAPIPROGRESS lpProgress = GetMAPIProgress(_T("IMAPIProp::CopyTo"), hWnd);// STRING_OK

		EC_H(pIMsg->CopyTo(
			0,
			NULL,
			(LPSPropTagArray)&excludeTags,
			lpProgress ? (ULONG_PTR)hWnd : NULL,
			lpProgress,
			&IID_IMessage,
			lpMessage,
			lpProgress ? MAPI_DIALOG : 0,
			&lpProblems));

		if(lpProgress)
			lpProgress->Release();

		lpProgress = NULL;

		EC_PROBLEMARRAY(lpProblems);
		MAPIFreeBuffer(lpProblems);
		if (!FAILED(hRes))
		{
			EC_H(lpMessage->SaveChanges(KEEP_OPEN_READWRITE));
		}
	}

	if (pIMsg) pIMsg->Release();
	return hRes;
}//LoadFromMSG

//lpMessage must be created first
HRESULT LoadFromTNEF(LPCTSTR szMessageFile, LPADRBOOK lpAdrBook, LPMESSAGE lpMessage)
{
	HRESULT				hRes = S_OK;
	LPSTREAM			lpStream = NULL;
	LPITNEF				lpTNEF = NULL;
	LPSTnefProblemArray	lpError = NULL;
	LPSTREAM			lpBodyStream = NULL;

	if (!szMessageFile | !lpAdrBook | !lpMessage) return MAPI_E_INVALID_PARAMETER;
	static WORD dwKey = (WORD)::GetTickCount();

	enum { ulNumTNEFExcludeProps = 1 };
	const static SizedSPropTagArray(
		ulNumTNEFExcludeProps, lpPropTnefExcludeArray ) =
	{
		ulNumTNEFExcludeProps,
			PR_URL_COMP_NAME
	};

	// Get a Stream interface on the input TNEF file
	EC_H(OpenStreamOnFile(
		MAPIAllocateBuffer,
		MAPIFreeBuffer,
		STGM_READ,
		(LPTSTR) szMessageFile,
		NULL,
		&lpStream));

	// get the key value for OpenTnefStreamEx function
	dwKey++;

#pragma warning(push)
#pragma warning(disable:4616)
#pragma warning(disable:6276)
	EC_H(OpenTnefStreamEx(
		NULL,
		lpStream,
		(LPTSTR) "winmail.dat",// STRING_OK - despite it's signature, this function is ANSI only
		TNEF_DECODE,
		lpMessage,
		dwKey,
		lpAdrBook,
		&lpTNEF));
#pragma warning(pop)

	if (lpTNEF)
	{
		// Decode the TNEF stream into our MAPI message.
		EC_H(lpTNEF->ExtractProps(
			TNEF_PROP_EXCLUDE,
			(LPSPropTagArray) &lpPropTnefExcludeArray,
			&lpError));

		EC_TNEFERR(lpError);
	}

	EC_H(lpMessage->SaveChanges(KEEP_OPEN_READWRITE));

	if (lpBodyStream) lpBodyStream->Release();
	MAPIFreeBuffer(lpError);
	if (lpTNEF) lpTNEF->Release();
	if (lpStream) lpStream->Release();
	return hRes;
}//LoadFromTNEF

//Builds a file name out of the passed in message and extension
HRESULT BuildFileName(LPTSTR szFileOut, size_t cchFileOut, LPCTSTR szExt, size_t cchExt, LPMESSAGE lpMessage)
{
	HRESULT			hRes = S_OK;
	LPSPropValue	lpSubject = NULL;

	if (!lpMessage || !szFileOut) return MAPI_E_INVALID_PARAMETER;

	// Get subject line of message
	// This will be used as the new file name.
	WC_H(HrGetOneProp(lpMessage, PR_SUBJECT, &lpSubject));
	if (MAPI_E_NOT_FOUND == hRes)
	{
		//This is OK. We'll use our own file name.
		hRes = S_OK;
	}
	else CHECKHRES(hRes);

	szFileOut[0] = NULL;
	if (CheckStringProp(lpSubject,PT_TSTRING))
	{
		EC_H(SanitizeFileName(
			szFileOut,
			cchFileOut,
			lpSubject->Value.LPSZ,
			cchFileOut-cchExt-1));
	}
	else
	{
		//We must have failed to get a subject before. Make one up.
		EC_H(StringCchCat(szFileOut, cchFileOut, _T("UnknownSubject")));// STRING_OK
	}

	// Add our extension
	EC_H(StringCchCat(szFileOut, cchFileOut, szExt));

	MAPIFreeBuffer(lpSubject);
	return hRes;
}

//Problem here is that cchFileOut can't be longer than MAX_PATH
//So the file name we generate must be shorter than MAX_PATH
//This includes our directory name too!
//So directory is part of the input and output now
#define MAXSUBJ 25
#define MAXBIN 141
HRESULT BuildFileNameAndPath(LPTSTR szFileOut, size_t cchFileOut, LPCTSTR szExt, size_t cchExt, LPCTSTR szSubj, LPSBinary lpBin, LPCTSTR szRootPath)
{
	HRESULT			hRes = S_OK;

	if (!szFileOut) return MAPI_E_INVALID_PARAMETER;
	if (cchFileOut > MAX_PATH) return MAPI_E_INVALID_PARAMETER;

	szFileOut[0] = _T('\0');//initialize our string to NULL
	size_t cchCharRemaining = cchFileOut;

	size_t cchRootPath = NULL;

	//set up the path portion of the output:
	if (szRootPath)
	{
		//Use the short path to give us as much room as possible
		EC_D(cchRootPath,GetShortPathName(szRootPath, szFileOut, (DWORD)cchFileOut));
		//stuff a slash in there if we need one
		if (cchRootPath+1 < cchFileOut && szFileOut[cchRootPath-1] != _T('\\'))
		{
			szFileOut[cchRootPath] = _T('\\');
			szFileOut[cchRootPath+1] = _T('\0');
			cchRootPath++;
		}
		cchCharRemaining -= cchRootPath;
	}

	//We now have cchCharRemaining characters in which to work
	//Suppose this is 0? Need at least 12 for an 8.3 name

	size_t cchBin = 0;
	if (lpBin) cchBin = (2*lpBin->cb)+1;//bin + '_'

	size_t cchSubj = 14;//length of 'UnknownSubject'
	if (szSubj)
	{
		EC_H(StringCchLength(szSubj,STRSAFE_MAX_CCH,&cchSubj));
	}

	if (cchCharRemaining < cchSubj + cchBin + cchExt + 1)
	{
		//don't have enough space - need to shorten things:
		if (cchSubj > MAXSUBJ) cchSubj = MAXSUBJ;
		if (cchBin  > MAXBIN)  cchBin  = MAXBIN;
	}
	if (cchCharRemaining < cchSubj + cchBin + cchExt + 1)
	{
		//still don't have enough space - need to shorten things:
		//TODO: generate a unique 8.3 name and return it
		return MAPI_E_INVALID_PARAMETER;
	}
	else
	{
		if (szSubj)
		{
			EC_H(SanitizeFileName(
				szFileOut + cchRootPath,
				cchCharRemaining,
				szSubj,
				cchSubj));
		}
		else
		{
			//We must have failed to get a subject before. Make one up.
			EC_H(StringCchCopy(szFileOut + cchCharRemaining, cchCharRemaining, _T("UnknownSubject")));// STRING_OK
		}

		if (lpBin && lpBin->cb)
		{
			EC_H(AppendEntryID(szFileOut,cchFileOut,lpBin,cchBin));
		}

		// Add our extension
		if (szExt && cchExt)
		{
			EC_H(StringCchCatN(szFileOut, cchFileOut, szExt,cchExt));
		}
	}

	return hRes;
}

//Takes szFileIn and copies it to szFileOut, replacing non file system characters with underscores
//Do NOT call with full path - just file names
//Resulting string will have no more than ulCharsToCopy characters
HRESULT SanitizeFileName(
						 LPTSTR szFileOut, //output buffer
						 size_t cchFileOut, //length of output buffer
						 LPCTSTR szFileIn,//File name in
						 size_t cchCharsToCopy)
{
	HRESULT hRes = S_OK;
	size_t cchFileNameLen = 0;
	size_t iCurChar = 0;

	EC_H(StringCchCopyN(szFileOut, cchFileOut, szFileIn, cchCharsToCopy));

	EC_H(StringCchLength(szFileOut,STRSAFE_MAX_CCH,&cchFileNameLen));

	for (iCurChar = 0;iCurChar < cchFileNameLen;iCurChar++)
	{
		switch (szFileOut[iCurChar])
		{
		case _T('^'):
		case _T('&'):
		case _T('*'):
		case _T('-'):
		case _T('+'):
		case _T('='):
		case _T('['):
		case _T(']'):
		case _T('\\'):
		case _T('|'):
		case _T(';'):
		case _T(':'):
		case _T('\"'):
		case _T(','):
		case _T('<'):
		case _T('>'):
		case _T('/'):
		case _T('?'):
			szFileOut[iCurChar] = _T('_');
			break;
		default:
			;
		}
	}

	return hRes;
}

HRESULT	SaveFolderContentsToMSG(LPMAPIFOLDER lpFolder, LPCTSTR szPathName, BOOL bAssoc, HWND hWnd)
{
	HRESULT			hRes = S_OK;
	LPMAPITABLE		lpFolderContents = NULL;
	LPMESSAGE		lpMessage = NULL;
	LPSRowSet		pRows = NULL;

	enum {fldPR_ENTRYID,
		fldPR_SUBJECT,
		fldPR_SEARCH_KEY,
		fldNUM_COLS};

	static SizedSPropTagArray(fldNUM_COLS,fldCols) = {fldNUM_COLS,
		PR_ENTRYID,
		PR_SUBJECT,
		PR_SEARCH_KEY
	};

	if (!lpFolder || !szPathName) return MAPI_E_INVALID_PARAMETER;

	DebugPrint(DBGGeneric,_T("SaveFolderContentsToMSG: Saving contents of 0x%08X to \"%s\"\n"),lpFolder,szPathName);

	EC_H(lpFolder->GetContentsTable(
		fMapiUnicode | (bAssoc?MAPI_ASSOCIATED:NULL),
		&lpFolderContents));

	if (lpFolderContents)
	{
		EC_H(lpFolderContents->SetColumns((LPSPropTagArray)&fldCols, TBL_BATCH));

		if (!FAILED(hRes)) for (;;)
		{
			hRes = S_OK;
			if (pRows) FreeProws(pRows);
			pRows = NULL;
			EC_H(lpFolderContents->QueryRows(
				1,
				NULL,
				&pRows));
			if (FAILED(hRes) || !pRows || (pRows && !pRows->cRows)) break;

			pRows->aRow->lpProps[fldPR_ENTRYID].ulPropTag;

			if (PT_ERROR != PROP_TYPE(pRows->aRow->lpProps[fldPR_ENTRYID].ulPropTag))
			{
				DebugPrint(DBGGeneric,_T("Source Message =\n"));
				DebugPrintBinary(DBGGeneric,&pRows->aRow->lpProps[fldPR_ENTRYID].Value.bin);

				if (lpMessage) lpMessage->Release();
				lpMessage = NULL;
				EC_H(CallOpenEntry(
					NULL,
					NULL,
					lpFolder,
					NULL,
					pRows->aRow->lpProps[fldPR_ENTRYID].Value.bin.cb,
					(LPENTRYID) pRows->aRow->lpProps[fldPR_ENTRYID].Value.bin.lpb,
					NULL,
					MAPI_BEST_ACCESS,
					NULL,
					(LPUNKNOWN*)&lpMessage));
				if (!lpMessage) continue;

				TCHAR szFileName[MAX_PATH];

				LPCTSTR szSubj = _T("UnknownSubject");// STRING_OK

				if (CheckStringProp(&pRows->aRow->lpProps[fldPR_SUBJECT],PT_TSTRING))
				{
					szSubj = pRows->aRow->lpProps[fldPR_SUBJECT].Value.LPSZ;
				}
				EC_H(BuildFileNameAndPath(szFileName,CCH(szFileName),_T(".msg"),4,szSubj,&pRows->aRow->lpProps[fldPR_SEARCH_KEY].Value.bin,szPathName));// STRING_OK

				DebugPrint(DBGGeneric,_T("Saving to = \"%s\"\n"),szFileName);

				EC_H(SaveToMSG(
					lpMessage,
					szFileName,
					hWnd));

				DebugPrint(DBGGeneric,_T("Message Saved\n"));
			}
		}
	}

	if (pRows) FreeProws(pRows);
	if (lpMessage) lpMessage->Release();
	if (lpFolderContents) lpFolderContents->Release();
	return hRes;
}

HRESULT SaveToEML(LPMESSAGE lpMessage, LPCTSTR szFileName)
{
	HRESULT hRes = S_OK;
	LPSTREAM		pStrmSrc = NULL;
	LPSTREAM		pStrmDest = NULL;
	STATSTG			StatInfo = {0};

	if (!lpMessage || !szFileName) return MAPI_E_INVALID_PARAMETER;
	DebugPrint(DBGGeneric,_T("SaveToEML: Saving 0x%X to \"%s\"\n"),lpMessage,szFileName);

	// Open the property of the attachment
	// containing the file data
	EC_H(lpMessage->OpenProperty(
		PR_INTERNET_CONTENT,
		(LPIID)&IID_IStream,
		0,
		NULL,//MAPI_MODIFY is not needed
		(LPUNKNOWN *)&pStrmSrc));
	if (FAILED(hRes))
	{
		if (MAPI_E_NOT_FOUND == hRes)
		{
			DebugPrint(DBGGeneric,_T("No internet content found\n"));
		}
	}
	else
	{
		if (pStrmSrc)
		{
			// Open an IStream interface and create the file at the
			// same time. This code will create the file in the
			// current directory.
			EC_H(OpenStreamOnFile(
				MAPIAllocateBuffer,
				MAPIFreeBuffer,
				STGM_CREATE | STGM_READWRITE,
				(LPTSTR) szFileName,
				NULL,
				&pStrmDest));

			if (pStrmDest)
			{
				pStrmSrc->Stat(&StatInfo, STATFLAG_NONAME);

				EC_H(pStrmSrc->CopyTo(pStrmDest,
					StatInfo.cbSize,
					NULL,
					NULL));

				// Commit changes to new stream
				EC_H(pStrmDest->Commit(STGC_DEFAULT));

				pStrmDest->Release();
			}
			pStrmSrc->Release();
		}
	}

	return hRes;
}//SaveToEML

HRESULT STDAPICALLTYPE MyStgCreateStorageEx(IN const TCHAR* pName,
							IN  DWORD grfMode,
							IN  DWORD stgfmt,
							IN  DWORD grfAttrs,
							IN  STGOPTIONS * pStgOptions,
							IN  void * reserved,
							IN  REFIID riid,
							OUT void ** ppObjectOpen)
{
	HRESULT hRes = S_OK;
	if (!pName) return MAPI_E_INVALID_PARAMETER;
#ifdef _UNICODE
	const WCHAR* pwcsName = pName;
#else
	WCHAR* pwcsName = NULL;
	// Convert new file name to WideChar
	EC_H(AnsiToUnicode(
		pName,
		&pwcsName));
	if (FAILED(hRes))
	{
		if (pwcsName) delete[] pwcsName;
		return hRes;
	}
#endif
	if (pfnStgCreateStorageEx) hRes = pfnStgCreateStorageEx(
		pwcsName,
		grfMode,
		stgfmt,
		grfAttrs,
		pStgOptions,
		reserved,
		riid,
		ppObjectOpen);
	// Fallback for NT4, which doesn't have StgCreateStorageEx
	else hRes = ::StgCreateDocfile(
		pwcsName,
		grfMode,
		0,
		(LPSTORAGE*) ppObjectOpen);
#ifndef _UNICODE
	delete[] pwcsName;
#endif
	return hRes;
}

HRESULT CreateNewMSG(LPCTSTR szFileName, LPMESSAGE* lppMessage, LPSTORAGE* lppStorage)
{
	if (!szFileName || !lppMessage || !lppStorage) return MAPI_E_INVALID_PARAMETER;

	HRESULT		hRes = S_OK;
	LPSTORAGE	pStorage = NULL;
	LPMESSAGE	pIMsg = NULL;
	LPMALLOC	pMalloc = NULL;

	// get memory allocation function
	pMalloc = MAPIGetDefaultMalloc();
	if (pMalloc)
	{
		STGOPTIONS myOpts = {0};

		myOpts.usVersion = 1,//STGOPTIONS_VERSION;
		myOpts.ulSectorSize = 4096;

		// Open the compound file
		EC_H(MyStgCreateStorageEx(
			szFileName,
			STGM_READWRITE | STGM_TRANSACTED | STGM_CREATE,
			STGFMT_DOCFILE,
			0, //FILE_FLAG_NO_BUFFERING,
			&myOpts,
			0,
			__uuidof(IStorage),
			(LPVOID*)&pStorage));
		if (SUCCEEDED(hRes) && pStorage)
		{
			// Open an IMessage interface on an IStorage object
			EC_H(OpenIMsgOnIStg(
				NULL,
				MAPIAllocateBuffer,
				MAPIAllocateMore,
				MAPIFreeBuffer,
				pMalloc,
				NULL,
				pStorage,
				NULL,
				0,
				0,
				&pIMsg));
			if (SUCCEEDED(hRes) && pIMsg)
			{
				// write the CLSID to the IStorage instance - pStorage. This will
				// only work with clients that support this compound document type
				// as the storage medium. If the client does not support
				// CLSID_MailMessage as the compound document, you will have to use
				// the CLSID that it does support.
				EC_H(WriteClassStg(pStorage, CLSID_MailMessage));
				if (SUCCEEDED(hRes))
				{
					*lppStorage = pStorage;
					(*lppStorage)->AddRef();
					*lppMessage = pIMsg;
					(*lppMessage)->AddRef();
				}
			}
			if (pIMsg) pIMsg->Release();
		}
		if (pStorage) pStorage->Release();
	}

	return hRes;
}

HRESULT SaveToMSG(LPMESSAGE lpMessage, LPCTSTR szFileName, HWND hWnd)
{
	HRESULT hRes = S_OK;
	LPSTORAGE pStorage = NULL;
	LPMESSAGE pIMsg = NULL;

	if (!lpMessage || !szFileName) return MAPI_E_INVALID_PARAMETER;

	DebugPrint(DBGGeneric,_T("SaveToMSG: Saving 0x%X to \"%s\"\n"),lpMessage,szFileName);

	EC_H(CreateNewMSG(szFileName,&pIMsg,&pStorage));
	if (pIMsg && pStorage)
	{
		// Specify properties to exclude in the copy operation. These are
		// the properties that Exchange excludes to save bits and time.
		// Should not be necessary to exclude these, but speeds the process
		// when a lot of messages are being copied.
		SizedSPropTagArray (7, excludeTags);
		excludeTags.cValues = 7;
		excludeTags.aulPropTag[0] = PR_ACCESS;
		excludeTags.aulPropTag[1] = PR_BODY;
		excludeTags.aulPropTag[2] = PR_RTF_SYNC_BODY_COUNT;
		excludeTags.aulPropTag[3] = PR_RTF_SYNC_BODY_CRC;
		excludeTags.aulPropTag[4] = PR_RTF_SYNC_BODY_TAG;
		excludeTags.aulPropTag[5] = PR_RTF_SYNC_PREFIX_COUNT;
		excludeTags.aulPropTag[6] = PR_RTF_SYNC_TRAILING_COUNT;

		LPSPropProblemArray lpProblems = NULL;

		// copy message properties to IMessage object opened on top of IStorage.
		LPMAPIPROGRESS lpProgress = GetMAPIProgress(_T("IMAPIProp::CopyTo"), hWnd);// STRING_OK

		EC_H(lpMessage->CopyTo(0, NULL,
			(LPSPropTagArray)&excludeTags,
			lpProgress ? (ULONG_PTR)hWnd : NULL,
			lpProgress,
			(LPIID)&IID_IMessage,
			pIMsg,
			lpProgress ? MAPI_DIALOG : 0,
			&lpProblems));

		if(lpProgress)
			lpProgress->Release();

		lpProgress = NULL;

		EC_PROBLEMARRAY(lpProblems);
		MAPIFreeBuffer(lpProblems);

		// save changes to IMessage object.
		EC_H(pIMsg->SaveChanges(KEEP_OPEN_READWRITE));

		// save changes in storage of new doc file
		EC_H(pStorage->Commit(STGC_DEFAULT));
	}

	if (pStorage) pStorage->Release();
	if (pIMsg) pIMsg->Release();

	return hRes;
}//SaveToMSG

HRESULT SaveToTNEF(LPMESSAGE lpMessage, LPADRBOOK lpAdrBook, LPCTSTR szFileName)
{
	HRESULT hRes = S_OK;

	enum { ulNumTNEFIncludeProps = 2 };
	const static SizedSPropTagArray(
		ulNumTNEFIncludeProps, lpPropTnefIncludeArray ) =
	{
		ulNumTNEFIncludeProps,
			PR_MESSAGE_RECIPIENTS,
			PR_ATTACH_DATA_BIN
	};

	enum { ulNumTNEFExcludeProps = 1 };
	const static SizedSPropTagArray(
		ulNumTNEFExcludeProps, lpPropTnefExcludeArray ) =
	{
		ulNumTNEFExcludeProps,
			PR_URL_COMP_NAME
	};

	if (!lpMessage || !lpAdrBook || !szFileName) return MAPI_E_INVALID_PARAMETER;
	DebugPrint(DBGGeneric,_T("SaveToTNEF: Saving 0x%X to \"%s\"\n"),lpMessage,szFileName);

	LPSTREAM			lpStream	=	NULL;
	LPITNEF				lpTNEF		=	NULL;
	LPSTnefProblemArray	lpError		=	NULL;

	static WORD dwKey = (WORD)::GetTickCount();

	// Get a Stream interface on the input TNEF file
	EC_H(OpenStreamOnFile(
		MAPIAllocateBuffer,
		MAPIFreeBuffer,
		STGM_READWRITE | STGM_CREATE,
		(LPTSTR) szFileName,
		NULL,
		&lpStream));

	if (lpStream)
	{
		//Open TNEF stream
#pragma warning(push)
#pragma warning(disable:4616)
#pragma warning(disable:6276)
		EC_H(OpenTnefStreamEx(
			NULL,
			lpStream,
			(LPTSTR) "winmail.dat",// STRING_OK - despite it's signature, this function is ANSI only
			TNEF_ENCODE,
			lpMessage,
			dwKey,
			lpAdrBook,
			&lpTNEF));
#pragma warning(pop)

		if (lpTNEF)
		{
			//Excludes
			EC_H(lpTNEF->AddProps(
				TNEF_PROP_EXCLUDE,
				0,
				NULL,
				(LPSPropTagArray) &lpPropTnefExcludeArray
				));
			EC_H(lpTNEF->AddProps(
				TNEF_PROP_EXCLUDE | TNEF_PROP_ATTACHMENTS_ONLY,
				0,
				NULL,
				(LPSPropTagArray) &lpPropTnefExcludeArray
				));

			EC_H(lpTNEF->AddProps(
				TNEF_PROP_INCLUDE,
				0,
				NULL,
				(LPSPropTagArray) &lpPropTnefIncludeArray
				));

			EC_H(lpTNEF->Finish(0, &dwKey, &lpError));

			EC_TNEFERR(lpError);

			//Saving stream
			EC_H(lpStream->Commit(STGC_DEFAULT));

			MAPIFreeBuffer(lpError);
			lpTNEF->Release();
		}
		lpStream->Release();
	}

	return hRes;
}//SaveToTNEF

HRESULT DeleteAttachments(LPMESSAGE lpMessage, LPCTSTR szAttName, HWND hWnd)
{
	LPSPropValue	pProps = NULL;
	HRESULT			hRes = S_OK;
	LPMAPITABLE		lpAttTbl = NULL;
	LPSRowSet		pRows = NULL;
	ULONG			iRow;

	enum {ATTACHNUM,ATTACHNAME,NUM_COLS};
	static SizedSPropTagArray(NUM_COLS,sptAttachTableCols) = {NUM_COLS,
		PR_ATTACH_NUM, PR_ATTACH_LONG_FILENAME};

	if (!lpMessage) return MAPI_E_INVALID_PARAMETER;

	EC_H(HrGetOneProp(
		lpMessage,
		PR_HASATTACH,
		&pProps));

	if (pProps && PR_HASATTACH == pProps[0].ulPropTag && pProps[0].Value.b)
	{
		EC_H(lpMessage->OpenProperty(
			PR_MESSAGE_ATTACHMENTS,
			&IID_IMAPITable,
			0,
			0,
			(LPUNKNOWN *) &lpAttTbl));

		if (lpAttTbl)
		{
			// I would love to just pass a restriction
			// to HrQueryAllRows for the file name.  However,
			// we don't support restricting attachment tables (EMSMDB32 that is)
			// So I have to compare the strings myself (see below)
			EC_H(HrQueryAllRows(lpAttTbl,
				(LPSPropTagArray) &sptAttachTableCols,
				NULL,
				NULL,
				0,
				&pRows));

			if (pRows)
			{
				BOOL bDirty = FALSE;

				if (!FAILED(hRes)) for (iRow = 0; iRow < pRows -> cRows; iRow++)
				{
					hRes = S_OK;

					if (PR_ATTACH_NUM != pRows->aRow[iRow].lpProps[ATTACHNUM].ulPropTag) continue;

					if (szAttName)
					{
						if (PR_ATTACH_LONG_FILENAME != pRows->aRow[iRow].lpProps[ATTACHNAME].ulPropTag ||
							lstrcmpi(szAttName, pRows->aRow[iRow].lpProps[ATTACHNAME].Value.LPSZ) != 0)
							continue;
					}

					// Open the attachment
					LPMAPIPROGRESS lpProgress = GetMAPIProgress(_T("IMessage::DeleteAttach"), hWnd);// STRING_OK

					EC_H(lpMessage->DeleteAttach(
						pRows->aRow[iRow].lpProps[ATTACHNUM].Value.l,
						lpProgress ? (ULONG_PTR)hWnd : NULL,
						lpProgress,
						lpProgress ? ATTACH_DIALOG : 0));

					if(SUCCEEDED(hRes))
						bDirty = TRUE;

					if(lpProgress)
						lpProgress->Release();

					lpProgress = NULL;
				}

				// Moved this inside the if (pRows) check
				// and also added a flag so we only call this if we
				// got a successful DeleteAttach call
				if (bDirty)
					EC_H(lpMessage->SaveChanges(KEEP_OPEN_READWRITE));
			}
		}
	}

	if (pRows) FreeProws(pRows);
	if (lpAttTbl) lpAttTbl -> Release();
	MAPIFreeBuffer(pProps);

	return hRes;
}//DeleteAllAttachments

HRESULT WriteAttachmentsToFile(LPMESSAGE lpMessage, HWND hWnd)
{
	LPSPropValue	pProps = NULL;
	HRESULT			hRes = S_OK;
	LPMAPITABLE		lpAttTbl = NULL;
	LPSRowSet		pRows = NULL;
	ULONG			iRow;
	LPATTACH		lpAttach = NULL;

	enum {ATTACHNUM,NUM_COLS};
	static SizedSPropTagArray(NUM_COLS,sptAttachTableCols) = {NUM_COLS,
		PR_ATTACH_NUM};

	if (!lpMessage) return MAPI_E_INVALID_PARAMETER;

	EC_H(HrGetOneProp(
		lpMessage,
		PR_HASATTACH,
		&pProps));

	if (pProps && PR_HASATTACH == pProps[0].ulPropTag && pProps[0].Value.b)
	{
		EC_H(lpMessage->OpenProperty(
			PR_MESSAGE_ATTACHMENTS,
			&IID_IMAPITable,
			0,
			0,
			(LPUNKNOWN *) &lpAttTbl));

		if (lpAttTbl)
		{
			EC_H(HrQueryAllRows(lpAttTbl,
				(LPSPropTagArray) &sptAttachTableCols,
				NULL,
				NULL,
				0,
				&pRows));

			if (pRows)
			{
				if (!FAILED(hRes)) for (iRow = 0; iRow < pRows -> cRows; iRow++)
				{
					lpAttach = NULL;

					if (PR_ATTACH_NUM != pRows->aRow[iRow].lpProps[ATTACHNUM].ulPropTag) continue;

					// Open the attachment
					EC_H(lpMessage->OpenAttach (
						pRows->aRow[iRow].lpProps[ATTACHNUM].Value.l,
						NULL,
						MAPI_BEST_ACCESS,
						&lpAttach));

					if (lpAttach)
					{
						WC_H(WriteAttachmentToFile(lpAttach, hWnd));
						lpAttach->Release();
						lpAttach = NULL;
						if (S_OK != hRes && iRow != pRows->cRows-1)
						{
							if (bShouldCancel(NULL,hRes)) break;
							hRes = S_OK;
						}
					}
				}
				if (pRows) FreeProws(pRows);
			}
			lpAttTbl -> Release();
		}
	}

	MAPIFreeBuffer(pProps);

	return hRes;
}//WriteAttachmentsToFile

HRESULT WriteEmbeddedMSGToFile(LPATTACH lpAttach,LPCTSTR szFileName, HWND hWnd)
{
	HRESULT			hRes = S_OK;
	LPMESSAGE		lpAttachMsg = NULL;

	if (!lpAttach || !szFileName) return MAPI_E_INVALID_PARAMETER;

	DebugPrint(DBGGeneric,_T("WriteEmbeddedMSGToFile: Saving 0x%X to \"%s\"\n"),lpAttach,szFileName);

	EC_H(lpAttach->OpenProperty(
		PR_ATTACH_DATA_OBJ,
		(LPIID)&IID_IMessage,
		0,
		NULL,//MAPI_MODIFY is not needed
		(LPUNKNOWN *)&lpAttachMsg));

	if (lpAttachMsg)
	{
		EC_H(SaveToMSG(lpAttachMsg,szFileName, hWnd));
		lpAttachMsg->Release();
	}

	return hRes;
}

HRESULT WriteAttachStreamToFile(LPATTACH lpAttach,LPCTSTR szFileName)
{
	HRESULT			hRes = S_OK;
	LPSTREAM		pStrmSrc = NULL;
	LPSTREAM		pStrmDest = NULL;
	STATSTG			StatInfo = {0};

	if (!lpAttach || !szFileName) return MAPI_E_INVALID_PARAMETER;

	// Open the property of the attachment
	// containing the file data
	WC_H(lpAttach->OpenProperty(
		PR_ATTACH_DATA_BIN,
		(LPIID)&IID_IStream,
		0,
		NULL,//MAPI_MODIFY is not needed
		(LPUNKNOWN *)&pStrmSrc));
	if (FAILED(hRes))
	{
		if (MAPI_E_NOT_FOUND == hRes)
		{
			DebugPrint(DBGGeneric,_T("No attachments found. Maybe the attachment was a message?\n"));
		}
		else CHECKHRES(hRes);
	}
	else
	{
		if (pStrmSrc)
		{
			// Open an IStream interface and create the file at the
			// same time. This code will create the file in the
			// current directory.
			EC_H(OpenStreamOnFile(
				MAPIAllocateBuffer,
				MAPIFreeBuffer,
				STGM_CREATE | STGM_READWRITE,
				(LPTSTR) szFileName,
				NULL,
				&pStrmDest));

			if (pStrmDest)
			{
				pStrmSrc->Stat(&StatInfo, STATFLAG_NONAME);

				EC_H(pStrmSrc->CopyTo(pStrmDest,
					StatInfo.cbSize,
					NULL,
					NULL));

				// Commit changes to new stream
				EC_H(pStrmDest->Commit(STGC_DEFAULT));

				pStrmDest->Release();
			}
			pStrmSrc->Release();
		}
	}

	return hRes;
}

//Pretty sure this covers all OLE attachments - we don't need to look at PR_ATTACH_TAG
HRESULT WriteOleToFile(LPATTACH lpAttach,LPCTSTR szFileName)
{
	HRESULT			hRes = S_OK;
	LPSTORAGE		lpStorageSrc = NULL;
	LPSTORAGE		lpStorageDest = NULL;
	LPSTREAM		pStrmSrc = NULL;
	LPSTREAM		pStrmDest = NULL;
	STATSTG			StatInfo = {0};

	// Open the property of the attachment containing the OLE data
	// Try to get it as an IStreamDocFile file first as that will be faster
	WC_H(lpAttach->OpenProperty(
		PR_ATTACH_DATA_OBJ,
		(LPIID)&IID_IStreamDocfile,
		0,
		NULL,
		(LPUNKNOWN *)&pStrmSrc));

	//We got IStreamDocFile! Great! We can copy stream to stream into the file
	if (pStrmSrc)
	{
		// Open an IStream interface and create the file at the
		// same time. This code will create the file in the
		// current directory.
		EC_H(OpenStreamOnFile(
			MAPIAllocateBuffer,
			MAPIFreeBuffer,
			STGM_CREATE | STGM_READWRITE,
			(LPTSTR) szFileName,
			NULL,
			&pStrmDest));

		if (pStrmDest)
		{
			EC_H(pStrmSrc->Stat(&StatInfo, STATFLAG_NONAME));

			EC_H(pStrmSrc->CopyTo(pStrmDest,
				StatInfo.cbSize,
				NULL,
				NULL));

			// Commit changes to new stream
			EC_H(pStrmDest->Commit(STGC_DEFAULT));
			pStrmDest->Release();
		}
		pStrmSrc->Release();
	}
	//We couldn't get IStreamDocFile! No problem - we'll try IStorage next
	else
	{
		hRes = S_OK;
		EC_H(lpAttach->OpenProperty(
			PR_ATTACH_DATA_OBJ,
			(LPIID)&IID_IStorage,
			0,
			NULL,
			(LPUNKNOWN *)&lpStorageSrc));

		if (lpStorageSrc)
		{

#ifdef _UNICODE
			EC_H(::StgCreateDocfile(
				szFileName,
				STGM_READWRITE | STGM_CREATE,
				0,
				&lpStorageDest));
#else
			// Convert new file name to WideChar
			LPWSTR	lpWideCharStr = NULL;
			EC_H(AnsiToUnicode(
				szFileName,
				&lpWideCharStr));
			if (lpWideCharStr)
			{
				EC_H(::StgCreateDocfile(
					lpWideCharStr,
					STGM_READWRITE | STGM_TRANSACTED | STGM_CREATE,
					0,
					&lpStorageDest));
				delete lpWideCharStr;
			}
#endif
			if (lpStorageDest)
			{
				EC_H(lpStorageSrc->CopyTo(
					NULL,
					NULL,
					NULL,
					lpStorageDest));

				EC_H(lpStorageDest->Commit(STGC_DEFAULT));
				lpStorageDest->Release();
			}

			lpStorageSrc->Release();
		}
	}

	return hRes;
}


HRESULT	WriteAttachmentToFile(LPATTACH lpAttach, HWND hWnd)
{
	HRESULT			hRes = S_OK;
	LPSPropValue	lpProps = NULL;
	ULONG			ulProps = 0;
	TCHAR			szFileName[MAX_PATH];
	INT_PTR			iDlgRet = 0;

	enum {ATTACH_METHOD,ATTACH_LONG_FILENAME,ATTACH_FILENAME,DISPLAY_NAME,NUM_COLS};
	SizedSPropTagArray(NUM_COLS,sptaAttachProps) = { NUM_COLS, {
		PR_ATTACH_METHOD,
			PR_ATTACH_LONG_FILENAME,
			PR_ATTACH_FILENAME,
			PR_DISPLAY_NAME}
	};

	if (!lpAttach) return MAPI_E_INVALID_PARAMETER;

	DebugPrint(DBGGeneric,_T("WriteAttachmentToFile: Saving 0x%X.\n"),lpAttach);

	// Get required properties from the message
	EC_H_GETPROPS(lpAttach->GetProps(
		(LPSPropTagArray) &sptaAttachProps,//property tag array
		fMapiUnicode,//flags
		&ulProps, //Count of values returned
		&lpProps));//Values returned

	if (lpProps)
	{
		LPCTSTR szName = _T("Unknown");// STRING_OK

		//Get a file name to use
		if (CheckStringProp(&lpProps[ATTACH_LONG_FILENAME],PT_TSTRING))
		{
			szName = lpProps[ATTACH_LONG_FILENAME].Value.LPSZ;
		}
		else if (CheckStringProp(&lpProps[ATTACH_FILENAME],PT_TSTRING))
		{
			szName = lpProps[ATTACH_FILENAME].Value.LPSZ;
		}
		else if (CheckStringProp(&lpProps[DISPLAY_NAME],PT_TSTRING))
		{
			szName = lpProps[DISPLAY_NAME].Value.LPSZ;
		}

		EC_H(SanitizeFileName(szFileName,CCH(szFileName),szName,CCH(szFileName)));

		//Get File Name
		switch(lpProps[ATTACH_METHOD].Value.l)
		{
		case ATTACH_BY_VALUE:
		case ATTACH_BY_REFERENCE:
		case ATTACH_BY_REF_RESOLVE:
		case ATTACH_BY_REF_ONLY:
			{
				CString szFileSpec;
				szFileSpec.LoadString(IDS_ALLFILES);

				CFileDialogEx dlgFilePicker(
					FALSE,
					_T("txt"),// STRING_OK
					_T("unknown.txt"),// STRING_OK
					OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
					szFileSpec);

				dlgFilePicker.m_ofn.lpstrFile = szFileName;

				DebugPrint(DBGGeneric,_T("WriteAttachmentToFile: Prompting with \"%s\"\n"),dlgFilePicker.m_ofn.lpstrFile);

				EC_D_DIALOG(dlgFilePicker.DoModal());
				if (iDlgRet == IDOK)
				{
					EC_H(WriteAttachStreamToFile(lpAttach,dlgFilePicker.m_ofn.lpstrFile));
				}
			}
			break;
		case ATTACH_EMBEDDED_MSG:
			//Get File Name
			{
				CString szFileSpec;
				szFileSpec.LoadString(IDS_MSGFILES);

				CFileDialogEx dlgFilePicker(
					FALSE,
					_T("msg"),// STRING_OK
					_T("test.msg"),// STRING_OK
					OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
					szFileSpec);

				dlgFilePicker.m_ofn.lpstrFile = szFileName;

				DebugPrint(DBGGeneric,_T("WriteAttachmentToFile: Prompting with \"%s\"\n"),dlgFilePicker.m_ofn.lpstrFile);

				EC_D_DIALOG(dlgFilePicker.DoModal());
				if (iDlgRet == IDOK)
				{
					EC_H(WriteEmbeddedMSGToFile(lpAttach,dlgFilePicker.m_ofn.lpstrFile, hWnd));
				}
			}
			break;
		case ATTACH_OLE:
			{
				CString szFileSpec;
				szFileSpec.LoadString(IDS_ALLFILES);

				CFileDialogEx dlgFilePicker(
					FALSE,
					NULL,
					NULL,
					OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
					szFileSpec);

				dlgFilePicker.m_ofn.lpstrFile = szFileName;

				DebugPrint(DBGGeneric,_T("WriteAttachmentToFile: Prompting with \"%s\"\n"),dlgFilePicker.m_ofn.lpstrFile);
				EC_D_DIALOG(dlgFilePicker.DoModal());
				if (iDlgRet == IDOK)
				{
					EC_H(WriteOleToFile(lpAttach,dlgFilePicker.m_ofn.lpstrFile));
				}
			}
			break;
		default:
				ErrDialog(__FILE__,__LINE__,IDS_EDUNKNOWNATTMETHOD); break;
		}
	}
	if (iDlgRet == IDCANCEL) hRes = MAPI_E_USER_CANCEL;

	MAPIFreeBuffer(lpProps);
	return hRes;
}//WriteAttachmentToFile