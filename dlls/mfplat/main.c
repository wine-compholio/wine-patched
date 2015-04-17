/*
 *
 * Copyright 2014 Austin English
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
#include "config.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "mfapi.h"
#include "mferror.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

static WCHAR transform_keyW[] = {'S','o','f','t','w','a','r','e','\\','C','l','a','s','s','e','s',
                                 '\\','M','e','d','i','a','F','o','u','n','d','a','t','i','o','n','\\',
                                 'T','r','a','n','s','f','o','r','m','s',0};
static WCHAR categories_keyW[] = {'S','o','f','t','w','a','r','e','\\','C','l','a','s','s','e','s',
                                 '\\','M','e','d','i','a','F','o','u','n','d','a','t','i','o','n','\\',
                                 'T','r','a','n','s','f','o','r','m','s','\\',
                                 'C','a','t','e','g','o','r','i','e','s',0};
static WCHAR inputtypesW[]  = {'I','n','p','u','t','T','y','p','e','s',0};
static WCHAR outputtypesW[] = {'O','u','t','p','u','t','T','y','p','e','s',0};
static const WCHAR szGUIDFmt[] =
{
    '%','0','8','x','-','%','0','4','x','-','%','0','4','x','-','%','0',
    '2','x','%','0','2','x','-','%','0','2','x','%','0','2','x','%','0','2',
    'x','%','0','2','x','%','0','2','x','%','0','2','x',0
};

static LPWSTR GUIDToString(LPWSTR lpwstr, REFGUID lpcguid)
{
    wsprintfW(lpwstr, szGUIDFmt, lpcguid->Data1, lpcguid->Data2,
        lpcguid->Data3, lpcguid->Data4[0], lpcguid->Data4[1],
        lpcguid->Data4[2], lpcguid->Data4[3], lpcguid->Data4[4],
        lpcguid->Data4[5], lpcguid->Data4[6], lpcguid->Data4[7]);

    return lpwstr;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            break;
    }

    return TRUE;
}

static HRESULT register_transform(CLSID *clsid, WCHAR *name,
                                  UINT32 cinput, MFT_REGISTER_TYPE_INFO *inputTypes,
                                  UINT32 coutput, MFT_REGISTER_TYPE_INFO *outputTypes)
{
    HKEY htransform, hclsid = 0;
    WCHAR buffer[64];
    GUID *types;
    DWORD size;
    LONG ret;
    UINT32 i;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, transform_keyW, &htransform))
        return E_FAIL;

    GUIDToString(buffer, clsid);
    ret = RegCreateKeyW(htransform, buffer, &hclsid);
    RegCloseKey(htransform);
    if (ret) return E_FAIL;

    size = (strlenW(name) + 1) * sizeof(WCHAR);
    if (RegSetValueExW(hclsid, NULL, 0, REG_SZ, (BYTE *)name, size))
        goto err;

    if (cinput)
    {
        size = 2 * cinput * sizeof(GUID);
        types = HeapAlloc(GetProcessHeap(), 0, size);
        if (!types) goto err;

        for (i = 0; i < cinput; i++)
        {
            memcpy(&types[2 * i],     &inputTypes[i].guidMajorType, sizeof(GUID));
            memcpy(&types[2 * i + 1], &inputTypes[i].guidSubtype,   sizeof(GUID));
        }

        ret = RegSetValueExW(hclsid, inputtypesW, 0, REG_BINARY, (BYTE *)types, size);
        HeapFree(GetProcessHeap(), 0, types);
        if (ret) goto err;
    }

    if (coutput)
    {
        size = 2 * coutput * sizeof(GUID);
        types = HeapAlloc(GetProcessHeap(), 0, size);
        if (!types) goto err;

        for (i = 0; i < coutput; i++)
        {
            memcpy(&types[2 * i],     &outputTypes[i].guidMajorType, sizeof(GUID));
            memcpy(&types[2 * i + 1], &outputTypes[i].guidSubtype,   sizeof(GUID));
        }

        ret = RegSetValueExW(hclsid, outputtypesW, 0, REG_BINARY, (BYTE *)types, size);
        HeapFree(GetProcessHeap(), 0, types);
        if (ret) goto err;
    }

    RegCloseKey(hclsid);
    return S_OK;

err:
    RegCloseKey(hclsid);
    return E_FAIL;
}

static HRESULT register_category(CLSID *clsid, GUID *category)
{
    HKEY hcategory, htmp1, htmp2;
    WCHAR buffer[64];
    DWORD ret;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, categories_keyW, &hcategory))
        return E_FAIL;

    GUIDToString(buffer, category);
    ret = RegCreateKeyW(hcategory, buffer, &htmp1);
    RegCloseKey(hcategory);
    if (ret) return E_FAIL;

    GUIDToString(buffer, clsid);
    ret = RegCreateKeyW(htmp1, buffer, &htmp2);
    RegCloseKey(htmp1);
    if (ret) return E_FAIL;

    RegCloseKey(htmp2);
    return S_OK;
}

/***********************************************************************
 *      MFTRegister (mfplat.@)
 */
HRESULT WINAPI MFTRegister(CLSID clsid, GUID category, LPWSTR name, UINT32 flags, UINT32 cinput,
                           MFT_REGISTER_TYPE_INFO *inputTypes, UINT32 coutput,
                           MFT_REGISTER_TYPE_INFO *outputTypes, void *attributes)
{
    HRESULT hr;

    FIXME("(%s, %s, %s, %x, %u, %p, %u, %p, %p)\n", debugstr_guid(&clsid), debugstr_guid(&category),
                                                    debugstr_w(name), flags, cinput, inputTypes,
                                                    coutput, outputTypes, attributes);

    if (attributes)
        FIXME("attributes not yet supported.\n");

    if (flags)
        FIXME("flags not yet supported.\n");

    hr = register_transform(&clsid, name, cinput, inputTypes, coutput, outputTypes);

    if (SUCCEEDED(hr))
        hr = register_category(&clsid, &category);

    return hr;
}

/***********************************************************************
 *      MFStartup (mfplat.@)
 */
HRESULT WINAPI MFStartup(ULONG version, DWORD flags)
{
    FIXME("(%u, %u): stub\n", version, flags);
    return MF_E_BAD_STARTUP_VERSION;
}

/***********************************************************************
 *      MFShutdown (mfplat.@)
 */
HRESULT WINAPI MFShutdown(void)
{
    FIXME("(): stub\n");
    return S_OK;
}
