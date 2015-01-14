/*
 * Copyright 2015 Michael Müller
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

#include "config.h"
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "ocidl.h"
#include "netlistmgr.h"
#include "olectl.h"

#include "wine/debug.h"
#include "netprofm_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(netprofm);

struct connection_point
{
    IConnectionPoint IConnectionPoint_iface;
    IConnectionPointContainer *container;
    LONG refs;
    IID iid;
};

static inline struct connection_point *impl_from_IConnectionPoint(
    IConnectionPoint *iface )
{
    return CONTAINING_RECORD( iface, struct connection_point, IConnectionPoint_iface );
}

static ULONG WINAPI connection_point_AddRef(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    return InterlockedIncrement( &cp->refs );
}

static ULONG WINAPI connection_point_Release(
    IConnectionPoint *iface )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    LONG refs = InterlockedDecrement( &cp->refs );
    if (!refs)
    {
        TRACE( "destroying %p\n", cp );
        IConnectionPointContainer_Release( cp->container );
        HeapFree( GetProcessHeap(), 0, cp );
    }
    return refs;
}

static HRESULT WINAPI connection_point_QueryInterface(
    IConnectionPoint *iface,
    REFIID riid,
    void **obj )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %s, %p\n", cp, debugstr_guid(riid), obj );

    if (IsEqualGUID( riid, &IID_IConnectionPoint ) ||
        IsEqualGUID( riid, &IID_IUnknown ))
    {
        *obj = iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        return E_NOINTERFACE;
    }
    IConnectionPoint_AddRef( iface );
    return S_OK;
}

static HRESULT WINAPI connection_point_GetConnectionInterface(
    IConnectionPoint *iface,
    IID *pIID )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, pIID );

    if (!pIID)
        return E_POINTER;

    memcpy(pIID, &cp->iid, sizeof(IID));
    return S_OK;
}

static HRESULT WINAPI connection_point_GetConnectionPointContainer(
    IConnectionPoint *iface,
    IConnectionPointContainer **ppCPC )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    TRACE( "%p, %p\n", cp, ppCPC );

    if (!ppCPC)
        return E_POINTER;

    IConnectionPointContainer_AddRef(cp->container);
    *ppCPC = cp->container;
    return S_OK;
}

static HRESULT WINAPI connection_point_Advise(
    IConnectionPoint *iface,
    IUnknown *pUnkSink,
    DWORD *pdwCookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    FIXME( "%p, %p, %p - stub\n", cp, pUnkSink, pdwCookie);

    if (!pUnkSink || !pdwCookie)
        return E_POINTER;

    return CONNECT_E_CANNOTCONNECT;
}

static HRESULT WINAPI connection_point_Unadvise(
    IConnectionPoint *iface,
    DWORD dwCookie )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    FIXME( "%p, %d - stub\n", cp, dwCookie);

    return E_POINTER;
}

static HRESULT WINAPI connection_point_EnumConnections(
    IConnectionPoint *iface,
    IEnumConnections **ppEnum )
{
    struct connection_point *cp = impl_from_IConnectionPoint( iface );
    FIXME( "%p, %p - stub\n", cp, ppEnum);

    return E_NOTIMPL;
}

static const IConnectionPointVtbl connection_point_vtbl =
{
    connection_point_QueryInterface,
    connection_point_AddRef,
    connection_point_Release,
    connection_point_GetConnectionInterface,
    connection_point_GetConnectionPointContainer,
    connection_point_Advise,
    connection_point_Unadvise,
    connection_point_EnumConnections
};

HRESULT connection_point_create( IConnectionPoint **obj, REFIID riid, IConnectionPointContainer *container )
{
    struct connection_point *cp;
    TRACE( "%p, %s, %p\n", obj, debugstr_guid(riid), container );

    if (!(cp = HeapAlloc( GetProcessHeap(), 0, sizeof(*cp) ))) return E_OUTOFMEMORY;
    cp->IConnectionPoint_iface.lpVtbl = &connection_point_vtbl;
    cp->container = container;
    cp->refs = 1;

    memcpy(&cp->iid, riid, sizeof(IID));
    IConnectionPointContainer_AddRef(container);

    *obj = &cp->IConnectionPoint_iface;
    TRACE( "returning iface %p\n", *obj );
    return S_OK;
}
