/*
 * IEnumWIA_DEV_INFO implementation.
 *
 * Copyright 2014 Mikael Ståldal
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

#define COBJMACROS

#include "objbase.h"
#include "wia_lh.h"

#include "wiaservc_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wia);

static inline enumwiadevinfo *impl_from_IEnumWIA_DEV_INFO(IEnumWIA_DEV_INFO *iface)
{
    return CONTAINING_RECORD(iface, enumwiadevinfo, IEnumWIA_DEV_INFO_iface);
}

static HRESULT WINAPI enumwiadevinfo_QueryInterface(IEnumWIA_DEV_INFO *iface, REFIID riid, void **ppvObject)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);

    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppvObject);

    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IEnumWIA_DEV_INFO))
        *ppvObject = iface;
    else
    {
        FIXME("interface %s not implemented\n", debugstr_guid(riid));
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown*) *ppvObject);
    return S_OK;
}

static ULONG WINAPI enumwiadevinfo_AddRef(IEnumWIA_DEV_INFO *iface)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI enumwiadevinfo_Release(IEnumWIA_DEV_INFO *iface)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    ULONG ref;

    ref = InterlockedDecrement(&This->ref);
    if (ref == 0)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI enumwiadevinfo_Next(IEnumWIA_DEV_INFO *iface, ULONG celt, IWiaPropertyStorage **rgelt, ULONG *pceltFetched)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    TRACE("(%p, %d, %p, %p)\n", This, celt, rgelt, pceltFetched);

    *pceltFetched = 0;
    return S_FALSE;
}

static HRESULT WINAPI enumwiadevinfo_Skip(IEnumWIA_DEV_INFO *iface, ULONG celt)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    TRACE("(%p, %u)\n", This, celt);

    return S_FALSE;
}

static HRESULT WINAPI enumwiadevinfo_Reset(IEnumWIA_DEV_INFO *iface)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    TRACE("(%p)\n", This);

    return S_OK;
}

static HRESULT WINAPI enumwiadevinfo_Clone(IEnumWIA_DEV_INFO *iface, IEnumWIA_DEV_INFO **ppIEnum)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    FIXME("(%p, %p): stub\n", This, ppIEnum);

    return E_NOTIMPL;
}

static HRESULT WINAPI enumwiadevinfo_GetCount(IEnumWIA_DEV_INFO *iface, ULONG *celt)
{
    enumwiadevinfo *This = impl_from_IEnumWIA_DEV_INFO(iface);
    TRACE("(%p, %p)\n", This, celt);

    *celt = 0;
    return S_OK;
}

static const IEnumWIA_DEV_INFOVtbl WIASERVC_IEnumWIA_DEV_INFO_Vtbl =
{
    enumwiadevinfo_QueryInterface,
    enumwiadevinfo_AddRef,
    enumwiadevinfo_Release,
    enumwiadevinfo_Next,
    enumwiadevinfo_Skip,
    enumwiadevinfo_Reset,
    enumwiadevinfo_Clone,
    enumwiadevinfo_GetCount
};

HRESULT enumwiadevinfo_Constructor(IEnumWIA_DEV_INFO **ppObj)
{
    enumwiadevinfo *This;
    TRACE("(%p)\n", ppObj);
    This = HeapAlloc(GetProcessHeap(), 0, sizeof(enumwiadevinfo));
    if (This)
    {
        This->IEnumWIA_DEV_INFO_iface.lpVtbl = &WIASERVC_IEnumWIA_DEV_INFO_Vtbl;
        This->ref = 1;
        *ppObj = &This->IEnumWIA_DEV_INFO_iface;
        return S_OK;
    }
    *ppObj = NULL;
    return E_OUTOFMEMORY;
}
