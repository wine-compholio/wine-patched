/*
 *	file system folder
 *
 *  Copyright 1997          Marcus Meissner
 *  Copyright 1998, 1999, 2002 Juergen Schmied
 *  Copyright 2015          Michael Müller
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
#include "wine/port.h"

#include <stdarg.h>
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "wingdi.h"
#include "winuser.h"

#include "objbase.h"
#include "ole2.h"
#include "shlguid.h"
#include "shlobj.h"

#include "wine/debug.h"
#include "debughlp.h"

WINE_DEFAULT_DEBUG_CHANNEL (shell);

/***********************************************************************
*   IDropTargetHelper implementation
*/

typedef struct
{
    IDropTargetHelper IDropTargetHelper_iface;
    IDragSourceHelper IDragSourceHelper_iface;
    LONG ref;
} IDragHelperImpl;

static inline IDragHelperImpl *impl_from_IDropTargetHelper(IDropTargetHelper *iface)
{
    return CONTAINING_RECORD(iface, IDragHelperImpl, IDropTargetHelper_iface);
}

static inline IDragHelperImpl *impl_from_IDragSourceHelper(IDragSourceHelper *iface)
{
    return CONTAINING_RECORD(iface, IDragHelperImpl, IDragSourceHelper_iface);
}

/**************************************************************************
 *	IDropTargetHelper_fnQueryInterface
 */
static HRESULT WINAPI IDropTargetHelper_fnQueryInterface (IDropTargetHelper * iface, REFIID riid, LPVOID * ppvObj)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);

    TRACE ("(%p)->(%s,%p)\n", This, shdebugstr_guid (riid), ppvObj);

    *ppvObj = NULL;

    if (IsEqualIID (riid, &IID_IUnknown) || IsEqualIID (riid, &IID_IDropTargetHelper))
    {
       *ppvObj = &This->IDropTargetHelper_iface;
    }
    else if (IsEqualIID (riid, &IID_IDragSourceHelper))
    {
       *ppvObj = &This->IDragSourceHelper_iface;
    }

    if (*ppvObj)
    {
        IUnknown_AddRef ((IUnknown *) (*ppvObj));
        TRACE ("-- Interface: (%p)->(%p)\n", ppvObj, *ppvObj);
        return S_OK;
    }

    FIXME ("-- Interface: E_NOINTERFACE\n");
    return E_NOINTERFACE;
}

static ULONG WINAPI IDropTargetHelper_fnAddRef (IDropTargetHelper * iface)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    ULONG refCount = InterlockedIncrement(&This->ref);

    TRACE ("(%p)->(count=%u)\n", This, refCount - 1);

    return refCount;
}

static ULONG WINAPI IDropTargetHelper_fnRelease (IDropTargetHelper * iface)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    ULONG refCount = InterlockedDecrement(&This->ref);

    TRACE ("(%p)->(count=%u)\n", This, refCount + 1);

    if (!refCount)
    {
        TRACE ("-- destroying (%p)\n", This);
        HeapFree(GetProcessHeap(), 0, This);
        return 0;
    }
    return refCount;
}

static HRESULT WINAPI IDropTargetHelper_fnDragEnter (IDropTargetHelper * iface, HWND hwndTarget,
                                                     IDataObject* pDataObject,POINT* ppt, DWORD dwEffect)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    FIXME ("(%p)->(%p %p %p 0x%08x)\n", This,hwndTarget, pDataObject, ppt, dwEffect);
    return E_NOTIMPL;
}

static HRESULT WINAPI IDropTargetHelper_fnDragLeave (IDropTargetHelper * iface)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    FIXME ("(%p)->()\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI IDropTargetHelper_fnDragOver (IDropTargetHelper * iface, POINT* ppt, DWORD dwEffect)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    FIXME ("(%p)->(%p 0x%08x)\n", This, ppt, dwEffect);
    return E_NOTIMPL;
}

static HRESULT WINAPI IDropTargetHelper_fnDrop (IDropTargetHelper * iface, IDataObject* pDataObject, POINT* ppt, DWORD dwEffect)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    FIXME ("(%p)->(%p %p 0x%08x)\n", This, pDataObject, ppt, dwEffect);
    return E_NOTIMPL;
}

static HRESULT WINAPI IDropTargetHelper_fnShow (IDropTargetHelper * iface, BOOL fShow)
{
    IDragHelperImpl *This = impl_from_IDropTargetHelper(iface);
    FIXME ("(%p)->(%u)\n", This, fShow);
    return E_NOTIMPL;
}

static const IDropTargetHelperVtbl vt_IDropTargetHelper =
{
    IDropTargetHelper_fnQueryInterface,
    IDropTargetHelper_fnAddRef,
    IDropTargetHelper_fnRelease,
    IDropTargetHelper_fnDragEnter,
    IDropTargetHelper_fnDragLeave,
    IDropTargetHelper_fnDragOver,
    IDropTargetHelper_fnDrop,
    IDropTargetHelper_fnShow
};

static HRESULT WINAPI IDragSourceHelper_fnQueryInterface (IDragSourceHelper * iface, REFIID riid, LPVOID * ppv)
{
    IDragHelperImpl *This = impl_from_IDragSourceHelper(iface);
    return IDropTargetHelper_fnQueryInterface(&This->IDropTargetHelper_iface, riid, ppv);
}

static ULONG WINAPI IDragSourceHelper_fnAddRef (IDragSourceHelper * iface)
{
    IDragHelperImpl *This = impl_from_IDragSourceHelper(iface);
    return IDropTargetHelper_fnAddRef(&This->IDropTargetHelper_iface);
}

static ULONG WINAPI IDragSourceHelper_fnRelease (IDragSourceHelper * iface)
{
    IDragHelperImpl *This = impl_from_IDragSourceHelper(iface);
    return IDropTargetHelper_fnRelease(&This->IDropTargetHelper_iface);
}

static HRESULT WINAPI IDragSourceHelper_fnInitializeFromBitmap(IDragSourceHelper * iface, LPSHDRAGIMAGE pshdi,
                                                               IDataObject *object)
{
    IDragHelperImpl *This = impl_from_IDragSourceHelper(iface);

    FIXME("(%p)->(%p, %p): stub\n", This, pshdi, object);
    return S_OK;
}

static HRESULT WINAPI IDragSourceHelper_fnInitializeFromWindow(IDragSourceHelper * iface, HWND hwnd, POINT *ppt,
                                                               IDataObject *object)
{
    IDragHelperImpl *This = impl_from_IDragSourceHelper(iface);

    FIXME("(%p)->(%p, %p, %p): stub\n", This, hwnd, ppt, object);
    return S_OK;
}

static const IDragSourceHelperVtbl vt_IDragSourceHelper =
{
    IDragSourceHelper_fnQueryInterface,
    IDragSourceHelper_fnAddRef,
    IDragSourceHelper_fnRelease,
    IDragSourceHelper_fnInitializeFromBitmap,
    IDragSourceHelper_fnInitializeFromWindow
};

/**************************************************************************
*   IDropTargetHelper_Constructor
*/
HRESULT WINAPI IDropTargetHelper_Constructor (IUnknown * pUnkOuter, REFIID riid, LPVOID * ppv)
{
    IDragHelperImpl *dth;

    TRACE ("unkOut=%p %s\n", pUnkOuter, shdebugstr_guid (riid));

    if (!ppv)
        return E_POINTER;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    dth = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof (IDragHelperImpl));
    if (!dth) return E_OUTOFMEMORY;

    dth->ref = 0;
    dth->IDropTargetHelper_iface.lpVtbl = &vt_IDropTargetHelper;
    dth->IDragSourceHelper_iface.lpVtbl = &vt_IDragSourceHelper;

    if (FAILED(IDropTargetHelper_QueryInterface (&dth->IDropTargetHelper_iface, riid, ppv)))
    {
        HeapFree(GetProcessHeap(), 0, dth);
        return E_NOINTERFACE;
    }

    TRACE ("--(%p)\n", dth);
    return S_OK;
}
