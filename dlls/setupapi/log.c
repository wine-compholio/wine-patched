/*
 * SetupAPI logs functions
 *
 * Copyright 2015 Pierre Schweitzer
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

#include "wine/debug.h"
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "setupapi.h"
#include "winnls.h"

static HANDLE setupact = INVALID_HANDLE_VALUE;
static HANDLE setuperr = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION setupapi_cs;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &setupapi_cs,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": setupapi_cs") }
};
static CRITICAL_SECTION setupapi_cs = { &critsect_debug, -1, 0, 0, 0, 0 };

/***********************************************************************
 *      SetupCloseLog(SETUPAPI.@)
 */
void WINAPI SetupCloseLog(void)
{
    EnterCriticalSection(&setupapi_cs);

    if (setupact != INVALID_HANDLE_VALUE)
    {
        CloseHandle(setupact);
        setupact = INVALID_HANDLE_VALUE;
    }

    if (setuperr != INVALID_HANDLE_VALUE)
    {
        CloseHandle(setuperr);
        setuperr = INVALID_HANDLE_VALUE;
    }

    LeaveCriticalSection(&setupapi_cs);
}

/***********************************************************************
 *      SetupLogErrorA(SETUPAPI.@)
 */
BOOL WINAPI SetupLogErrorA(LPCSTR message, LogSeverity severity)
{
    static const CHAR null[] = "(null)";
    DWORD written;
    DWORD len;
    BOOL ret;

    EnterCriticalSection(&setupapi_cs);

    if (setupact == INVALID_HANDLE_VALUE || setuperr == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_FILE_INVALID);
        ret = FALSE;
        goto done;
    }

    if (message == NULL)
        message = null;

    len = lstrlenA(message) * sizeof(CHAR);
    ret = WriteFile(setupact, message, len, &written, NULL);
    if (!ret)
        goto done;

    if (severity >= LogSevMaximum)
    {
        ret = FALSE;
        goto done;
    }

    if (severity > LogSevInformation)
        ret = WriteFile(setuperr, message, len, &written, NULL);

done:
    LeaveCriticalSection(&setupapi_cs);
    return ret;
}

/***********************************************************************
 *      SetupLogErrorW(SETUPAPI.@)
 */
BOOL WINAPI SetupLogErrorW(LPCWSTR message, LogSeverity severity)
{
    LPSTR msg = NULL;
    DWORD len;
    BOOL ret;

    if (message)
    {
        len = WideCharToMultiByte(CP_ACP, 0, message, -1, NULL, 0, NULL, NULL);
        msg = HeapAlloc(GetProcessHeap(), 0, len * sizeof(CHAR));
        if (msg == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        WideCharToMultiByte(CP_ACP, 0, message, -1, msg, len, NULL, NULL);
    }

    /* This is the normal way to proceed. The log files are ASCII files
     * and W is to be converted. */
    ret = SetupLogErrorA(msg, severity);

    if (msg)
        HeapFree(GetProcessHeap(), 0, msg);

    return ret;
}

/***********************************************************************
 *      SetupOpenLog(SETUPAPI.@)
 */
BOOL WINAPI SetupOpenLog(BOOL reserved)
{
    static const WCHAR setupactlog[] = {'\\','s','e','t','u','p','a','c','t','.','l','o','g',0};
    static const WCHAR setuperrlog[] = {'\\','s','e','t','u','p','e','r','r','.','l','o','g',0};
    WCHAR path[MAX_PATH];
    WCHAR win[MAX_PATH];

    EnterCriticalSection(&setupapi_cs);

    if (setupact != INVALID_HANDLE_VALUE && setuperr != INVALID_HANDLE_VALUE)
    {
        LeaveCriticalSection(&setupapi_cs);
        SetLastError(ERROR_ALREADY_INITIALIZED);
        return TRUE;
    }

    GetWindowsDirectoryW(win, MAX_PATH);

    lstrcpyW(path, win);
    lstrcatW(path, setupactlog);
    setupact = CreateFileW(path, FILE_GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (setupact == INVALID_HANDLE_VALUE)
    {
        LeaveCriticalSection(&setupapi_cs);
        return FALSE;
    }
    SetFilePointer(setupact, 0, NULL, FILE_END);

    lstrcpyW(path, win);
    lstrcatW(path, setuperrlog);
    setuperr = CreateFileW(path, FILE_GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (setuperr == INVALID_HANDLE_VALUE)
    {
        CloseHandle(setupact);
        setupact = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&setupapi_cs);
        return FALSE;
    }
    SetFilePointer(setuperr, 0, NULL, FILE_END);

    LeaveCriticalSection(&setupapi_cs);
    SetLastError(ERROR_ALREADY_EXISTS);
    return TRUE;
}
