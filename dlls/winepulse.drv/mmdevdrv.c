/*
 * Copyright 2011-2012 Maarten Lankhorst
 * Copyright 2010-2011 Maarten Lankhorst for CodeWeavers
 * Copyright 2011 Andrew Eikum for CodeWeavers
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
 *
 * Pulseaudio driver support.. hell froze over
 */

#define NONAMELESSUNION
#define COBJMACROS
#include "config.h"
#include <poll.h>
#include <pthread.h>

#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

#include <pulse/pulseaudio.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"
#include "wine/unicode.h"
#include "wine/list.h"

#include "ole2.h"
#include "dshow.h"
#include "dsound.h"
#include "propsys.h"

#include "initguid.h"
#include "ks.h"
#include "ksmedia.h"
#include "mmdeviceapi.h"
#include "audioclient.h"
#include "endpointvolume.h"
#include "audiopolicy.h"

#include "wine/list.h"

#define NULL_PTR_ERR MAKE_HRESULT(SEVERITY_ERROR, FACILITY_WIN32, RPC_X_NULL_REF_POINTER)

WINE_DEFAULT_DEBUG_CHANNEL(pulse);

static const REFERENCE_TIME MinimumPeriod = 30000;
static const REFERENCE_TIME DefaultPeriod = 100000;

static pa_context *pulse_ctx;
static pa_mainloop *pulse_ml;

static HANDLE pulse_thread;
static pthread_mutex_t pulse_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pulse_cond = PTHREAD_COND_INITIALIZER;

/* Mixer format + period times */
static WAVEFORMATEXTENSIBLE pulse_fmt[2];
static REFERENCE_TIME pulse_min_period[2], pulse_def_period[2];

static DWORD pulse_stream_volume;

const WCHAR pulse_keyW[] = {'S','o','f','t','w','a','r','e','\\',
    'W','i','n','e','\\','P','u','l','s','e',0};
const WCHAR pulse_streamW[] = { 'S','t','r','e','a','m','V','o','l',0 };

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HKEY key;
        if (RegOpenKeyW(HKEY_CURRENT_USER, pulse_keyW, &key) == ERROR_SUCCESS) {
            DWORD size = sizeof(pulse_stream_volume);
            RegQueryValueExW(key, pulse_streamW, 0, NULL,
                             (BYTE*)&pulse_stream_volume, &size);
            RegCloseKey(key);
        }
        DisableThreadLibraryCalls(dll);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (pulse_ctx) {
           pa_context_disconnect(pulse_ctx);
           pa_context_unref(pulse_ctx);
        }
        if (pulse_ml)
            pa_mainloop_quit(pulse_ml, 0);
        CloseHandle(pulse_thread);
    }
    return TRUE;
}


static const WCHAR defaultW[] = {'P','u','l','s','e','a','u','d','i','o',0};

/* Following pulseaudio design here, mainloop has the lock taken whenever
 * it is handling something for pulse, and the lock is required whenever
 * doing any pa_* call that can affect the state in any way
 *
 * pa_cond_wait is used when waiting on results, because the mainloop needs
 * the same lock taken to affect the state
 *
 * This is basically the same as the pa_threaded_mainloop implementation,
 * but that cannot be used because it uses pthread_create directly
 *
 * pa_threaded_mainloop_(un)lock -> pthread_mutex_(un)lock
 * pa_threaded_mainloop_signal -> pthread_cond_signal
 * pa_threaded_mainloop_wait -> pthread_cond_wait
 */

static int pulse_poll_func(struct pollfd *ufds, unsigned long nfds, int timeout, void *userdata) {
    int r;
    pthread_mutex_unlock(&pulse_lock);
    r = poll(ufds, nfds, timeout);
    pthread_mutex_lock(&pulse_lock);
    return r;
}

static DWORD CALLBACK pulse_mainloop_thread(void *tmp) {
    int ret;
    pulse_ml = pa_mainloop_new();
    pa_mainloop_set_poll_func(pulse_ml, pulse_poll_func, NULL);
    pthread_mutex_lock(&pulse_lock);
    pthread_cond_signal(&pulse_cond);
    pa_mainloop_run(pulse_ml, &ret);
    pthread_mutex_unlock(&pulse_lock);
    pa_mainloop_free(pulse_ml);
    CloseHandle(pulse_thread);
    return ret;
}

static void pulse_contextcallback(pa_context *c, void *userdata);
static void pulse_stream_state(pa_stream *s, void *user);

static const enum pa_channel_position pulse_pos_from_wfx[] = {
    PA_CHANNEL_POSITION_FRONT_LEFT,
    PA_CHANNEL_POSITION_FRONT_RIGHT,
    PA_CHANNEL_POSITION_FRONT_CENTER,
    PA_CHANNEL_POSITION_LFE,
    PA_CHANNEL_POSITION_REAR_LEFT,
    PA_CHANNEL_POSITION_REAR_RIGHT,
    PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
    PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
    PA_CHANNEL_POSITION_REAR_CENTER,
    PA_CHANNEL_POSITION_SIDE_LEFT,
    PA_CHANNEL_POSITION_SIDE_RIGHT,
    PA_CHANNEL_POSITION_TOP_CENTER,
    PA_CHANNEL_POSITION_TOP_FRONT_LEFT,
    PA_CHANNEL_POSITION_TOP_FRONT_CENTER,
    PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
    PA_CHANNEL_POSITION_TOP_REAR_LEFT,
    PA_CHANNEL_POSITION_TOP_REAR_CENTER,
    PA_CHANNEL_POSITION_TOP_REAR_RIGHT
};

static void pulse_probe_settings(int render, WAVEFORMATEXTENSIBLE *fmt) {
    WAVEFORMATEX *wfx = &fmt->Format;
    pa_stream *stream;
    pa_channel_map map;
    pa_sample_spec ss;
    pa_buffer_attr attr;
    int ret, i;
    unsigned int length = 0;

    pa_channel_map_init_auto(&map, 2, PA_CHANNEL_MAP_ALSA);
    ss.rate = 48000;
    ss.format = PA_SAMPLE_FLOAT32LE;
    ss.channels = map.channels;

    attr.maxlength = -1;
    attr.tlength = -1;
    attr.minreq = attr.fragsize = pa_frame_size(&ss);
    attr.prebuf = 0;

    stream = pa_stream_new(pulse_ctx, "format test stream", &ss, &map);
    if (stream)
        pa_stream_set_state_callback(stream, pulse_stream_state, NULL);
    if (!stream)
        ret = -1;
    else if (render)
        ret = pa_stream_connect_playback(stream, NULL, &attr,
        PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS, NULL, NULL);
    else
        ret = pa_stream_connect_record(stream, NULL, &attr, PA_STREAM_START_CORKED|PA_STREAM_FIX_RATE|PA_STREAM_FIX_CHANNELS|PA_STREAM_EARLY_REQUESTS);
    if (ret >= 0) {
        while (pa_stream_get_state(stream) == PA_STREAM_CREATING)
            pthread_cond_wait(&pulse_cond, &pulse_lock);
        if (pa_stream_get_state(stream) == PA_STREAM_READY) {
            ss = *pa_stream_get_sample_spec(stream);
            map = *pa_stream_get_channel_map(stream);
            if (render)
                length = pa_stream_get_buffer_attr(stream)->minreq;
            else
                length = pa_stream_get_buffer_attr(stream)->fragsize;
            pa_stream_disconnect(stream);
            while (pa_stream_get_state(stream) == PA_STREAM_READY)
                pthread_cond_wait(&pulse_cond, &pulse_lock);
        }
    }
    if (stream)
        pa_stream_unref(stream);
    if (length)
        pulse_def_period[!render] = pulse_min_period[!render] = pa_bytes_to_usec(10 * length, &ss);
    else
        pulse_min_period[!render] = MinimumPeriod;
    if (pulse_def_period[!render] <= DefaultPeriod)
        pulse_def_period[!render] = DefaultPeriod;

    wfx->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx->nChannels = ss.channels;
    wfx->wBitsPerSample = 8 * pa_sample_size_of_format(ss.format);
    wfx->nSamplesPerSec = ss.rate;
    wfx->nBlockAlign = pa_frame_size(&ss);
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    if (ss.format != PA_SAMPLE_S24_32LE)
        fmt->Samples.wValidBitsPerSample = wfx->wBitsPerSample;
    else
        fmt->Samples.wValidBitsPerSample = 24;
    if (ss.format == PA_SAMPLE_FLOAT32LE)
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    else
        fmt->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    fmt->dwChannelMask = 0;
    for (i = 0; i < map.channels; ++i)
        switch (map.map[i]) {
            default: FIXME("Unhandled channel %s\n", pa_channel_position_to_string(map.map[i])); break;
            case PA_CHANNEL_POSITION_FRONT_LEFT: fmt->dwChannelMask |= SPEAKER_FRONT_LEFT; break;
            case PA_CHANNEL_POSITION_MONO:
            case PA_CHANNEL_POSITION_FRONT_CENTER: fmt->dwChannelMask |= SPEAKER_FRONT_CENTER; break;
            case PA_CHANNEL_POSITION_FRONT_RIGHT: fmt->dwChannelMask |= SPEAKER_FRONT_RIGHT; break;
            case PA_CHANNEL_POSITION_REAR_LEFT: fmt->dwChannelMask |= SPEAKER_BACK_LEFT; break;
            case PA_CHANNEL_POSITION_REAR_CENTER: fmt->dwChannelMask |= SPEAKER_BACK_CENTER; break;
            case PA_CHANNEL_POSITION_REAR_RIGHT: fmt->dwChannelMask |= SPEAKER_BACK_RIGHT; break;
            case PA_CHANNEL_POSITION_LFE: fmt->dwChannelMask |= SPEAKER_LOW_FREQUENCY; break;
            case PA_CHANNEL_POSITION_SIDE_LEFT: fmt->dwChannelMask |= SPEAKER_SIDE_LEFT; break;
            case PA_CHANNEL_POSITION_SIDE_RIGHT: fmt->dwChannelMask |= SPEAKER_SIDE_RIGHT; break;
            case PA_CHANNEL_POSITION_TOP_CENTER: fmt->dwChannelMask |= SPEAKER_TOP_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_LEFT: fmt->dwChannelMask |= SPEAKER_TOP_FRONT_LEFT; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_CENTER: fmt->dwChannelMask |= SPEAKER_TOP_FRONT_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT: fmt->dwChannelMask |= SPEAKER_TOP_FRONT_RIGHT; break;
            case PA_CHANNEL_POSITION_TOP_REAR_LEFT: fmt->dwChannelMask |= SPEAKER_TOP_BACK_LEFT; break;
            case PA_CHANNEL_POSITION_TOP_REAR_CENTER: fmt->dwChannelMask |= SPEAKER_TOP_BACK_CENTER; break;
            case PA_CHANNEL_POSITION_TOP_REAR_RIGHT: fmt->dwChannelMask |= SPEAKER_TOP_BACK_RIGHT; break;
        }
}

static HRESULT pulse_connect(void)
{
    int len;
    WCHAR path[PATH_MAX], *name;
    char *str;

    if (!pulse_thread)
    {
        if (!(pulse_thread = CreateThread(NULL, 0, pulse_mainloop_thread, NULL, 0, NULL)))
        {
            ERR("Failed to create mainloop thread.");
            return E_FAIL;
        }
        SetThreadPriority(pulse_thread, THREAD_PRIORITY_TIME_CRITICAL);
        pthread_cond_wait(&pulse_cond, &pulse_lock);
    }

    if (pulse_ctx && PA_CONTEXT_IS_GOOD(pa_context_get_state(pulse_ctx)))
        return S_OK;
    if (pulse_ctx)
        pa_context_unref(pulse_ctx);

    GetModuleFileNameW(NULL, path, sizeof(path)/sizeof(*path));
    name = strrchrW(path, '\\');
    if (!name)
        name = path;
    else
        name++;
    len = WideCharToMultiByte(CP_UNIXCP, 0, name, -1, NULL, 0, NULL, NULL);
    str = pa_xmalloc(len);
    WideCharToMultiByte(CP_UNIXCP, 0, name, -1, str, len, NULL, NULL);
    TRACE("Name: %s\n", str);
    pulse_ctx = pa_context_new(pa_mainloop_get_api(pulse_ml), str);
    pa_xfree(str);
    if (!pulse_ctx) {
        ERR("Failed to create context\n");
        return E_FAIL;
    }

    pa_context_set_state_callback(pulse_ctx, pulse_contextcallback, NULL);

    TRACE("libpulse protocol version: %u. API Version %u\n", pa_context_get_protocol_version(pulse_ctx), PA_API_VERSION);
    if (pa_context_connect(pulse_ctx, NULL, 0, NULL) < 0)
        goto fail;

    /* Wait for connection */
    while (pthread_cond_wait(&pulse_cond, &pulse_lock)) {
        pa_context_state_t state = pa_context_get_state(pulse_ctx);

        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            goto fail;

        if (state == PA_CONTEXT_READY)
            break;
    }

    TRACE("Connected to server %s with protocol version: %i.\n",
        pa_context_get_server(pulse_ctx),
        pa_context_get_server_protocol_version(pulse_ctx));
    pulse_probe_settings(1, &pulse_fmt[0]);
    pulse_probe_settings(0, &pulse_fmt[1]);
    return S_OK;

fail:
    pa_context_unref(pulse_ctx);
    pulse_ctx = NULL;
    return E_FAIL;
}

static void pulse_contextcallback(pa_context *c, void *userdata) {
    switch (pa_context_get_state(c)) {
        default:
            FIXME("Unhandled state: %i\n", pa_context_get_state(c));
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        case PA_CONTEXT_TERMINATED:
            TRACE("State change to %i\n", pa_context_get_state(c));
            return;

        case PA_CONTEXT_READY:
            TRACE("Ready\n");
            break;

        case PA_CONTEXT_FAILED:
            ERR("Context failed: %s\n", pa_strerror(pa_context_errno(c)));
            break;
    }
    pthread_cond_signal(&pulse_cond);
}

static void pulse_stream_state(pa_stream *s, void *user)
{
    pa_stream_state_t state = pa_stream_get_state(s);
    TRACE("Stream state changed to %i\n", state);
    pthread_cond_signal(&pulse_cond);
}

HRESULT WINAPI AUDDRV_GetEndpointIDs(EDataFlow flow, WCHAR ***ids, void ***keys,
        UINT *num, UINT *def_index)
{
    HRESULT hr = S_OK;
    TRACE("%d %p %p %p\n", flow, ids, num, def_index);

    pthread_mutex_lock(&pulse_lock);
    hr = pulse_connect();
    pthread_mutex_unlock(&pulse_lock);
    if (FAILED(hr))
        return hr;
    *num = 1;
    *def_index = 0;

    *ids = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR *));
    if (!*ids)
        return E_OUTOFMEMORY;

    (*ids)[0] = HeapAlloc(GetProcessHeap(), 0, sizeof(defaultW));
    if (!(*ids)[0]) {
        HeapFree(GetProcessHeap(), 0, *ids);
        return E_OUTOFMEMORY;
    }

    lstrcpyW((*ids)[0], defaultW);

    *keys = HeapAlloc(GetProcessHeap(), 0, sizeof(void *));
    (*keys)[0] = NULL;

    return S_OK;
}

int WINAPI AUDDRV_GetPriority(void)
{
    HRESULT hr;
    pthread_mutex_lock(&pulse_lock);
    hr = pulse_connect();
    pthread_mutex_unlock(&pulse_lock);
    return SUCCEEDED(hr) ? 3 : 0;
}

HRESULT WINAPI AUDDRV_GetAudioEndpoint(void *key, IMMDevice *dev,
        EDataFlow dataflow, IAudioClient **out)
{
    TRACE("%p %p %d %p\n", key, dev, dataflow, out);
    if (dataflow != eRender && dataflow != eCapture)
        return E_UNEXPECTED;

    *out = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI AUDDRV_GetAudioSessionManager(IMMDevice *device,
        IAudioSessionManager2 **out)
{
    *out = NULL;
    return E_NOTIMPL;
}
