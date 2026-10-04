/*
 * hantek.sys Unix library interface
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

#ifndef __WINE_HANTEK_UNIXLIB_H
#define __WINE_HANTEK_UNIXLIB_H

#include "windef.h"
#include "winternl.h"
#include "wine/unixlib.h"

enum hantek_mode
{
    HANTEK_MODE_USB,
    HANTEK_MODE_TCP,
};

struct hantek_init_params
{
    enum hantek_mode mode;  /* out */
};

struct hantek_ioctl_params
{
    UINT32 io_code;
    void  *in_buf;          /* AssociatedIrp.SystemBuffer */
    UINT32 in_len;
    void  *out_buf;         /* UserBuffer */
    UINT32 out_len;
    ULONG_PTR information;  /* out: IoStatus.Information */
    NTSTATUS status;        /* out: IoStatus.Status */
};

enum unix_funcs
{
    unix_hantek_init,
    unix_hantek_ioctl_usb,
    unix_hantek_ioctl_tcp,
};

#endif
