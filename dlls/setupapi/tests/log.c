/*
 * Unit tests for setupapi.dll log functions
 *
 * Copyright (C) 2015 Pierre Schweitzer
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

#include <stdio.h>
#include <windows.h>
#include <setupapi.h>
#include "wine/test.h"

static void test_SetupLogError(void)
{
    BOOL ret;
    DWORD error;

    SetLastError(0xdeadbeef);
    ret = SetupLogErrorA("Test without opening\r\n", LogSevInformation);
    error = GetLastError();
    ok(!ret, "SetupLogError succeeded\n");
    ok(error == ERROR_FILE_INVALID, "got wrong error: %d\n", error);

    SetLastError(0xdeadbeef);
    ret = SetupOpenLog(FALSE);
    error = GetLastError();
    ok(ret, "SetupOpenLog failed\n");
    ok(error == ERROR_ALREADY_EXISTS, "got wrong error: %d\n", error); /* Even if log file didn't exist */

    SetLastError(0xdeadbeef);
    ret = SetupLogErrorA("Test with wrong log severity\r\n", LogSevMaximum);
    error = GetLastError();
    ok(!ret, "SetupLogError succeeded\n");
    ok(error == 0xdeadbeef, "got wrong error: %d\n", error);
    ret = SetupLogErrorA("Test without EOL", LogSevInformation);
    ok(ret, "SetupLogError failed\n");

    SetLastError(0xdeadbeef);
    ret = SetupLogErrorA(NULL, LogSevInformation);
    ok(ret || broken(!ret && GetLastError() == ERROR_INVALID_PARAMETER /* Win Vista+ */),
        "SetupLogError failed: %08x\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = SetupOpenLog(FALSE);
    error = GetLastError();
    ok(ret, "SetupOpenLog failed\n");
    ok(error == ERROR_ALREADY_INITIALIZED, "got wrong error: %d\n", error);

    SetupCloseLog();
}

START_TEST(log)
{
    test_SetupLogError();
}
