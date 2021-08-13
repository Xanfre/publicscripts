/******************************************************************************
 *  scrversion.cpp
 *
 *  This file is part of Public Scripts
 *  Copyright (C) 2005-2011 Tom N Harris <telliamed@whoopdedo.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *****************************************************************************/
#define _SHOCKINTERFACES 1
#include "scrversion.h"
#include "ScriptModule.h"

#include <lg/objstd.h>
#include <lg/interface.h>
#include <lg/types.h>
#include <lg/defs.h>
#include <lg/scrmsgs.h>
#include <lg/scrservices.h>
#include <lg/objects.h>
#include <lg/properties.h>
#include <lg/links.h>

#include "ScriptLib.h"

#include <new>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <memory>
#include <malloc.h>

#include <windows.h>
#include <winver.h>

#if (__cplusplus >= 201103L)
template <typename T>
using SPtr = std::unique_ptr<T>;
#else
#define SPtr std::auto_ptr
#endif


static bool CheckFileVersion(const char* pszFile, ulong dwVersHigh, ulong dwVersLow);
static void DoSuccess(int iObjId);
static void DoFailure(int iObjId);
static char* GetObjectParamsCompatible(int iObjId);
static void RelayCompatible(const char* pszMsg, int iObjId);
static int ShowBookCompatible(int iObjId, unsigned long ulTime);


const char* cScriptModule::sm_ScriptModuleName = "version";
const sScrClassDesc cScriptModule::sm_ScriptsArray[] = {
	{ sm_ScriptModuleName, "VersionCheck", "CustomScript", cScr_VersionCheck::MakeVersionCheck },
};
const unsigned int cScriptModule::sm_ScriptsArraySize = 1;


IScript* cScr_VersionCheck::MakeVersionCheck(const char* pszName, int iHostObjId)
{
	cScr_VersionCheck* pscrRet = new(std::nothrow) cScr_VersionCheck(pszName, iHostObjId);
	return static_cast<IScript*>(pscrRet);
}

long __stdcall cScr_VersionCheck::ReceiveMessage(sScrMsg* pMsg, sMultiParm*, eScrTraceAction)
{
	try
	{
		if (!::_stricmp(pMsg->message, "Sim"))
		{
			if (!static_cast<sSimMsg*>(pMsg)->fStarting)
			{
				// not sure if this is really necessary, but just to be safe...
				if (m_iTextType == 2)
				{
					// TDP has, rather annoyingly, all the Shock interfaces.
					// So we use a member variable. It's non-persistent though.
					// Let's just pretend the user isn't so demented as to
					// save the game in this state.
					SService<IShockGameSrv> pSGS(g_pScriptManager);
					pSGS->OverlayChange(41,0);
				}
				else if (m_iTextType == 1)
				{
					SService<IDarkUISrv> pUI(g_pScriptManager);
					pUI->TextMessage("", 0, 1);
				}
				return 0;
			}

			SPtr<char> pszParams(GetObjectParamsCompatible(ObjId()));
			char* pszScript;
			char* pszToken = pszParams.get();
			for (pszScript = strsep(&pszToken, ";"); pszScript; pszScript = strsep(&pszToken, ";"))
			{
				if (!*pszScript)
					continue;
				ulong scrVersHigh = 0, scrVersLow = 0;
				char* pszVers = ::strchr(pszScript, '=');
				if (pszVers)
				{
					char* pt = pszVers + 1;
					scrVersHigh = ::strtoul(pt, &pt, 10) << 16;
					if (pt && *pt == '.')
					{
						scrVersHigh |= ::strtoul(pt+1, &pt, 10);
						if (pt && *pt == '.')
						{
							scrVersLow = ::strtoul(pt+1, &pt, 10) << 16;
							if (pt && *pt == '.')
								scrVersLow |= ::strtoul(pt+1, &pt, 10);
						}
					}
					*pszVers = '\0';
				}

				// FindFileInPath will fail if the script path length exceeds MAX_PATH.
				// Let's assume that will not happen under reasonable circumstances.
				SService<IEngineSrv> pVers(g_pScriptManager);
				bool bScrFound = false;
				cScrStr scrPath;
				if (!::strchr(pszScript, '.'))
				{
					char pszScriptWithExt[::strlen(pszScript)+5];
					::strcpy(pszScriptWithExt, pszScript);
					::strcat(pszScriptWithExt, ".osm");
					bScrFound = pVers->FindFileInPath("script_module_path", pszScriptWithExt, scrPath);
				}
				else
				{
					bScrFound = pVers->FindFileInPath("script_module_path", pszScript, scrPath);
				}
				if(!bScrFound)
				{
					DoFailure(ObjId());
					return 0;
				}
				if (scrVersHigh == 0 && scrVersLow == 0)
				{
					continue;
				}
				if (!CheckFileVersion(scrPath, scrVersHigh, scrVersLow))
				{
					DoFailure(ObjId());
					return 0;
				}
			}
			DoSuccess(ObjId());
		} // "Sim"
		else if (!::_stricmp(pMsg->message, "Timer"))
		{
			if (!::_stricmp(static_cast<sScrTimerMsg*>(pMsg)->name, "ErrorText"))
			{
				m_iTextType = ShowBookCompatible(ObjId(), 0x7FFFFFFFUL);
				return 0;
			}
		}
	}
	catch (...)
	{
	}
	return 0;
}

bool CheckFileVersion(const char* pszFile, ulong dwVersHigh, ulong dwVersLow)
{
	DWORD z;
	unsigned int len = ::GetFileVersionInfoSizeA(const_cast<LPSTR>(pszFile), &z);
	if (len)
	{
		char* buffer = reinterpret_cast<char*>(::alloca(len));
		VS_FIXEDFILEINFO* pFileVers;
		::GetFileVersionInfoA(const_cast<LPSTR>(pszFile), z, len, reinterpret_cast<void*>(buffer));
		len = 0;
		::VerQueryValueA(reinterpret_cast<void*>(buffer), "\\", reinterpret_cast<void**>(&pFileVers), &len);
		if (len > 0)
		{
			if ( (pFileVers->dwFileVersionMS > dwVersHigh)
			  || (pFileVers->dwFileVersionMS == dwVersHigh
			   && pFileVers->dwFileVersionLS >= dwVersLow)
			)
				return true;
		}
	}
	return false;
}

void DoSuccess(int iObjId)
{
	RelayCompatible("TurnOn", iObjId);
	SInterface<IObjectSystem> pOS(g_pScriptManager);
	pOS->Destroy(iObjId);
}

void DoFailure(int iObjId)
{
	RelayCompatible("TurnOff", iObjId);
	//m_iTextType = ShowBookCompatible(iObjId, 0x7FFFFFFFUL);
	g_pScriptManager->SetTimedMessage2(iObjId, "ErrorText", 288, kSTM_OneShot, 0);
	//SInterface<IObjectSystem> pOS(g_pScriptManager);
	//pOS->Destroy(iObjId);
}

/* IPropertySrv isn't the same in all game versions,
 * nor is the property name always the same.
 * So let's try to even out those differences, even
 * if it is more awkward.
 */
char* GetObjectParamsCompatible(int iObjId)
{
	SInterface<IPropertyManager> pPM(g_pScriptManager);
	SInterface<IStringProperty> pProp = static_cast<IStringProperty*>(pPM->GetPropertyNamed("DesignNote"));
	if (-1 == pProp->GetID())
	{
		pProp.reset(static_cast<IStringProperty*>(pPM->GetPropertyNamed("ObjList")));
		if (-1 == pProp->GetID())
			return NULL;
	}

	if (!pProp->IsRelevant(iObjId))
	{
		return NULL;
	}

	char* pRet = NULL;
	const char* pszValue;
	pProp->Get(iObjId, &pszValue);
	if (pszValue)
	{
		pRet = new(std::nothrow) char[::strlen(pszValue)+1];
		if (pRet)
			::strcpy(pRet, pszValue);
	}
	return pRet;
}

void RelayCompatible(const char* pszMsg, int iObjId)
{
	SInterface<ILinkManager> pLM(g_pScriptManager);
	SInterface<IRelation> pRel = pLM->GetRelationNamed("ControlDevice");
	if (! pRel->GetID())
	{
		pRel.reset(pLM->GetRelationNamed("SwitchLink"));
		if (! pRel->GetID())
			return;
	}

	SInterface<ILinkQuery> pLQ = pRel->Query(iObjId, 0);
	if (!pLQ)
		return;

	for (; ! pLQ->Done(); pLQ->Next())
	{
		sLink sl;
		pLQ->Link(&sl);
#if (_DARKGAME == 1)
		g_pScriptManager->PostMessage2(iObjId, sl.dest, pszMsg, 0, 0, 0);
#else
		g_pScriptManager->PostMessage2(iObjId, sl.dest, pszMsg, 0, 0, 0, 0);
#endif
	}
}

int ShowBookCompatible(int iObjId, unsigned long ulTime)
{
	SInterface<IPropertyManager> pPM(g_pScriptManager);
	SInterface<IStringProperty> pBookProp = static_cast<IStringProperty*>(pPM->GetPropertyNamed("Book"));
	if (-1 == pBookProp->GetID())
	{
		// Must be SShock2
#if (_DARKGAME == 3)
		pBookProp.reset(static_cast<IStringProperty*>(pPM->GetPropertyNamed("UseMsg")));
		if (-1 != pBookProp->GetID()
		 && pBookProp->IsRelevant(iObjId))
		{
			const char* pszBook;
			pBookProp->Get(iObjId, &pszBook);
			SService<IShockGameSrv> pSGS(g_pScriptManager);
			pSGS->TlucTextAdd(pszBook, "error", -1);
			//pSGS->AddTranslatableText(pszBook, "error", 0, ulTime);
			return 2;
		}
#endif
		return 0;
	}

	if (pBookProp->IsRelevant(iObjId))
	{
		const char* pszBook;
		pBookProp->Get(iObjId, &pszBook);

		SInterface<IStringProperty> pArtProp = static_cast<IStringProperty*>(pPM->GetPropertyNamed("BookArt"));
		if (-1 != pArtProp->GetID()
		 && pArtProp->IsRelevant(iObjId))
		{
			const char* pszBookArt;
			pArtProp->Get(iObjId, &pszBookArt);
			SService<IDarkUISrv> pUI(g_pScriptManager);
			pUI->ReadBook(pszBook, pszBookArt);
			return 0;
		}

		SService<IDataSrv> pDS(g_pScriptManager);
		char* szBookFile = reinterpret_cast<char*>(::alloca(10 + ::strlen(pszBook)));
		::strcpy(szBookFile, "..\\books\\");
		::strcat(szBookFile, pszBook);
		cScrStr strText;
		pDS->GetString(strText, szBookFile, "page_0", "", "strings");
		if (!strText.IsEmpty())
		{
			SService<IDarkUISrv> pUI(g_pScriptManager);
			pUI->TextMessage(strText, 0, ulTime);
		}
		strText.Free();
		return 1;
	}
	return 0;
}
