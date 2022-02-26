
/*
 * file system folder
 *
 * Copyright 1997             Marcus Meissner
 * Copyright 1998, 1999, 2002 Juergen Schmied
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <precomp.h>

WINE_DEFAULT_DEBUG_CHANNEL (shell);

HKEY OpenKeyFromFileType(PCUIDLIST_RELATIVE pidl, LPCWSTR KeyName)
{
    HKEY hkey;

    if (!_ILIsValue(pidl))
    {
        ERR("Invalid pidl!\n");
        return NULL;
    }

    FileStructW* pDataW = _ILGetFileStructW(pidl);
    if (!pDataW)
    {
        ERR("Invalid pidl!\n");
        return NULL;
    }

    LPWSTR pExtension = PathFindExtensionW(pDataW->wszName);
    if (!pExtension || *pExtension == NULL)
    {
        WARN("No extension for %S!\n", pDataW->wszName);
        return NULL;
    }

    WCHAR FullName[MAX_PATH];
    DWORD dwSize = sizeof(FullName);
    wsprintf(FullName, L"%s\\%s", pExtension, KeyName);

    LONG res = RegOpenKeyExW(HKEY_CLASSES_ROOT, FullName, 0, KEY_READ, &hkey);
    if (!res)
        return hkey;

    res = RegGetValueW(HKEY_CLASSES_ROOT, pExtension, NULL, RRF_RT_REG_SZ, NULL, FullName, &dwSize);
    if (res)
    {
        WARN("Failed to get progid for file %S, extension %S (%x), address %x, pidl: %x, error %d\n", pDataW->wszName, pExtension, pExtension, &dwSize, pidl, res);
        return NULL;
    }

    wcscat(FullName, L"\\");
    wcscat(FullName, KeyName);

    hkey = NULL;
    res = RegOpenKeyExW(HKEY_CLASSES_ROOT, FullName, 0, KEY_READ, &hkey);
    if (res)
        WARN("Could not open key %S for extension %S\n", KeyName, pExtension);

    return hkey;
}

HRESULT GetCLSIDForFileType(PCUIDLIST_RELATIVE pidl, LPCWSTR KeyName, CLSID* pclsid)
{
    HKEY hkeyProgId = OpenKeyFromFileType(pidl, KeyName);
    if (!hkeyProgId)
    {
        WARN("OpenKeyFromFileType failed for key %S\n", KeyName);
        return S_FALSE;
    }

    WCHAR wszCLSIDValue[CHARS_IN_GUID];
    DWORD dwSize = sizeof(wszCLSIDValue);
    LONG res = RegGetValueW(hkeyProgId, NULL, NULL, RRF_RT_REG_SZ, NULL, wszCLSIDValue, &dwSize);
    RegCloseKey(hkeyProgId);
    if (res)
    {
        ERR("OpenKeyFromFileType succeeded but RegGetValueW failed\n");
        return S_FALSE;
    }

#if 0
    {
        res = RegGetValueW(HKEY_LOCAL_MACHINE,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
                           wszCLSIDValue,
                           RRF_RT_REG_SZ,
                           NULL,
                           NULL,
                           NULL);
        if (res != ERROR_SUCCESS)
        {
            ERR("DropHandler extension %S not approved\n", wszName);
            return E_ACCESSDENIED;
        }
    }
#endif

    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Blocked",
                     wszCLSIDValue,
                     RRF_RT_REG_SZ,
                     NULL,
                     NULL,
                     NULL) == ERROR_SUCCESS)
    {
        ERR("Extension %S  not approved\n", wszCLSIDValue);
        return E_ACCESSDENIED;
    }

    HRESULT hres = CLSIDFromString (wszCLSIDValue, pclsid);
    if (FAILED_UNEXPECTEDLY(hres))
        return hres;

    return S_OK;
}

static HRESULT getIconLocationForFolder(IShellFolder * psf, LPCITEMIDLIST pidl, UINT uFlags,
                                        LPWSTR szIconFile, UINT cchMax, int *piIndex, UINT *pwFlags)
{
    static const WCHAR shellClassInfo[] = { '.', 'S', 'h', 'e', 'l', 'l', 'C', 'l', 'a', 's', 's', 'I', 'n', 'f', 'o', 0 };
    static const WCHAR iconFile[] = { 'I', 'c', 'o', 'n', 'F', 'i', 'l', 'e', 0 };
    static const WCHAR clsid[] = { 'C', 'L', 'S', 'I', 'D', 0 };
    static const WCHAR clsid2[] = { 'C', 'L', 'S', 'I', 'D', '2', 0 };
    static const WCHAR iconIndex[] = { 'I', 'c', 'o', 'n', 'I', 'n', 'd', 'e', 'x', 0 };
    static const WCHAR wszDesktopIni[] = { 'd','e','s','k','t','o','p','.','i','n','i',0 };
    int icon_idx;

    if (!(uFlags & GIL_DEFAULTICON) && (_ILGetFileAttributes(ILFindLastID(pidl), NULL, 0) & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) != 0 )
    {
        WCHAR wszFolderPath[MAX_PATH];

        if (!ILGetDisplayNameExW(psf, pidl, wszFolderPath, 0))
            return FALSE;

        PathAppendW(wszFolderPath, wszDesktopIni);

        if (PathFileExistsW(wszFolderPath))
        {
            WCHAR wszPath[MAX_PATH];
            WCHAR wszCLSIDValue[CHARS_IN_GUID];

            if (GetPrivateProfileStringW(shellClassInfo, iconFile, NULL, wszPath, MAX_PATH, wszFolderPath))
            {
                ExpandEnvironmentStringsW(wszPath, szIconFile, cchMax);

                *piIndex = GetPrivateProfileIntW(shellClassInfo, iconIndex, 0, wszFolderPath);
                return S_OK;
            }
            else if (GetPrivateProfileStringW(shellClassInfo, clsid, NULL, wszCLSIDValue, CHARS_IN_GUID, wszFolderPath) &&
                HCR_GetIconW(wszCLSIDValue, szIconFile, NULL, cchMax, &icon_idx))
            {
                *piIndex = icon_idx;
                return S_OK;
            }
            else if (GetPrivateProfileStringW(shellClassInfo, clsid2, NULL, wszCLSIDValue, CHARS_IN_GUID, wszFolderPath) &&
                HCR_GetIconW(wszCLSIDValue, szIconFile, NULL, cchMax, &icon_idx))
            {
                *piIndex = icon_idx;
                return S_OK;
            }
        }
    }

    static const WCHAR folder[] = { 'F', 'o', 'l', 'd', 'e', 'r', 0 };

    if (!HCR_GetIconW(folder, szIconFile, NULL, cchMax, &icon_idx))
    {
        lstrcpynW(szIconFile, swShell32Name, cchMax);
        icon_idx = -IDI_SHELL_FOLDER;
    }

    if (uFlags & GIL_OPENICON)
        *piIndex = icon_idx < 0 ? icon_idx - 1 : icon_idx + 1;
    else
        *piIndex = icon_idx;

    return S_OK;
}

HRESULT CFSExtractIcon_CreateInstance(IShellFolder * psf, LPCITEMIDLIST pidl, REFIID iid, LPVOID * ppvOut)
{
    CComPtr<IDefaultExtractIconInit> initIcon;
    HRESULT hr;
    int icon_idx = 0;
    UINT flags = 0; // FIXME: Use it!
    WCHAR wTemp[MAX_PATH] = L"";

    hr = SHCreateDefaultExtractIcon(IID_PPV_ARG(IDefaultExtractIconInit,&initIcon));
    if (FAILED(hr))
        return hr;

    if (_ILIsFolder (pidl))
    {
        if (SUCCEEDED(getIconLocationForFolder(psf, 
                          pidl, 0, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetNormalIcon(wTemp, icon_idx);
            // FIXME: if/when getIconLocationForFolder does something for 
            //        GIL_FORSHORTCUT, code below should be uncommented. and
            //        the following line removed.
            initIcon->SetShortcutIcon(wTemp, icon_idx);
        }
        if (SUCCEEDED(getIconLocationForFolder(psf, 
                          pidl, GIL_DEFAULTICON, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetDefaultIcon(wTemp, icon_idx);
        }
        // if (SUCCEEDED(getIconLocationForFolder(psf, 
        //                   pidl, GIL_FORSHORTCUT, wTemp, _countof(wTemp),
        //                   &icon_idx,
        //                   &flags)))
        // {
        //     initIcon->SetShortcutIcon(wTemp, icon_idx);
        // }
        if (SUCCEEDED(getIconLocationForFolder(psf, 
                          pidl, GIL_OPENICON, wTemp, _countof(wTemp),
                          &icon_idx,
                          &flags)))
        {
            initIcon->SetOpenIcon(wTemp, icon_idx);
        }
    }
    else
    {
        HKEY hkey = OpenKeyFromFileType(pidl, L"DefaultIcon");
        if (!hkey)
            WARN("Could not open DefaultIcon key!\n");

        DWORD dwSize = sizeof(wTemp);
        if (hkey && !SHQueryValueExW(hkey, NULL, NULL, NULL, wTemp, &dwSize))
        {
            WCHAR sNum[5];
            if (ParseFieldW (wTemp, 2, sNum, 5))
                icon_idx = _wtoi(sNum);
            else
                icon_idx = 0; /* sometimes the icon number is missing */
            ParseFieldW (wTemp, 1, wTemp, MAX_PATH);
            PathUnquoteSpacesW(wTemp);

            if (!wcscmp(L"%1", wTemp)) /* icon is in the file */
            {
                ILGetDisplayNameExW(psf, pidl, wTemp, 0);
                icon_idx = 0;
            }

            initIcon->SetNormalIcon(wTemp, icon_idx);
        }
        else
        {
            initIcon->SetNormalIcon(swShell32Name, 0);
        }

        if (hkey)
            RegCloseKey(hkey);
    }

    return initIcon->QueryInterface(iid, ppvOut);
}

/*
CFileSysEnum should do an initial FindFirstFile and do a FindNextFile as each file is
returned by Next. When the enumerator is created, it can do numerous additional operations
including formatting a drive, reconnecting a network share drive, and requesting a disk
be inserted in a removable drive.
*/

/***********************************************************************
*   IShellFolder implementation
*/

class CFileSysEnum :
    public CEnumIDListBase
{
    private:
    public:
        CFileSysEnum();
        ~CFileSysEnum();
        HRESULT WINAPI Initialize(LPWSTR sPathTarget, DWORD dwFlags);

        BEGIN_COM_MAP(CFileSysEnum)
        COM_INTERFACE_ENTRY_IID(IID_IEnumIDList, IEnumIDList)
        END_COM_MAP()
};

CFileSysEnum::CFileSysEnum()
{
}

CFileSysEnum::~CFileSysEnum()
{
}

HRESULT WINAPI CFileSysEnum::Initialize(LPWSTR lpszPath, DWORD dwFlags)
{
    WIN32_FIND_DATAW stffile;
    HANDLE hFile;
    WCHAR  szPath[MAX_PATH];
    BOOL succeeded = TRUE;
    static const WCHAR stars[] = { '*','.','*',0 };
    static const WCHAR dot[] = { '.',0 };
    static const WCHAR dotdot[] = { '.','.',0 };

    TRACE("(%p)->(path=%s flags=0x%08x)\n", this, debugstr_w(lpszPath), dwFlags);

    if(!lpszPath || !lpszPath[0]) return FALSE;

    wcscpy(szPath, lpszPath);
    PathAddBackslashW(szPath);
    wcscat(szPath,stars);

    hFile = FindFirstFileW(szPath,&stffile);
    if ( hFile != INVALID_HANDLE_VALUE )
    {
        BOOL findFinished = FALSE;

        do
        {
            if ( !(stffile.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
             || (dwFlags & SHCONTF_INCLUDEHIDDEN) )
            {
                LPITEMIDLIST pidl = NULL;

                if ( (stffile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                 dwFlags & SHCONTF_FOLDERS &&
                 strcmpW(stffile.cFileName, dot) && strcmpW(stffile.cFileName, dotdot))
                {
                    pidl = _ILCreateFromFindDataW(&stffile);
                    succeeded = succeeded && AddToEnumList(pidl);
                }
                else if (!(stffile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                 && dwFlags & SHCONTF_NONFOLDERS)
                {
                    pidl = _ILCreateFromFindDataW(&stffile);
                    succeeded = succeeded && AddToEnumList(pidl);
                }
            }
            if (succeeded)
            {
                if (!FindNextFileW(hFile, &stffile))
                {
                    if (GetLastError() == ERROR_NO_MORE_FILES)
                        findFinished = TRUE;
                    else
                        succeeded = FALSE;
                }
            }
        } while (succeeded && !findFinished);
        FindClose(hFile);
    }

    return succeeded;
}

CFSFolder::CFSFolder()
{
    pclsid = (CLSID *)&CLSID_ShellFSFolder;
    sPathTarget = NULL;
    pidlRoot = NULL;
    m_bGroupPolicyActive = 0;
}

CFSFolder::~CFSFolder()
{
    TRACE("-- destroying IShellFolder(%p)\n", this);

    SHFree(pidlRoot);
    SHFree(sPathTarget);
}


static const shvheader GenericSFHeader[] = {
    {IDS_SHV_COLUMN_NAME, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 15},
    {IDS_SHV_COLUMN_COMMENTS, SHCOLSTATE_TYPE_STR, LVCFMT_LEFT, 0},
    {IDS_SHV_COLUMN_TYPE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 10},
    {IDS_SHV_COLUMN_SIZE, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_RIGHT, 10},
    {IDS_SHV_COLUMN_MODIFIED, SHCOLSTATE_TYPE_DATE | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 12},
    {IDS_SHV_COLUMN_ATTRIBUTES, SHCOLSTATE_TYPE_STR | SHCOLSTATE_ONBYDEFAULT, LVCFMT_LEFT, 10}
};

#define GENERICSHELLVIEWCOLUMNS 6

/**************************************************************************
 *  SHELL32_CreatePidlFromBindCtx  [internal]
 *
 *  If the caller bound File System Bind Data, assume it is the
 *   find data for the path.
 *  This allows binding of paths that don't exist.
 */
LPITEMIDLIST SHELL32_CreatePidlFromBindCtx(IBindCtx *pbc, LPCWSTR path)
{
    IFileSystemBindData *fsbd = NULL;
    LPITEMIDLIST pidl = NULL;
    IUnknown *param = NULL;
    WIN32_FIND_DATAW wfd;
    HRESULT r;

    TRACE("%p %s\n", pbc, debugstr_w(path));

    if (!pbc)
        return NULL;

    /* see if the caller bound File System Bind Data */
    r = pbc->GetObjectParam((LPOLESTR)STR_FILE_SYS_BIND_DATA, &param);
    if (FAILED(r))
        return NULL;

    r = param->QueryInterface(IID_PPV_ARG(IFileSystemBindData,&fsbd));
    if (SUCCEEDED(r))
    {
        r = fsbd->GetFindData(&wfd);
        if (SUCCEEDED(r))
        {
            lstrcpynW(&wfd.cFileName[0], path, MAX_PATH);
            pidl = _ILCreateFromFindDataW(&wfd);
        }
        fsbd->Release();
    }

    return pidl;
}

void SHELL32_GetCLSIDForDirectory(LPCWSTR pwszDir, CLSID* pclsidFolder)
{
    WCHAR wszCLSIDValue[CHARS_IN_GUID];
    WCHAR wszDesktopIni[MAX_PATH];
    StringCchCopyW(wszDesktopIni, MAX_PATH, pwszDir);
    StringCchCatW(wszDesktopIni, MAX_PATH, L"\\desktop.ini");

    if (GetPrivateProfileStringW(L".ShellClassInfo", 
                                 L"CLSID", 
                                 L"",
                                 wszCLSIDValue, 
                                 CHARS_IN_GUID, 
                                 wszDesktopIni))
    {
        CLSIDFromString (wszCLSIDValue, pclsidFolder);
    }
}

HRESULT SHELL32_GetFSItemAttributes(IShellFolder * psf, LPCITEMIDLIST pidl, LPDWORD pdwAttributes)
{
    DWORD dwFileAttributes, dwShellAttributes;

    if (!_ILIsFolder(pidl) && !_ILIsValue(pidl))
    {
        ERR("Got wrong type of pidl!\n");
        *pdwAttributes &= SFGAO_CANLINK;
        return S_OK;
    }

    dwFileAttributes = _ILGetFileAttributes(pidl, NULL, 0);

    /* Set common attributes */
    dwShellAttributes = SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANLINK | SFGAO_CANRENAME | SFGAO_CANDELETE |
                        SFGAO_HASPROPSHEET | SFGAO_DROPTARGET | SFGAO_FILESYSTEM;

    if (dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        dwShellAttributes |= (SFGAO_FOLDER | SFGAO_HASSUBFOLDER | SFGAO_FILESYSANCESTOR | SFGAO_STORAGEANCESTOR | SFGAO_STORAGE);
    else
        dwShellAttributes |= SFGAO_STREAM;

    if (dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
        dwShellAttributes |=  SFGAO_HIDDEN;

    if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        dwShellAttributes |=  SFGAO_READONLY;

    if (SFGAO_LINK & *pdwAttributes)
    {
        char ext[MAX_PATH];

        if (_ILGetExtension(pidl, ext, MAX_PATH) && !lstrcmpiA(ext, "lnk"))
            dwShellAttributes |= SFGAO_LINK;
    }

    if (SFGAO_HASSUBFOLDER & *pdwAttributes)
    {
        CComPtr<IShellFolder> psf2;
        if (SUCCEEDED(psf->BindToObject(pidl, 0, IID_PPV_ARG(IShellFolder, &psf2))))
        {
            CComPtr<IEnumIDList> pEnumIL;
            if (SUCCEEDED(psf2->EnumObjects(0, SHCONTF_FOLDERS, &pEnumIL)))
            {
                if (pEnumIL->Skip(1) == S_OK)
                    dwShellAttributes |= SFGAO_HASSUBFOLDER;
            }
        }
    }

    *pdwAttributes = dwShellAttributes;

    TRACE ("-- 0x%08x\n", *pdwAttributes);
    return S_OK;
}

/**************************************************************************
* CFSFolder::ParseDisplayName {SHELL32}
*
* Parse a display name.
*
* PARAMS
*  hwndOwner       [in]  Parent window for any message's
*  pbc             [in]  optional FileSystemBindData context
*  lpszDisplayName [in]  Unicode displayname.
*  pchEaten        [out] (unicode) characters processed
*  ppidl           [out] complex pidl to item
*  pdwAttributes   [out] items attributes
*
* NOTES
*  Every folder tries to parse only its own (the leftmost) pidl and creates a
*  subfolder to evaluate the remaining parts.
*  Now we can parse into namespaces implemented by shell extensions
*
*  Behaviour on win98: lpszDisplayName=NULL -> crash
*                      lpszDisplayName="" -> returns mycoputer-pidl
*
* FIXME
*    pdwAttributes is not set
*    pchEaten is not set like in windows
*/
HRESULT WINAPI CFSFolder::ParseDisplayName(HWND hwndOwner,
        LPBC pbc,
        LPOLESTR lpszDisplayName,
        DWORD *pchEaten, PIDLIST_RELATIVE *ppidl,
        DWORD *pdwAttributes)
{
    HRESULT hr = E_INVALIDARG;
    LPCWSTR szNext = NULL;
    WCHAR szElement[MAX_PATH];
    WCHAR szPath[MAX_PATH];
    LPITEMIDLIST pidlTemp = NULL;
    DWORD len;

    TRACE ("(%p)->(HWND=%p,%p,%p=%s,%p,pidl=%p,%p)\n",
           this, hwndOwner, pbc, lpszDisplayName, debugstr_w (lpszDisplayName),
           pchEaten, ppidl, pdwAttributes);

    if (!ppidl)
        return E_INVALIDARG;

    if (!lpszDisplayName)
    {
        *ppidl = NULL;
        return E_INVALIDARG;
    }

    *ppidl = NULL;

    if (pchEaten)
        *pchEaten = 0; /* strange but like the original */

    if (*lpszDisplayName)
    {
        /* get the next element */
        szNext = GetNextElementW (lpszDisplayName, szElement, MAX_PATH);

        pidlTemp = SHELL32_CreatePidlFromBindCtx(pbc, szElement);
        if (pidlTemp != NULL)
        {
            /* We are creating an id list without ensuring that the items exist.
               If we have a remaining path, this must be a folder. 
               We have to do it now because it is set as a file by default */
            if (szNext)
            {
                pidlTemp->mkid.abID[0] = PT_FOLDER;
            }
            hr = S_OK;
        }
        else
        {
            /* build the full pathname to the element */
            lstrcpynW(szPath, sPathTarget, MAX_PATH - 1);
            PathAddBackslashW(szPath);
            len = wcslen(szPath);
            lstrcpynW(szPath + len, szElement, MAX_PATH - len);

            /* get the pidl */
            hr = _ILCreateFromPathW(szPath, &pidlTemp);
        }

        if (SUCCEEDED(hr))
        {
            if (szNext && *szNext)
            {
                /* try to analyse the next element */
                hr = SHELL32_ParseNextElement(this, hwndOwner, pbc,
                                              &pidlTemp, (LPOLESTR) szNext, pchEaten, pdwAttributes);
            }
            else
            {
                /* it's the last element */
                if (pdwAttributes && *pdwAttributes)
                    hr = SHELL32_GetFSItemAttributes(this, pidlTemp, pdwAttributes);
            }
        }
    }

    if (SUCCEEDED(hr))
        *ppidl = pidlTemp;
    else
        *ppidl = NULL;

    TRACE("(%p)->(-- pidl=%p ret=0x%08x)\n", this, ppidl ? *ppidl : 0, hr);

    return hr;
}

/**************************************************************************
* CFSFolder::EnumObjects
* PARAMETERS
*  HWND          hwndOwner,    //[in ] Parent Window
*  DWORD         grfFlags,     //[in ] SHCONTF enumeration mask
*  LPENUMIDLIST* ppenumIDList  //[out] IEnumIDList interface
*/
HRESULT WINAPI CFSFolder::EnumObjects(
    HWND hwndOwner,
    DWORD dwFlags,
    LPENUMIDLIST *ppEnumIDList)
{
    return ShellObjectCreatorInit<CFileSysEnum>(sPathTarget, dwFlags, IID_PPV_ARG(IEnumIDList, ppEnumIDList));
}

/**************************************************************************
* CFSFolder::BindToObject
* PARAMETERS
*  LPCITEMIDLIST pidl,       //[in ] relative pidl to open
*  LPBC          pbc,        //[in ] optional FileSystemBindData context
*  REFIID        riid,       //[in ] Initial Interface
*  LPVOID*       ppvObject   //[out] Interface*
*/
HRESULT WINAPI CFSFolder::BindToObject(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbc,
    REFIID riid,
    LPVOID * ppvOut)
{
    TRACE("(%p)->(pidl=%p,%p,%s,%p)\n", this, pidl, pbc,
          shdebugstr_guid(&riid), ppvOut);

    CComPtr<IShellFolder> pSF;
    HRESULT hr;

    if (!pidlRoot || !ppvOut || !pidl || !pidl->mkid.cb)
    {
        ERR("CFSFolder::BindToObject: Invalid parameters\n");
        return E_INVALIDARG;
    }

    /* Get the pidl data */
    FileStruct* pData = &_ILGetDataPointer(pidl)->u.file;
    FileStructW* pDataW = _ILGetFileStructW(pidl);

    if (!pDataW)
    {
        ERR("CFSFolder::BindToObject: Invalid pidl!\n");
        return E_INVALIDARG;
    }

    *ppvOut = NULL;

    /* Create the target folder info */
    PERSIST_FOLDER_TARGET_INFO pfti = {0};
    pfti.dwAttributes = -1;
    pfti.csidl = -1;
    PathCombineW(pfti.szTargetParsingName, sPathTarget, pDataW->wszName);

    /* Get the CLSID to bind to */
    CLSID clsidFolder;
    if (_ILIsFolder(pidl))
    {
        clsidFolder = CLSID_ShellFSFolder;

        if ((pData->uFileAttribs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY)) != 0)
            SHELL32_GetCLSIDForDirectory(pfti.szTargetParsingName, &clsidFolder);
    }
    else
    {
        hr = GetCLSIDForFileType(pidl, L"CLSID", &clsidFolder);
        if (hr == S_FALSE)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (hr != S_OK)
            return hr;
    }

    hr = SHELL32_BindToSF(pidlRoot, &pfti, pidl, &clsidFolder, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    TRACE ("-- returning (%p) %08x\n", *ppvOut, hr);

    return S_OK;

}

/**************************************************************************
*  CFSFolder::BindToStorage
* PARAMETERS
*  LPCITEMIDLIST pidl,       //[in ] complex pidl to store
*  LPBC          pbc,        //[in ] reserved
*  REFIID        riid,       //[in ] Initial storage interface
*  LPVOID*       ppvObject   //[out] Interface* returned
*/
HRESULT WINAPI CFSFolder::BindToStorage(
    PCUIDLIST_RELATIVE pidl,
    LPBC pbcReserved,
    REFIID riid,
    LPVOID *ppvOut)
{
    FIXME("(%p)->(pidl=%p,%p,%s,%p) stub\n", this, pidl, pbcReserved,
          shdebugstr_guid (&riid), ppvOut);

    *ppvOut = NULL;
    return E_NOTIMPL;
}

/**************************************************************************
*  CFSFolder::CompareIDs
*/

HRESULT WINAPI CFSFolder::CompareIDs(LPARAM lParam,
                                     PCUIDLIST_RELATIVE pidl1,
                                     PCUIDLIST_RELATIVE pidl2)
{
    LPPIDLDATA pData1 = _ILGetDataPointer(pidl1);
    LPPIDLDATA pData2 = _ILGetDataPointer(pidl2);
    FileStructW* pDataW1 = _ILGetFileStructW(pidl1);
    FileStructW* pDataW2 = _ILGetFileStructW(pidl2);
    BOOL bIsFolder1 = _ILIsFolder(pidl1);
    BOOL bIsFolder2 = _ILIsFolder(pidl2);
    LPWSTR pExtension1, pExtension2;

    if (!pDataW1 || !pDataW2 || LOWORD(lParam) >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    /* When sorting between a File and a Folder, the Folder gets sorted first */
    if (bIsFolder1 != bIsFolder2)
    {
        return MAKE_COMPARE_HRESULT(bIsFolder1 ? -1 : 1);
    }

    int result;
    switch (LOWORD(lParam))
    {
        case 0: /* Name */
            result = wcsicmp(pDataW1->wszName, pDataW2->wszName);
            break;
        case 1: /* Comments */
            result = 0;
            break;
        case 2: /* Type */
            pExtension1 = PathFindExtensionW(pDataW1->wszName);
            pExtension2 = PathFindExtensionW(pDataW2->wszName);
            result = wcsicmp(pExtension1, pExtension2); 
            break;
        case 3: /* Size */
            result = pData1->u.file.dwFileSize - pData2->u.file.dwFileSize;
            break;
        case 4: /* Modified */
            result = pData1->u.file.uFileDate - pData2->u.file.uFileDate;
            if (result == 0)
                result = pData1->u.file.uFileTime - pData2->u.file.uFileTime;
            break;
        case 5: /* Attributes */
            return SHELL32_CompareDetails(this, lParam, pidl1, pidl2);
    }

    if (result == 0)
        return SHELL32_CompareChildren(this, lParam, pidl1, pidl2);

    return MAKE_COMPARE_HRESULT(result);
}

/**************************************************************************
* CFSFolder::CreateViewObject
*/
HRESULT WINAPI CFSFolder::CreateViewObject(HWND hwndOwner,
        REFIID riid, LPVOID * ppvOut)
{
    CComPtr<IShellView> pShellView;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(hwnd=%p,%s,%p)\n", this, hwndOwner, shdebugstr_guid (&riid),
           ppvOut);

    if (ppvOut)
    {
        *ppvOut = NULL;

        if (IsEqualIID (riid, IID_IDropTarget))
            hr = CFSDropTarget_CreateInstance(sPathTarget, riid, ppvOut);
        else if (IsEqualIID (riid, IID_IContextMenu))
        {
            HKEY hKeys[16];
            UINT cKeys = 0;
            AddClassKeyToArray(L"Directory\\Background", hKeys, &cKeys);

            DEFCONTEXTMENU dcm;
            dcm.hwnd = hwndOwner;
            dcm.pcmcb = this;
            dcm.pidlFolder = pidlRoot;
            dcm.psf = this;
            dcm.cidl = 0;
            dcm.apidl = NULL;
            dcm.cKeys = cKeys;
            dcm.aKeys = hKeys;
            dcm.punkAssociationInfo = NULL;
            hr = SHCreateDefaultContextMenu (&dcm, riid, ppvOut);
        }
        else if (IsEqualIID (riid, IID_IShellView))
        {
            SFV_CREATE sfvparams = {sizeof(SFV_CREATE), this};
            hr = SHCreateShellFolderView(&sfvparams, (IShellView**)ppvOut);
        }
    }
    TRACE("-- (%p)->(interface=%p)\n", this, ppvOut);
    return hr;
}

/**************************************************************************
*  CFSFolder::GetAttributesOf
*
* PARAMETERS
*  UINT            cidl,     //[in ] num elements in pidl array
*  LPCITEMIDLIST*  apidl,    //[in ] simple pidl array
*  ULONG*          rgfInOut) //[out] result array
*
*/
HRESULT WINAPI CFSFolder::GetAttributesOf(UINT cidl,
        PCUITEMID_CHILD_ARRAY apidl, DWORD * rgfInOut)
{
    HRESULT hr = S_OK;

    if (!rgfInOut)
        return E_INVALIDARG;
    if (cidl && !apidl)
        return E_INVALIDARG;

    if (*rgfInOut == 0)
        *rgfInOut = ~0;

    if(cidl == 0)
    {
        LPCITEMIDLIST rpidl = ILFindLastID(pidlRoot);

        if (_ILIsFolder(rpidl) || _ILIsValue(rpidl))
        {
            SHELL32_GetFSItemAttributes(this, rpidl, rgfInOut);
        }
        else if (_ILIsDrive(rpidl))
        {
            IShellFolder *psfParent = NULL;
            hr = SHBindToParent(pidlRoot, IID_PPV_ARG(IShellFolder, &psfParent), NULL);
            if(SUCCEEDED(hr))
            {
                hr = psfParent->GetAttributesOf(1, &rpidl, (SFGAOF*)rgfInOut);
                psfParent->Release();
            }
        }
        else
        {
            ERR("Got and unknown pidl!\n");
        }
    }
    else
    {
        while (cidl > 0 && *apidl)
        {
            pdump(*apidl);
            if(_ILIsFolder(*apidl) || _ILIsValue(*apidl))
                SHELL32_GetFSItemAttributes(this, *apidl, rgfInOut);
            else
                ERR("Got an unknown type of pidl!!!\n");
            apidl++;
            cidl--;
        }
    }
    /* make sure SFGAO_VALIDATE is cleared, some apps depend on that */
    *rgfInOut &= ~SFGAO_VALIDATE;

    TRACE("-- result=0x%08x\n", *rgfInOut);

    return hr;
}

/**************************************************************************
*  CFSFolder::GetUIObjectOf
*
* PARAMETERS
*  HWND           hwndOwner, //[in ] Parent window for any output
*  UINT           cidl,      //[in ] array size
*  LPCITEMIDLIST* apidl,     //[in ] simple pidl array
*  REFIID         riid,      //[in ] Requested Interface
*  UINT*          prgfInOut, //[   ] reserved
*  LPVOID*        ppvObject) //[out] Resulting Interface
*
* NOTES
*  This function gets asked to return "view objects" for one or more (multiple
*  select) items:
*  The viewobject typically is an COM object with one of the following
*  interfaces:
*  IExtractIcon,IDataObject,IContextMenu
*  In order to support icon positions in the default Listview your DataObject
*  must implement the SetData method (in addition to GetData :) - the shell
*  passes a barely documented "Icon positions" structure to SetData when the
*  drag starts, and GetData's it if the drop is in another explorer window that
*  needs the positions.
*/
HRESULT WINAPI CFSFolder::GetUIObjectOf(HWND hwndOwner,
                                        UINT cidl, PCUITEMID_CHILD_ARRAY apidl,
                                        REFIID riid, UINT * prgfInOut,
                                        LPVOID * ppvOut)
{
    LPVOID pObj = NULL;
    HRESULT hr = E_INVALIDARG;

    TRACE ("(%p)->(%p,%u,apidl=%p,%s,%p,%p)\n",
           this, hwndOwner, cidl, apidl, shdebugstr_guid (&riid), prgfInOut, ppvOut);

    if (ppvOut)
    {
        *ppvOut = NULL;

        if (cidl == 1 && _ILIsValue(apidl[0]))
        {
            hr = _CreateExtensionUIObject(apidl[0], riid, ppvOut);
            if(hr != S_FALSE)
                return hr;
        }

        if (IsEqualIID(riid, IID_IContextMenu) && (cidl >= 1))
        {
            HKEY hKeys[16];
            UINT cKeys = 0;
            AddFSClassKeysToArray(apidl[0], hKeys, &cKeys);

            DEFCONTEXTMENU dcm;
            dcm.hwnd = hwndOwner;
            dcm.pcmcb = this;
            dcm.pidlFolder = pidlRoot;
            dcm.psf = this;
            dcm.cidl = cidl;
            dcm.apidl = apidl;
            dcm.cKeys = cKeys;
            dcm.aKeys = hKeys;
            dcm.punkAssociationInfo = NULL;
            hr = SHCreateDefaultContextMenu (&dcm, riid, &pObj);
        }
        else if (IsEqualIID (riid, IID_IDataObject))
        {
            if (cidl >= 1) 
            {
                hr = IDataObject_Constructor (hwndOwner, pidlRoot, apidl, cidl, (IDataObject **)&pObj);
            }
            else
            {
                hr = E_INVALIDARG;
            }
        }
        else if ((IsEqualIID (riid, IID_IExtractIconA) || IsEqualIID (riid, IID_IExtractIconW)) && (cidl == 1))
        {
            if (_ILIsValue(apidl[0]))
                hr = _GetIconHandler(apidl[0], riid, (LPVOID*)&pObj);
            if (hr != S_OK)
                hr = CFSExtractIcon_CreateInstance(this, apidl[0], riid, &pObj);
        }
        else if (IsEqualIID (riid, IID_IDropTarget))
        {
            /* only interested in attempting to bind to shell folders, not files (except exe), so if we fail, rebind to root */
            if (cidl != 1 || FAILED(hr = this->_GetDropTarget(apidl[0], (LPVOID*) &pObj)))
            {
                hr = CFSDropTarget_CreateInstance(sPathTarget, riid, (LPVOID*) &pObj);
            }
        }
        else
            hr = E_NOINTERFACE;

        if (SUCCEEDED(hr) && !pObj)
            hr = E_OUTOFMEMORY;

        *ppvOut = pObj;
    }
    TRACE("(%p)->hr=0x%08x\n", this, hr);
    return hr;
}

static const WCHAR AdvancedW[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
static const WCHAR HideFileExtW[] = L"HideFileExt";
static const WCHAR NeverShowExtW[] = L"NeverShowExt";

/******************************************************************************
 * SHELL_FS_HideExtension [Internal]
 *
 * Query the registry if the filename extension of a given path should be
 * hidden.
 *
 * PARAMS
 *  szPath [I] Relative or absolute path of a file
 *
 * RETURNS
 *  TRUE, if the filename's extension should be hidden
 *  FALSE, otherwise.
 */
BOOL SHELL_FS_HideExtension(LPWSTR szPath)
{
    HKEY hKey;
    DWORD dwData;
    DWORD dwDataSize = sizeof (DWORD);
    BOOL doHide = FALSE; /* The default value is FALSE (win98 at least) */

    if (!RegCreateKeyExW(HKEY_CURRENT_USER, AdvancedW, 0, 0, 0, KEY_ALL_ACCESS, 0, &hKey, 0)) {
        if (!RegQueryValueExW(hKey, HideFileExtW, 0, 0, (LPBYTE) &dwData, &dwDataSize))
            doHide = dwData;
        RegCloseKey (hKey);
    }

    if (!doHide) {
        LPWSTR ext = PathFindExtensionW(szPath);

        if (*ext != '\0') {
            WCHAR classname[MAX_PATH];
            LONG classlen = sizeof(classname);

            if (!RegQueryValueW(HKEY_CLASSES_ROOT, ext, classname, &classlen))
                if (!RegOpenKeyW(HKEY_CLASSES_ROOT, classname, &hKey)) {
                    if (!RegQueryValueExW(hKey, NeverShowExtW, 0, NULL, NULL, NULL))
                        doHide = TRUE;
                    RegCloseKey(hKey);
                }
        }
    }
    return doHide;
}

void SHELL_FS_ProcessDisplayFilename(LPWSTR szPath, DWORD dwFlags)
{
    /*FIXME: MSDN also mentions SHGDN_FOREDITING which is not yet handled. */
    if (!(dwFlags & SHGDN_FORPARSING) &&
        ((dwFlags & SHGDN_INFOLDER) || (dwFlags == SHGDN_NORMAL))) {
            if (SHELL_FS_HideExtension(szPath) && szPath[0] != '.')
                PathRemoveExtensionW(szPath);
    }
}

/**************************************************************************
*  CFSFolder::GetDisplayNameOf
*  Retrieves the display name for the specified file object or subfolder
*
* PARAMETERS
*  LPCITEMIDLIST pidl,    //[in ] complex pidl to item
*  DWORD         dwFlags, //[in ] SHGNO formatting flags
*  LPSTRRET      lpName)  //[out] Returned display name
*
* FIXME
*  if the name is in the pidl the ret value should be a STRRET_OFFSET
*/

HRESULT WINAPI CFSFolder::GetDisplayNameOf(PCUITEMID_CHILD pidl,
        DWORD dwFlags, LPSTRRET strRet)
{
    if (!strRet)
        return E_INVALIDARG;

    /* If it is a complex pidl, let the child handle it */
    if (!_ILIsPidlSimple (pidl)) /* complex pidl */
    {
        return SHELL32_GetDisplayNameOfChild(this, pidl, dwFlags, strRet);
    }
    else if (pidl && !pidl->mkid.cb) /* empty pidl */
    {
        /* If it is an empty pidl return only the path of the folder */
        if ((GET_SHGDN_FOR(dwFlags) & SHGDN_FORPARSING) && 
            (GET_SHGDN_RELATION(dwFlags) != SHGDN_INFOLDER) &&
            sPathTarget)
        {
            return SHSetStrRet(strRet, sPathTarget);
        }
        return E_INVALIDARG;
    }
    
    int len = 0;
    LPWSTR pszPath = (LPWSTR)CoTaskMemAlloc((MAX_PATH + 1) * sizeof(WCHAR));
    if (!pszPath)
        return E_OUTOFMEMORY;

    if ((GET_SHGDN_FOR(dwFlags) & SHGDN_FORPARSING) &&
        (GET_SHGDN_RELATION(dwFlags) != SHGDN_INFOLDER) &&
        sPathTarget)
    {
        lstrcpynW(pszPath, sPathTarget, MAX_PATH);
        PathAddBackslashW(pszPath);
        len = wcslen(pszPath);
    }
    _ILSimpleGetTextW(pidl, pszPath + len, MAX_PATH + 1 - len);
    if (!_ILIsFolder(pidl)) SHELL_FS_ProcessDisplayFilename(pszPath, dwFlags);

    strRet->uType = STRRET_WSTR;
    strRet->pOleStr = pszPath;

    TRACE ("-- (%p)->(%s)\n", this, strRet->uType == STRRET_CSTR ? strRet->cStr : debugstr_w(strRet->pOleStr));
    return S_OK;
}

/**************************************************************************
*  CFSFolder::SetNameOf
*  Changes the name of a file object or subfolder, possibly changing its item
*  identifier in the process.
*
* PARAMETERS
*  HWND          hwndOwner,  //[in ] Owner window for output
*  LPCITEMIDLIST pidl,       //[in ] simple pidl of item to change
*  LPCOLESTR     lpszName,   //[in ] the items new display name
*  DWORD         dwFlags,    //[in ] SHGNO formatting flags
*  LPITEMIDLIST* ppidlOut)   //[out] simple pidl returned
*/
HRESULT WINAPI CFSFolder::SetNameOf(
    HWND hwndOwner,
    PCUITEMID_CHILD pidl,
    LPCOLESTR lpName,
    DWORD dwFlags,
    PITEMID_CHILD *pPidlOut)
{
    WCHAR szSrc[MAX_PATH + 1], szDest[MAX_PATH + 1];
    BOOL bIsFolder = _ILIsFolder (ILFindLastID (pidl));

    TRACE ("(%p)->(%p,pidl=%p,%s,%u,%p)\n", this, hwndOwner, pidl,
           debugstr_w (lpName), dwFlags, pPidlOut);

    FileStructW* pDataW = _ILGetFileStructW(pidl);
    if (!pDataW)
    {
        ERR("Got garbage pidl\n");
        return E_INVALIDARG;
    }

    /* build source path */
    PathCombineW(szSrc, sPathTarget, pDataW->wszName);

    /* build destination path */
    if (dwFlags == SHGDN_NORMAL || dwFlags & SHGDN_INFOLDER)
        PathCombineW(szDest, sPathTarget, lpName);
    else
        lstrcpynW(szDest, lpName, MAX_PATH);

    if(!(dwFlags & SHGDN_FORPARSING) && SHELL_FS_HideExtension(szSrc)) {
        WCHAR *ext = PathFindExtensionW(szSrc);
        if(*ext != '\0') {
            INT len = wcslen(szDest);
            lstrcpynW(szDest + len, ext, MAX_PATH - len);
        }
    }

    TRACE ("src=%s dest=%s\n", debugstr_w(szSrc), debugstr_w(szDest));
    if (!wcscmp(szSrc, szDest))
    {
        /* src and destination is the same */
        HRESULT hr = S_OK;
        if (pPidlOut)
            hr = _ILCreateFromPathW(szDest, pPidlOut);

        return hr;
    }

    if (MoveFileW (szSrc, szDest))
    {
        HRESULT hr = S_OK;

        if (pPidlOut)
            hr = _ILCreateFromPathW(szDest, pPidlOut);

        SHChangeNotify (bIsFolder ? SHCNE_RENAMEFOLDER : SHCNE_RENAMEITEM,
                        SHCNF_PATHW, szSrc, szDest);

        return hr;
    }

    return E_FAIL;
}

HRESULT WINAPI CFSFolder::GetDefaultSearchGUID(GUID * pguid)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::EnumSearches(IEnumExtraSearch ** ppenum)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::GetDefaultColumn(DWORD dwRes,
        ULONG * pSort, ULONG * pDisplay)
{
    TRACE ("(%p)\n", this);

    if (pSort)
        *pSort = 0;
    if (pDisplay)
        *pDisplay = 0;

    return S_OK;
}

HRESULT WINAPI CFSFolder::GetDefaultColumnState(UINT iColumn,
        DWORD * pcsFlags)
{
    TRACE ("(%p)\n", this);

    if (!pcsFlags || iColumn >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    *pcsFlags = GenericSFHeader[iColumn].pcsFlags;

    return S_OK;
}

HRESULT WINAPI CFSFolder::GetDetailsEx(PCUITEMID_CHILD pidl,
                                       const SHCOLUMNID * pscid, VARIANT * pv)
{
    FIXME ("(%p)\n", this);

    return E_NOTIMPL;
}

HRESULT WINAPI CFSFolder::GetDetailsOf(PCUITEMID_CHILD pidl,
                                       UINT iColumn, SHELLDETAILS * psd)
{
    HRESULT hr = E_FAIL;

    TRACE ("(%p)->(%p %i %p)\n", this, pidl, iColumn, psd);

    if (!psd || iColumn >= GENERICSHELLVIEWCOLUMNS)
        return E_INVALIDARG;

    if (!pidl)
    {
        /* the header titles */
        psd->fmt = GenericSFHeader[iColumn].fmt;
        psd->cxChar = GenericSFHeader[iColumn].cxChar;
        return SHSetStrRet(&psd->str, GenericSFHeader[iColumn].colnameid);
    }
    else
    {
        hr = S_OK;
        psd->str.uType = STRRET_CSTR;
        /* the data from the pidl */
        switch (iColumn)
        {
            case 0:                /* name */
                hr = GetDisplayNameOf (pidl, SHGDN_NORMAL | SHGDN_INFOLDER, &psd->str);
                break;
            case 1:                /* FIXME: comments */
                psd->str.cStr[0] = 0;
                break;
            case 2:                /* type */
                _ILGetFileType(pidl, psd->str.cStr, MAX_PATH);
                break;
            case 3:                /* size */
                _ILGetFileSize(pidl, psd->str.cStr, MAX_PATH);
                break;
            case 4:                /* date */
                _ILGetFileDate(pidl, psd->str.cStr, MAX_PATH);
                break;
            case 5:                /* attributes */
                _ILGetFileAttributes(pidl, psd->str.cStr, MAX_PATH);
                break;
        }
    }

    return hr;
}

HRESULT WINAPI CFSFolder::MapColumnToSCID (UINT column,
        SHCOLUMNID * pscid)
{
    FIXME ("(%p)\n", this);
    return E_NOTIMPL;
}

/************************************************************************
 * CFSFolder::GetClassID
 */
HRESULT WINAPI CFSFolder::GetClassID(CLSID * lpClassId)
{
    TRACE ("(%p)\n", this);

    if (!lpClassId)
        return E_POINTER;

    *lpClassId = *pclsid;

    return S_OK;
}

/************************************************************************
 * CFSFolder::Initialize
 *
 * NOTES
 *  sPathTarget is not set. Don't know how to handle in a non rooted environment.
 */
HRESULT WINAPI CFSFolder::Initialize(LPCITEMIDLIST pidl)
{
    WCHAR wszTemp[MAX_PATH];

    TRACE ("(%p)->(%p)\n", this, pidl);

    SHFree (pidlRoot);     /* free the old pidl */
    pidlRoot = ILClone (pidl); /* set my pidl */

    SHFree (sPathTarget);
    sPathTarget = NULL;

    /* set my path */
    if (SHGetPathFromIDListW (pidl, wszTemp))
    {
        int len = wcslen(wszTemp);
        sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
        if (!sPathTarget)
            return E_OUTOFMEMORY;
        memcpy(sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
    }

    TRACE ("--(%p)->(%s)\n", this, debugstr_w(sPathTarget));
    return S_OK;
}

/**************************************************************************
 * CFSFolder::GetCurFolder
 */
HRESULT WINAPI CFSFolder::GetCurFolder(LPITEMIDLIST * pidl)
{
    TRACE ("(%p)->(%p)\n", this, pidl);

    if (!pidl)
        return E_POINTER;

    *pidl = ILClone(pidlRoot);
    return S_OK;
}

/**************************************************************************
 * CFSFolder::InitializeEx
 *
 * FIXME: error handling
 */
HRESULT WINAPI CFSFolder::InitializeEx(IBindCtx * pbc, LPCITEMIDLIST pidlRootx,
                                       const PERSIST_FOLDER_TARGET_INFO * ppfti)
{
    WCHAR wszTemp[MAX_PATH];

    TRACE("(%p)->(%p,%p,%p)\n", this, pbc, pidlRootx, ppfti);
    if (ppfti)
        TRACE("--%p %s %s 0x%08x 0x%08x\n",
              ppfti->pidlTargetFolder, debugstr_w (ppfti->szTargetParsingName),
              debugstr_w (ppfti->szNetworkProvider), ppfti->dwAttributes,
              ppfti->csidl);

    pdump (pidlRootx);
    if (ppfti && ppfti->pidlTargetFolder)
        pdump(ppfti->pidlTargetFolder);

    if (pidlRoot)
        __SHFreeAndNil(&pidlRoot);    /* free the old */
    if (sPathTarget)
        __SHFreeAndNil(&sPathTarget);

    /*
     * Root path and pidl
     */
    pidlRoot = ILClone(pidlRootx);

    /*
     *  the target folder is spezified in csidl OR pidlTargetFolder OR
     *  szTargetParsingName
     */
    if (ppfti)
    {
        if (ppfti->csidl != -1)
        {
            if (SHGetSpecialFolderPathW(0, wszTemp, ppfti->csidl,
                                        ppfti->csidl & CSIDL_FLAG_CREATE)) {
                int len = wcslen(wszTemp);
                sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
                if (!sPathTarget)
                    return E_OUTOFMEMORY;
                memcpy(sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
            }
        }
        else if (ppfti->szTargetParsingName[0])
        {
            int len = wcslen(ppfti->szTargetParsingName);
            sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
            if (!sPathTarget)
                return E_OUTOFMEMORY;
            memcpy(sPathTarget, ppfti->szTargetParsingName,
                   (len + 1) * sizeof(WCHAR));
        }
        else if (ppfti->pidlTargetFolder)
        {
            if (SHGetPathFromIDListW(ppfti->pidlTargetFolder, wszTemp))
            {
                int len = wcslen(wszTemp);
                sPathTarget = (WCHAR *)SHAlloc((len + 1) * sizeof(WCHAR));
                if (!sPathTarget)
                    return E_OUTOFMEMORY;
                memcpy(sPathTarget, wszTemp, (len + 1) * sizeof(WCHAR));
            }
        }
    }

    TRACE("--(%p)->(target=%s)\n", this, debugstr_w(sPathTarget));
    pdump(pidlRoot);
    return (sPathTarget) ? S_OK : E_FAIL;
}

HRESULT WINAPI CFSFolder::GetFolderTargetInfo(PERSIST_FOLDER_TARGET_INFO * ppfti)
{
    FIXME("(%p)->(%p)\n", this, ppfti);
    ZeroMemory(ppfti, sizeof (*ppfti));
    return E_NOTIMPL;
}

HRESULT CFSFolder::_CreateExtensionUIObject(PCUIDLIST_RELATIVE pidl, REFIID riid, LPVOID *ppvOut)
{
    static const WCHAR formatW[] = {'S','h','e','l','l','E','x','\\',
        '{','%','0','8','x','-','%','0','4','x','-','%','0','4','x','-',
        '%','0','2','x','%','0','2','x','-','%','0','2','x','%','0','2','x',
        '%','0','2','x','%','0','2','x','%','0','2','x','%','0','2','x','}',0};
    WCHAR buf[MAX_PATH];

    sprintfW(buf, formatW, riid.Data1, riid.Data2, riid.Data3,
             riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
             riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);

    CLSID clsid;
    HRESULT hr;

    hr = GetCLSIDForFileType(pidl, buf, &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    return S_OK;
}

HRESULT CFSFolder::_GetDropTarget(LPCITEMIDLIST pidl, LPVOID *ppvOut) 
{
    HRESULT hr;

    TRACE("CFSFolder::_GetDropTarget entered\n");

    if (_ILIsFolder (pidl))
    {
        CComPtr<IShellFolder> psfChild;
        hr = this->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &psfChild));
        if (FAILED_UNEXPECTEDLY(hr))
            return hr;

        return psfChild->CreateViewObject(NULL, IID_IDropTarget, ppvOut);
    }

    CLSID clsid;
    hr = GetCLSIDForFileType(pidl, L"shellex\\DropHandler", &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, IID_IDropTarget, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return S_FALSE;

    return S_OK;
}

HRESULT CFSFolder::_GetIconHandler(LPCITEMIDLIST pidl, REFIID riid, LPVOID *ppvOut) 
{
    CLSID clsid;
    HRESULT hr;

    hr = GetCLSIDForFileType(pidl, L"shellex\\IconHandler", &clsid);
    if (hr != S_OK)
        return hr;

    hr = _CreateShellExtInstance(&clsid, pidl, riid, ppvOut);
    if (FAILED_UNEXPECTEDLY(hr))
        return S_FALSE;

    return S_OK;
}

HRESULT CFSFolder::_CreateShellExtInstance(const CLSID *pclsid, LPCITEMIDLIST pidl, REFIID riid, LPVOID *ppvOut)
{
    HRESULT hr;
    WCHAR wszPath[MAX_PATH];

    FileStructW* pDataW = _ILGetFileStructW(pidl);
    if (!pDataW)
    {
        ERR("Got garbage pidl\n");
        return E_INVALIDARG;
    }

    PathCombineW(wszPath, sPathTarget, pDataW->wszName);

    CComPtr<IPersistFile> pp;
    hr = SHCoCreateInstance(NULL, pclsid, NULL, IID_PPV_ARG(IPersistFile, &pp));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    pp->Load(wszPath, 0);

    hr = pp->QueryInterface(riid, ppvOut);
    if (hr != S_OK)
    {
        ERR("Failed to query for interface IID_IShellExtInit hr %x pclsid %s\n", hr, wine_dbgstr_guid(pclsid));
        return hr;
    }
    return hr;
}

HRESULT WINAPI CFSFolder::CallBack(IShellFolder *psf, HWND hwndOwner, IDataObject *pdtobj, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg != DFM_MERGECONTEXTMENU && uMsg != DFM_INVOKECOMMAND)
        return S_OK;

    /* no data object means no selection */
    if (!pdtobj)
    {
        if (uMsg == DFM_INVOKECOMMAND && wParam == 0)
        {
            PUITEMID_CHILD pidlChild = ILClone(ILFindLastID(pidlRoot));
            LPITEMIDLIST pidlParent = ILClone(pidlRoot);
            ILRemoveLastID(pidlParent);
            HRESULT hr = SH_ShowPropertiesDialog(sPathTarget, pidlParent, &pidlChild);
            if (FAILED(hr))
                ERR("SH_ShowPropertiesDialog failed\n");
            ILFree(pidlChild);
            ILFree(pidlParent);
        }
        else if (uMsg == DFM_MERGECONTEXTMENU)
        {
            QCMINFO *pqcminfo = (QCMINFO *)lParam;
            HMENU hpopup = CreatePopupMenu();
            _InsertMenuItemW(hpopup, 0, TRUE, 0, MFT_STRING, MAKEINTRESOURCEW(IDS_PROPERTIES), MFS_ENABLED);
            Shell_MergeMenus(pqcminfo->hmenu, hpopup, pqcminfo->indexMenu++, pqcminfo->idCmdFirst, pqcminfo->idCmdLast, MM_ADDSEPARATOR);
            DestroyMenu(hpopup);
        }

        return S_OK;
    }

    if (uMsg != DFM_INVOKECOMMAND || wParam != DFM_CMD_PROPERTIES)
        return S_OK;

    return Shell_DefaultContextMenuCallBack(this, pdtobj);
}
