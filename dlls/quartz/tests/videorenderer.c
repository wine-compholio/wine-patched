/*
 * Unit tests for Video Renderer functions
 *
 * Copyright (C) 2007 Google (Lei Zhang)
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

#include "wine/test.h"
#include "dshow.h"
#include "initguid.h"
#include "d3d9.h"
#include "vmr9.h"

#define QI_SUCCEED(iface, riid, ppv) hr = IUnknown_QueryInterface(iface, &riid, (LPVOID*)&ppv); \
    ok(hr == S_OK, "IUnknown_QueryInterface returned %x\n", hr); \
    ok(ppv != NULL, "Pointer is NULL\n");

#define RELEASE_EXPECT(iface, num) if (iface) { \
    hr = IUnknown_Release((IUnknown*)iface); \
    ok(hr == num, "IUnknown_Release should return %d, got %d\n", num, hr); \
}

static const WCHAR *memchrW( const WCHAR *ptr, WCHAR ch, size_t n )
{
    const WCHAR *end;
    for (end = ptr + n; ptr < end; ptr++) if (*ptr == ch) return ptr;
    return NULL;
}

static void test_query_interface(void)
{
    HRESULT hr;
    IUnknown *pVideoRenderer = NULL;
    IBaseFilter *pBaseFilter = NULL;
    IBasicVideo *pBasicVideo = NULL;
    IDirectDrawVideo *pDirectDrawVideo = NULL;
    IKsPropertySet *pKsPropertySet = NULL;
    IMediaPosition *pMediaPosition = NULL;
    IMediaSeeking *pMediaSeeking = NULL;
    IQualityControl *pQualityControl = NULL;
    IQualProp *pQualProp = NULL;
    IVideoWindow *pVideoWindow = NULL;

    hr = CoCreateInstance(&CLSID_VideoRenderer, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (LPVOID*)&pVideoRenderer);
    ok(hr != S_OK || pVideoRenderer != NULL, "CoCreateInstance returned S_OK, but pVideoRenderer is NULL.\n");
    if (hr != S_OK || !pVideoRenderer)
    {
        skip("VideoRenderer is not available, skipping QI test.\n");
        return;
    }

    QI_SUCCEED(pVideoRenderer, IID_IBaseFilter, pBaseFilter);
    RELEASE_EXPECT(pBaseFilter, 1);
    QI_SUCCEED(pVideoRenderer, IID_IBasicVideo, pBasicVideo);
    RELEASE_EXPECT(pBasicVideo, 1);
    QI_SUCCEED(pVideoRenderer, IID_IMediaSeeking, pMediaSeeking);
    RELEASE_EXPECT(pMediaSeeking, 1);
    QI_SUCCEED(pVideoRenderer, IID_IQualityControl, pQualityControl);
    RELEASE_EXPECT(pQualityControl, 1);
    todo_wine {
    QI_SUCCEED(pVideoRenderer, IID_IDirectDrawVideo, pDirectDrawVideo);
    RELEASE_EXPECT(pDirectDrawVideo, 1);
    QI_SUCCEED(pVideoRenderer, IID_IKsPropertySet, pKsPropertySet);
    RELEASE_EXPECT(pKsPropertySet, 1);
    QI_SUCCEED(pVideoRenderer, IID_IQualProp, pQualProp);
    RELEASE_EXPECT(pQualProp, 1);
    }
    QI_SUCCEED(pVideoRenderer, IID_IMediaPosition, pMediaPosition);
    RELEASE_EXPECT(pMediaPosition, 1);
    QI_SUCCEED(pVideoRenderer, IID_IVideoWindow, pVideoWindow);
    RELEASE_EXPECT(pVideoWindow, 1);

    RELEASE_EXPECT(pVideoRenderer, 0);
}

static void test_pin(IPin *pin)
{
    IMemInputPin *mpin = NULL;

    IPin_QueryInterface(pin, &IID_IMemInputPin, (void **)&mpin);

    ok(mpin != NULL, "No IMemInputPin found!\n");
    if (mpin)
    {
        ok(IMemInputPin_ReceiveCanBlock(mpin) == S_OK, "Receive can't block for pin!\n");
        ok(IMemInputPin_NotifyAllocator(mpin, NULL, 0) == E_POINTER, "NotifyAllocator likes a NULL pointer argument\n");
        IMemInputPin_Release(mpin);
    }
    /* TODO */
}

static void test_basefilter(void)
{
    IUnknown *pVideoRenderer = NULL;
    IEnumPins *pin_enum = NULL;
    IBaseFilter *base = NULL;
    IPin *pins[2];
    ULONG ref;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_VideoRenderer, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (LPVOID*)&pVideoRenderer);
    ok(hr != S_OK || pVideoRenderer != NULL, "CoCreateInstance returned S_OK, but pVideoRenderer is NULL.\n");
    if (hr != S_OK || !pVideoRenderer)
    {
        skip("VideoRenderer is not available, skipping BaseFilter test.\n");
        return;
    }

    IUnknown_QueryInterface(pVideoRenderer, &IID_IBaseFilter, (void **)&base);
    if (base == NULL)
    {
        /* test_query_interface handles this case */
        skip("No IBaseFilter\n");
        goto out;
    }

    hr = IBaseFilter_EnumPins(base, NULL);
    ok(hr == E_POINTER, "hr = %08x and not E_POINTER\n", hr);

    hr= IBaseFilter_EnumPins(base, &pin_enum);
    ok(hr == S_OK, "hr = %08x and not S_OK\n", hr);

    hr = IEnumPins_Next(pin_enum, 1, NULL, NULL);
    ok(hr == E_POINTER, "hr = %08x and not E_POINTER\n", hr);

    hr = IEnumPins_Next(pin_enum, 2, pins, NULL);
    ok(hr == E_INVALIDARG, "hr = %08x and not E_INVALIDARG\n", hr);

    pins[0] = (void *)0xdead;
    pins[1] = (void *)0xdeed;

    hr = IEnumPins_Next(pin_enum, 2, pins, &ref);
    ok(hr == S_FALSE, "hr = %08x instead of S_FALSE\n", hr);
    ok(pins[0] != (void *)0xdead && pins[0] != NULL, "pins[0] = %p\n", pins[0]);
    if (pins[0] != (void *)0xdead && pins[0] != NULL)
    {
        test_pin(pins[0]);
        IPin_Release(pins[0]);
    }

    ok(pins[1] == (void *)0xdeed, "pins[1] = %p\n", pins[1]);

    ref = IEnumPins_Release(pin_enum);
    ok(ref == 0, "ref is %u and not 0!\n", ref);

out:
    if (base) IBaseFilter_Release(base);
    RELEASE_EXPECT(pVideoRenderer, 0);
}

static void test_monitorconfig7(void)
{
    HRESULT hr;
    IUnknown *pVMR7 = NULL;
    IVMRMonitorConfig *pMonitorConfig = NULL;
    VMRGUID guid;
    VMRMONITORINFO info[8];
    DWORD numdev_total, numdev;
    GUID max_guid;
    RECT max_rect;

    hr = CoCreateInstance(&CLSID_VideoMixingRenderer, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (LPVOID*)&pVMR7);
    ok(hr != S_OK || pVMR7 != NULL, "CoCreateInstance returned S_OK, but pVMR7 is NULL.\n");
    if (hr != S_OK || !pVMR7)
    {
        skip("VideoMixingRenderer7 is not available, skipping MonitorConfig7 test.\n");
        return;
    }

    hr = IUnknown_QueryInterface(pVMR7, &IID_IVMRMonitorConfig, (LPVOID*)&pMonitorConfig);
    ok(hr == S_OK, "IUnknown_QueryInterface returned %x.\n", hr);
    ok(pMonitorConfig != NULL, "pMonitorConfig is NULL.\n");
    if (!pMonitorConfig) goto out;

    hr = IVMRMonitorConfig_SetMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "SetMonitor returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig_GetMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "GetMonitor returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig_SetDefaultMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "SetDefaultMonitor returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig_GetDefaultMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "GetDefaultMonitor returned %x, expected E_POINTER.\n", hr);

    memset(&guid, 0, sizeof(guid));
    guid.pGUID = NULL; /* default DirectDraw device */
    hr = IVMRMonitorConfig_SetMonitor(pMonitorConfig, &guid);
    ok(hr == S_OK, "SetMonitor failed with %x.\n", hr);

    memset(&guid, 255, sizeof(guid));
    hr = IVMRMonitorConfig_GetMonitor(pMonitorConfig, &guid);
    ok(hr == S_OK, "GetMonitor failed with %x.\n", hr);
    ok(guid.pGUID == NULL, "GetMonitor returned guid.pGUID = %p, expected NULL.\n", guid.pGUID);

    memset(&guid, 0, sizeof(guid));
    guid.pGUID = NULL; /* default DirectDraw device */
    hr = IVMRMonitorConfig_SetDefaultMonitor(pMonitorConfig, &guid);
    ok(hr == S_OK, "SetDefaultMonitor failed with %x.\n", hr);

    memset(&guid, 255, sizeof(guid));
    hr = IVMRMonitorConfig_GetDefaultMonitor(pMonitorConfig, &guid);
    ok(hr == S_OK, "GetDefaultMonitor failed with %x.\n", hr);
    ok(guid.pGUID == NULL, "GetDefaultMonitor returned guid.pGUID = %p, expected NULL.\n", guid.pGUID);

    hr = IVMRMonitorConfig_GetAvailableMonitors(pMonitorConfig, NULL, 0, NULL);
    ok(hr == E_POINTER, "GetAvailableMonitors returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig_GetAvailableMonitors(pMonitorConfig, info, 0, &numdev_total);
    ok(hr == E_INVALIDARG, "GetAvailableMonitors returned %x, expected E_INVALIDARG.\n", hr);

    numdev_total = 0;
    hr = IVMRMonitorConfig_GetAvailableMonitors(pMonitorConfig, NULL, 0, &numdev_total);
    ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
    ok(numdev_total > 0, "GetAvailableMonitors returned numdev_total = %d, expected > 0.\n", numdev_total);

    /* check if its possible to provide a buffer which is too small for all entries */
    if (numdev_total > 1)
    {
        hr = IVMRMonitorConfig_GetAvailableMonitors(pMonitorConfig, info, 1, &numdev);
        ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
        ok(numdev == 1, "GetAvailableMonitors returned numdev = %d, expected 1.\n", numdev);
    }

    /* don't request information for more monitors than memory available */
    if (numdev_total > sizeof(info)/sizeof(info[0]))
        numdev_total = sizeof(info)/sizeof(info[0]);
    memset(info, 255, sizeof(info));
    hr = IVMRMonitorConfig_GetAvailableMonitors(pMonitorConfig, info, numdev_total, &numdev);
    ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
    ok(numdev == numdev_total, "GetAvailableMonitors returned numdev = %d, expected %d.\n", numdev, numdev_total);

    memset(&max_guid, 255, sizeof(max_guid));
    memset(&max_rect, 255, sizeof(max_rect));

    /* check that result is filled out, we do not check if the values actually make any sense */
    while (numdev--)
    {
        ok(info[numdev].guid.pGUID == NULL || info[numdev].guid.pGUID == &info[numdev].guid.GUID,
                "GetAvailableMonitors returned info[%d].guid.pGUID = %p, expected NULL or %p.\n", numdev, info[numdev].guid.pGUID, &info[numdev].guid.GUID);
        ok(info[numdev].guid.pGUID != &info[numdev].guid.GUID || memcmp(&info[numdev].guid.GUID, &max_guid, sizeof(max_guid)) != 0,
                "GetAvailableMonitors returned info[%d].GUID = {FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF}, expected any other value.\n", numdev);
        ok(memcmp(&info[numdev].rcMonitor, &max_rect, sizeof(max_rect)) != 0,
                "GetAvailableMonitors returned info[%d].rcMonitor = {-1, -1, -1, -1}, expected any other value.\n", numdev);
        ok(info[numdev].hMon != (HMONITOR)0 && info[numdev].hMon != (HMONITOR)-1,
                "GetAvailableMonitors returned info[%d].hMon = %p, expected != 0 and != -1.\n", numdev, info[numdev].hMon);
        ok(info[numdev].dwFlags != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwFlags = -1, expected != -1.\n", numdev);
        ok(memchrW(info[numdev].szDevice, 0, sizeof(info[numdev].szDevice)/sizeof(WCHAR)) != NULL,
                "GetAvailableMonitors returned info[%d].szDevice without null-termination.\n", numdev);
        ok(memchrW(info[numdev].szDescription, 0, sizeof(info[numdev].szDescription)/sizeof(WCHAR)) != NULL,
                "GetAvailableMonitors returned info[%d].szDescription without null-termination.\n", numdev);
    }

out:
    if (pMonitorConfig) IVMRMonitorConfig_Release(pMonitorConfig);
    RELEASE_EXPECT(pVMR7, 0);
}

static void test_monitorconfig9(void)
{
    HRESULT hr;
    IUnknown *pVMR9 = NULL;
    IVMRMonitorConfig9 *pMonitorConfig = NULL;
    UINT uDev;
    VMR9MonitorInfo info[8];
    DWORD numdev_total, numdev;
    RECT max_rect;

    hr = CoCreateInstance(&CLSID_VideoMixingRenderer9, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IUnknown, (LPVOID*)&pVMR9);
    ok(hr != S_OK || pVMR9 != NULL, "CoCreateInstance returned S_OK, but pVMR9 is NULL.\n");
    if (hr != S_OK || !pVMR9)
    {
        skip("VideoMixingRenderer9 is not available, skipping MonitorConfig9 test.\n");
        return;
    }

    hr = IUnknown_QueryInterface(pVMR9, &IID_IVMRMonitorConfig9, (LPVOID*)&pMonitorConfig);
    ok(hr == S_OK, "IUnknown_QueryInterface returned %x.\n", hr);
    ok(pMonitorConfig != NULL, "pMonitorConfig is NULL.\n");
    if (!pMonitorConfig) goto out;

    hr = IVMRMonitorConfig9_GetMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "GetMonitor returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig9_GetDefaultMonitor(pMonitorConfig, NULL);
    ok(hr == E_POINTER, "GetDefaultMonitor returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig9_SetMonitor(pMonitorConfig, 0);
    ok(hr == S_OK, "SetMonitor failed with %x.\n", hr);

    uDev = 0xdeadbeef;
    hr = IVMRMonitorConfig9_GetMonitor(pMonitorConfig, &uDev);
    ok(hr == S_OK, "GetMonitor failed with %x.\n", hr);
    ok(uDev == 0, "GetMonitor returned uDev = %d, expected 0.\n", uDev);

    hr = IVMRMonitorConfig9_SetDefaultMonitor(pMonitorConfig, 0);
    ok(hr == S_OK, "SetDefaultMonitor failed with %x.\n", hr);

    uDev = 0xdeadbeef;
    hr = IVMRMonitorConfig9_GetDefaultMonitor(pMonitorConfig, &uDev);
    ok(hr == S_OK, "GetDefaultMonitor failed with %x.\n", hr);
    ok(uDev == 0, "GetDefaultMonitor returned uDev = %d, expected 0.\n", uDev);

    hr = IVMRMonitorConfig9_GetAvailableMonitors(pMonitorConfig, NULL, 0, NULL);
    ok(hr == E_POINTER, "GetAvailableMonitors returned %x, expected E_POINTER.\n", hr);

    hr = IVMRMonitorConfig9_GetAvailableMonitors(pMonitorConfig, info, 0, &numdev_total);
    ok(hr == E_INVALIDARG, "GetAvailableMonitors returned %x, expected E_INVALIDARG.\n", hr);

    numdev_total = 0;
    hr = IVMRMonitorConfig9_GetAvailableMonitors(pMonitorConfig, NULL, 0, &numdev_total);
    ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
    ok(numdev_total > 0, "GetAvailableMonitors returned numdev_total = %d, expected > 0.\n", numdev_total);

    /* check if its possible to provide a buffer which is too small for all entries */
    if (numdev_total > 1)
    {
        hr = IVMRMonitorConfig9_GetAvailableMonitors(pMonitorConfig, info, 1, &numdev);
        ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
        ok(numdev == 1, "GetAvailableMonitors returned numdev = %d, expected 1.\n", numdev);
    }

    if (numdev_total > sizeof(info)/sizeof(info[0]))
        numdev_total = sizeof(info)/sizeof(info[0]);
    memset(info, 255, sizeof(info));
    hr = IVMRMonitorConfig9_GetAvailableMonitors(pMonitorConfig, info, numdev_total, &numdev);
    ok(hr == S_OK, "GetAvailableMonitors failed with %x.\n", hr);
    ok(numdev == numdev_total, "GetAvailableMonitors returned numdev = %d, expected %d.\n", numdev, numdev_total);

    memset(&max_rect, 255, sizeof(max_rect));

    /* check that result is filled out, we do not check if the values actually make any sense */
    while (numdev--)
    {
        ok(info[numdev].uDevID != (UINT)-1,
                "GetAvailableMonitors returned info[%d].uDevID = -1, expected != -1.\n", numdev);
        ok(memcmp(&info[numdev].rcMonitor, &max_rect, sizeof(max_rect)) != 0,
                "GetAvailableMonitors returned info[%d].rcMonitor = {-1, -1, -1, -1}, expected any other value.\n", numdev);
        ok(info[numdev].hMon != (HMONITOR)0 && info[numdev].hMon != (HMONITOR)-1,
                "GetAvailableMonitors returned info[%d].hMon = %p, expected != 0 and != -1.\n", numdev, info[numdev].hMon);
        ok(info[numdev].dwFlags != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwFlags = -1, expected != -1.\n", numdev);
        ok(memchrW(info[numdev].szDevice, 0, sizeof(info[numdev].szDevice)/sizeof(WCHAR)) != NULL,
                "GetAvailableMonitors returned info[%d].szDevice without null-termination.\n", numdev);
        ok(memchrW(info[numdev].szDescription, 0, sizeof(info[numdev].szDescription)/sizeof(WCHAR)) != NULL,
                "GetAvailableMonitors returned info[%d].szDescription without null-termination.\n", numdev);
        ok(info[numdev].dwVendorId != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwVendorId = -1, expected != -1.\n", numdev);
        ok(info[numdev].dwDeviceId != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwDeviceId = -1, expected != -1.\n", numdev);
        ok(info[numdev].dwSubSysId != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwSubSysId = -1, expected != -1.\n", numdev);
        ok(info[numdev].dwRevision != (DWORD)-1,
                "GetAvailableMonitors returned info[%d].dwRevision = -1, expected != -1.\n", numdev);
    }

out:
    if (pMonitorConfig) IVMRMonitorConfig9_Release(pMonitorConfig);
    RELEASE_EXPECT(pVMR9, 0);
}

START_TEST(videorenderer)
{
    CoInitialize(NULL);

    /* Video Renderer tests */
    test_query_interface();
    test_basefilter();

    /* Video Mixing Renderer 7 tests */
    test_monitorconfig7();

    /* Video Mixing Renderer 9 tests */
    test_monitorconfig9();

    CoUninitialize();
}
