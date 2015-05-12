/*
 *	Shell AutoComplete list
 *
 *	Copyright 2008	CodeWeavers, Aric Stewart
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

#define COBJMACROS

#include "wine/debug.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winuser.h"
#include "shlwapi.h"
#include "winerror.h"
#include "objbase.h"

#include "shlguid.h"
#include "shlobj.h"

#include "wine/unicode.h"

#include "browseui.h"

WINE_DEFAULT_DEBUG_CHANNEL(browseui);

typedef struct tagACLMulti {
    IACList2 IACList2_iface;
    IEnumString IEnumString_iface;
    LONG refCount;
    DWORD dwOptions;
} ACLShellSource;

static inline ACLShellSource *impl_from_IACList2(IACList2 *iface)
{
    return CONTAINING_RECORD(iface, ACLShellSource, IACList2_iface);
}

static inline ACLShellSource *impl_from_IEnumString(IEnumString *iface)
{
    return CONTAINING_RECORD(iface, ACLShellSource, IEnumString_iface);
}

static void ACLShellSource_Destructor(ACLShellSource *This)
{
    TRACE("destroying %p\n", This);
    heap_free(This);
}

static HRESULT WINAPI ACLShellSource_QueryInterface(IACList2 *iface, REFIID iid, LPVOID *ppvOut)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    *ppvOut = NULL;

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IACList2) ||
        IsEqualIID(iid, &IID_IACList))
    {
        *ppvOut = &This->IACList2_iface;
    }
    else if(IsEqualIID(iid, &IID_IEnumString))
    {
        *ppvOut = &This->IEnumString_iface;
    }

    if (*ppvOut)
    {
        IACList2_AddRef(iface);
        return S_OK;
    }

    WARN("unsupported interface: %s\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI ACLShellSource_AddRef(IACList2 *iface)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    return InterlockedIncrement(&This->refCount);
}

static ULONG WINAPI ACLShellSource_Release(IACList2 *iface)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    ULONG ret;

    ret = InterlockedDecrement(&This->refCount);
    if (ret == 0)
        ACLShellSource_Destructor(This);
    return ret;
}

static HRESULT WINAPI ACLShellSource_Expand(IACList2 *iface, LPCWSTR wstr)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    FIXME("STUB:(%p) %s\n",This,debugstr_w(wstr));
    return E_NOTIMPL;
}


static HRESULT WINAPI ACLShellSource_GetOptions(IACList2 *iface,
    DWORD *pdwFlag)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    *pdwFlag = This->dwOptions;
    return S_OK;
}

static HRESULT WINAPI ACLShellSource_SetOptions(IACList2 *iface,
    DWORD dwFlag)
{
    ACLShellSource *This = impl_from_IACList2(iface);
    This->dwOptions = dwFlag;
    return S_OK;
}

static const IACList2Vtbl ACLMulti_ACList2Vtbl =
{
    ACLShellSource_QueryInterface,
    ACLShellSource_AddRef,
    ACLShellSource_Release,

    ACLShellSource_Expand,

    ACLShellSource_SetOptions,
    ACLShellSource_GetOptions
};

static HRESULT WINAPI ACLShellSource_IEnumString_QueryInterface(IEnumString *iface, REFIID iid, LPVOID *ppvOut)
{
    ACLShellSource *This = impl_from_IEnumString(iface);
    return ACLShellSource_QueryInterface(&This->IACList2_iface, iid, ppvOut);
}

static ULONG WINAPI ACLShellSource_IEnumString_AddRef(IEnumString *iface)
{
    ACLShellSource *This = impl_from_IEnumString(iface);
    return ACLShellSource_AddRef(&This->IACList2_iface);
}

static ULONG WINAPI ACLShellSource_IEnumString_Release(IEnumString *iface)
{
    ACLShellSource *This = impl_from_IEnumString(iface);
    return ACLShellSource_Release(&This->IACList2_iface);
}

static HRESULT WINAPI ACLShellSource_IEnumString_Next(IEnumString *iface, ULONG celt, LPOLESTR *rgelt, ULONG *pceltFetched)
{
    FIXME("(%p, %d, %p, %p): stub\n", iface, celt, rgelt, pceltFetched);

    if (celt)
        *rgelt = NULL;
    if (pceltFetched)
        *pceltFetched = 0;

    return S_FALSE;
}

static HRESULT WINAPI ACLShellSource_IEnumString_Reset(IEnumString *iface)
{
    FIXME("(%p): stub\n", iface);
    return S_OK;
}

static HRESULT WINAPI ACLShellSource_IEnumString_Skip(IEnumString *iface, ULONG celt)
{
    FIXME("(%p, %u): stub\n", iface, celt);
    return E_NOTIMPL;
}

static HRESULT WINAPI ACLShellSource_IEnumString_Clone(IEnumString *iface, IEnumString **ppOut)
{
    FIXME("(%p, %p): stub\n", iface, ppOut);
    *ppOut = NULL;
    return E_OUTOFMEMORY;
}

static const IEnumStringVtbl ACLShellSource_EnumStringVtbl =
{
    ACLShellSource_IEnumString_QueryInterface,
    ACLShellSource_IEnumString_AddRef,
    ACLShellSource_IEnumString_Release,

    ACLShellSource_IEnumString_Next,
    ACLShellSource_IEnumString_Skip,
    ACLShellSource_IEnumString_Reset,
    ACLShellSource_IEnumString_Clone
};

HRESULT ACLShellSource_Constructor(IUnknown *pUnkOuter, IUnknown **ppOut)
{
    ACLShellSource *This;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    This = heap_alloc_zero(sizeof(ACLShellSource));
    if (This == NULL)
        return E_OUTOFMEMORY;

    This->IACList2_iface.lpVtbl = &ACLMulti_ACList2Vtbl;
    This->IEnumString_iface.lpVtbl = &ACLShellSource_EnumStringVtbl;
    This->refCount = 1;

    TRACE("returning %p\n", This);
    *ppOut = (IUnknown *)&This->IACList2_iface;
    return S_OK;
}
