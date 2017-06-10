/*
 * ntoskrnl.exe testing framework
 *
 * Copyright 2015 Sebastian Lackner
 * Copyright 2015 Michael Müller
 * Copyright 2015 Christian Costa
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

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"

#define WINE_KERNEL
#include "util.h"
#include "test.h"
#include "driver.h"

extern PVOID WINAPI MmGetSystemRoutineAddress(PUNICODE_STRING);

const WCHAR driver_device[] = {'\\','D','e','v','i','c','e',
                               '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};
const WCHAR driver_link[] = {'\\','D','o','s','D','e','v','i','c','e','s',
                             '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};

static void *get_system_routine(const char *name)
{
    UNICODE_STRING name_u;
    ANSI_STRING name_a;
    NTSTATUS status;
    void *ret;

    RtlInitAnsiString(&name_a, name);
    status = RtlAnsiStringToUnicodeString(&name_u, &name_a, TRUE);
    if (status) return NULL;

    ret = MmGetSystemRoutineAddress(&name_u);
    RtlFreeUnicodeString(&name_u);
    return ret;
}

/* In each kernel testcase the following variables are available:
 *
 *   device     - DEVICE_OBJECT used for ioctl
 *   irp        - IRP pointer passed to ioctl
 *   __state    - used internally for test macros
 */

KERNEL_TESTCASE(PsGetCurrentProcessId)
{
    struct test_PsGetCurrentProcessId *test = (void *)&__state->userdata;
    test->pid = (DWORD)(ULONG_PTR)PsGetCurrentProcessId();
    ok(test->pid, "Expected processid to be non zero\n");
    return STATUS_SUCCESS;
}

KERNEL_TESTCASE(PsGetCurrentThread)
{
    PETHREAD thread = PsGetCurrentThread();
    todo_wine ok(thread != NULL, "Expected thread to be non-NULL\n");
    return STATUS_SUCCESS;
}

KERNEL_TESTCASE(NtBuildNumber)
{
    USHORT *pNtBuildNumber;
    ULONG build;

    if (!(pNtBuildNumber = get_system_routine("NtBuildNumber")))
    {
        win_skip("Could not get pointer to NtBuildNumber\n");
        return STATUS_SUCCESS;
    }

    PsGetVersion(NULL, NULL, &build, NULL);
    ok(*pNtBuildNumber == build, "Expected build number %u, got %u\n", build, *pNtBuildNumber);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_Create(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_IoControl(DEVICE_OBJECT *device, IRP *irp)
{
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    struct kernel_test_state *state = irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    ULONG_PTR information = 0;

    if (!state)
    {
        status = STATUS_ACCESS_VIOLATION;
        goto done;
    }

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*state) ||
        stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*state))
    {
        status = STATUS_BUFFER_TOO_SMALL;
        goto done;
    }

    kernel_memset(&state->temp, 0, sizeof(state->temp));
    kernel_memset(&state->output, 0, sizeof(state->output));

#define DECLARE_TEST(name) \
    case WINE_IOCTL_##name: status = test_##name(device, irp, state); break;

    switch (stack->Parameters.DeviceIoControl.IoControlCode)
    {
        DECLARE_TEST(PsGetCurrentProcessId);
        DECLARE_TEST(PsGetCurrentThread);
        DECLARE_TEST(NtBuildNumber);

        default:
            break;
    }

#undef DECLARE_TEST

    kernel_memset(&state->temp, 0, sizeof(state->temp));
    if (status == STATUS_SUCCESS) information = sizeof(*state);

done:
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS WINAPI driver_Close(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID WINAPI driver_Unload(DRIVER_OBJECT *driver)
{
    UNICODE_STRING linkW;

    DbgPrint("unloading driver\n");

    RtlInitUnicodeString(&linkW, driver_link);
    IoDeleteSymbolicLink(&linkW);

    IoDeleteDevice(driver->DeviceObject);
}

NTSTATUS WINAPI DriverEntry(DRIVER_OBJECT *driver, PUNICODE_STRING registry)
{
    UNICODE_STRING nameW, linkW;
    DEVICE_OBJECT *device;
    NTSTATUS status;

    DbgPrint("loading driver\n");

    /* Allow unloading of the driver */
    driver->DriverUnload = driver_Unload;

    /* Set driver functions */
    driver->MajorFunction[IRP_MJ_CREATE]            = driver_Create;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL]    = driver_IoControl;
    driver->MajorFunction[IRP_MJ_CLOSE]             = driver_Close;

    RtlInitUnicodeString(&nameW, driver_device);
    RtlInitUnicodeString(&linkW, driver_link);

    if (!(status = IoCreateDevice(driver, 0, &nameW, FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN, FALSE, &device)))
        status = IoCreateSymbolicLink(&linkW, &nameW);

    return status;
}
