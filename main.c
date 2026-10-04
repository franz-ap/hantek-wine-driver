/*
 * hantek.sys
 *
 * Copyright 2022 Daniel Kucera
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

/*
 * Since Wine 7/8 drivers are real PE modules, so libusb and socket calls
 * live in the Unix library (unixlib.c) and are reached via WINE_UNIX_CALL.
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "ddk/wdm.h"
#include "wine/debug.h"
#include "winioctl.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(hantek);

static NTSTATUS hantek_ioctl( IRP *irp, enum unix_funcs func )
{
    IO_STACK_LOCATION *irpsp = IoGetCurrentIrpStackLocation( irp );
    struct hantek_ioctl_params params;
    NTSTATUS status;

    TRACE( "ioctl %lx insize %lu outsize %lu\n",
           irpsp->Parameters.DeviceIoControl.IoControlCode,
           irpsp->Parameters.DeviceIoControl.InputBufferLength,
           irpsp->Parameters.DeviceIoControl.OutputBufferLength );
    TRACE("outdata %s\n", wine_dbgstr_an(irp->UserBuffer, irpsp->Parameters.DeviceIoControl.OutputBufferLength));
    TRACE("indata %s\n", wine_dbgstr_an(irp->AssociatedIrp.SystemBuffer, irpsp->Parameters.DeviceIoControl.InputBufferLength));

    params.io_code = irpsp->Parameters.DeviceIoControl.IoControlCode;
    params.in_buf  = irp->AssociatedIrp.SystemBuffer;
    params.in_len  = irpsp->Parameters.DeviceIoControl.InputBufferLength;
    params.out_buf = irp->UserBuffer;
    params.out_len = irpsp->Parameters.DeviceIoControl.OutputBufferLength;
    params.information = 0;
    params.status = STATUS_SUCCESS;

    status = WINE_UNIX_CALL( func, &params );
    if (status)
    {
        params.information = 0;
        params.status = status;
    }

    irp->IoStatus.Information = params.information;
    irp->IoStatus.Status = params.status;

    TRACE( "------ IOCTL COMPLETE --------\n" );
    IoCompleteRequest( irp, IO_NO_INCREMENT );
    return status ? status : STATUS_SUCCESS;
}

static NTSTATUS WINAPI hantek_ioctl_usb( DEVICE_OBJECT *device, IRP *irp )
{
    return hantek_ioctl( irp, unix_hantek_ioctl_usb );
}

static NTSTATUS WINAPI hantek_ioctl_tcp( DEVICE_OBJECT *device, IRP *irp )
{
    return hantek_ioctl( irp, unix_hantek_ioctl_tcp );
}

NTSTATUS WINAPI DriverEntry( DRIVER_OBJECT *driver, UNICODE_STRING *path )
{
    struct hantek_init_params init_params;
    NTSTATUS status;
    UNICODE_STRING nameW, dos_nameW;
    DEVICE_OBJECT *device;

    // d6CDE-0
    static const WCHAR hantek_deviceW[] = {'\\','D','e','v','i','c','e','\\','d','6','C','D','E','-','0',0};
    static const WCHAR hantek_dos_deviceW[] = {'\\','D','o','s','D','e','v','i','c','e','s','\\','d','6','C','D','E','-','0',0};

    if ((status = __wine_init_unix_call()))
    {
        ERR( "Failed to initialize Unix library, status %#lx.\n", status );
        return status;
    }

    if (WINE_UNIX_CALL( unix_hantek_init, &init_params )) {
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    if (init_params.mode == HANTEK_MODE_USB)
        driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = hantek_ioctl_usb;
    else
        driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = hantek_ioctl_tcp;

    TRACE( "(%p, %s) hantek\n", driver, debugstr_w(path->Buffer) );
    TRACE( "create device hantek (%s) dos device (%s)\n", debugstr_w(hantek_deviceW), debugstr_w(hantek_dos_deviceW));

    RtlInitUnicodeString( &nameW, hantek_deviceW );
    RtlInitUnicodeString( &dos_nameW, hantek_dos_deviceW );

    status = IoCreateDevice( driver, 0, &nameW, FILE_DEVICE_DISK, 0, FALSE, &device );
    if (status) {
        FIXME( "failed to create device %lx\n", status );
        return status;
    }

    status = IoCreateSymbolicLink( &dos_nameW, &nameW );
    if (status) {
        FIXME( "failed to create symlink %lx\n", status );
        return status;
    }

    return STATUS_SUCCESS;
}
