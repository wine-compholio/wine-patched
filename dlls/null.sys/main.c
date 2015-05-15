/*
 * null.sys
 *
 * Copyright 2015 Qian Hong for CodeWeavers
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

#define NONAMELESSUNION
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "ddk/wdm.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(null);

static NTSTATUS WINAPI null_ioctl( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );
    ULONG code = irpsp->Parameters.DeviceIoControl.IoControlCode;

    FIXME("Unsupported ioctl %x (device=%x access=%x func=%x method=%x)\n",
        code, code >> 16, (code >> 14) & 3, (code >> 2) & 0xfff, code & 3);
    irp->IoStatus.u.Status = STATUS_NOT_SUPPORTED;

    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI null_read( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );

    TRACE( "length %u key %u byteoffset %u\n",
           irpsp->Parameters.Read.Length,
           irpsp->Parameters.Read.Key,
           irpsp->Parameters.Read.ByteOffset.u.LowPart);

    irp->IoStatus.u.Status = STATUS_END_OF_FILE;

    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_END_OF_FILE;
}

static NTSTATUS WINAPI null_write( DEVICE_OBJECT *device, IRP *irp )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );

    TRACE( "length %u key %u byteoffset %u\n",
           irpsp->Parameters.Read.Length,
           irpsp->Parameters.Read.Key,
           irpsp->Parameters.Read.ByteOffset.u.LowPart);

    irp->IoStatus.Information = irpsp->Parameters.Read.Length;
    irp->IoStatus.u.Status = STATUS_SUCCESS;

    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI DriverEntry(DRIVER_OBJECT *driver, UNICODE_STRING *path)
{
    static const WCHAR device_nullW[] = {'\\','D','e','v','i','c','e','\\','N','u','l','l',0};
    UNICODE_STRING nameW;
    DEVICE_OBJECT *device;
    NTSTATUS status;

    TRACE("(%p, %s)\n", driver, debugstr_w(path->Buffer));

    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = null_ioctl;
    driver->MajorFunction[IRP_MJ_READ]           = null_read;
    driver->MajorFunction[IRP_MJ_WRITE]          = null_write;

    RtlInitUnicodeString( &nameW, device_nullW );

    status = IoCreateDevice( driver, 0, &nameW, 0, 0, FALSE, &device );
    if (status)
    {
        FIXME( "failed to create device error %x\n", status );
        return status;
    }

    return STATUS_SUCCESS;
}
