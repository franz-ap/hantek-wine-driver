/*
 * hantek.sys Unix side (libusb / TCP backend)
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

#if 0
#pragma makedep unix
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libusb.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(hantek);

#define VENDOR_REQUEST 0x40
#define DEVICE_TO_HOST 0x80

#define DEFAULT_PORT 8484

static int sock = -1;
static libusb_device_handle *usbdev;

struct __attribute__((__packed__)) hantekCommand {
    uint32_t io_code;
    uint16_t input_len;
    uint16_t output_len;
};

struct __attribute__((__packed__)) hantekResponse {
    uint8_t ret_val;
    uint16_t input_len;
    uint16_t output_len;
};

/* Like libusb_open_device_with_vid_pid(), but reports why opening failed. */
static libusb_device_handle *open_hantek_device( uint16_t vid, uint16_t pid )
{
    libusb_device **list;
    libusb_device_handle *handle = NULL;
    ssize_t count, i;
    int found = 0, r;

    count = libusb_get_device_list(NULL, &list);
    if (count < 0) {
        ERR("libusb_get_device_list failed: %s\n", libusb_error_name(count));
        return NULL;
    }
    TRACE("libusb sees %zd USB device(s)\n", count);

    for (i = 0; i < count && !handle; i++) {
        struct libusb_device_descriptor desc;
        uint8_t bus = libusb_get_bus_number(list[i]);
        uint8_t addr = libusb_get_device_address(list[i]);

        if ((r = libusb_get_device_descriptor(list[i], &desc)) < 0) {
            WARN("bus %03u device %03u: cannot read descriptor: %s\n", bus, addr, libusb_error_name(r));
            continue;
        }
        TRACE("bus %03u device %03u: %04x:%04x\n", bus, addr, desc.idVendor, desc.idProduct);
        if (desc.idVendor != vid || desc.idProduct != pid) continue;

        found++;
        if ((r = libusb_open(list[i], &handle)) < 0) {
            handle = NULL;
            ERR("cannot open hantek device %04x:%04x at /dev/bus/usb/%03u/%03u: %s (%s)\n",
                vid, pid, bus, addr, libusb_error_name(r), libusb_strerror(r));
            if (r == LIBUSB_ERROR_ACCESS)
                ERR("no permission - add a udev rule, e.g. SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"%04x\", "
                    "ATTRS{idProduct}==\"%04x\", GROUP=\"plugdev\", MODE=\"0664\"\n", vid, pid);
        } else {
            TRACE("opened hantek device %04x:%04x at /dev/bus/usb/%03u/%03u\n", vid, pid, bus, addr);
        }
    }

    if (!found)
        ERR("hantek device %04x:%04x not found (powered on and passed through to the VM?)\n", vid, pid);

    libusb_free_device_list(list, 1);
    return handle;
}

static NTSTATUS hantek_init( void *args )
{
    struct hantek_init_params *params = args;
    struct sockaddr_in serv_addr;
    int hantek_port = DEFAULT_PORT;
    int flag = 1;

    const char* hantek_port_var = getenv("HANTEK_PORT");
    const char* hantek_host_var = getenv("HANTEK_HOST");

    if(hantek_port_var) {
        hantek_port = atoi(hantek_port_var);
    }

    if(!hantek_host_var) {
        const struct libusb_version* version;
        int r;
        version = libusb_get_version();
        TRACE("Using libusb v%d.%d.%d.%d\n", version->major, version->minor, version->micro, version->nano);
        r = libusb_init(NULL);
        if (r < 0) {
            ERR("libusb_init failed: %s (%s)\n", libusb_error_name(r), libusb_strerror(r));
            return STATUS_INVALID_PARAMETER;
        }

        usbdev = open_hantek_device(0x04b5, 0x6cde);

        if (usbdev == NULL) {
            return STATUS_DEVICE_DOES_NOT_EXIST;
        }

        params->mode = HANTEK_MODE_USB;
        return STATUS_SUCCESS;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERR("Socket creation error\n");
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(hantek_port);

    ERR("Hantek connecting to %s:%d\n", hantek_host_var, hantek_port);

    if (inet_pton(AF_INET, hantek_host_var, &serv_addr.sin_addr) <= 0) {
        ERR("Invalid address/ Address not supported %s\n", hantek_host_var);
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        ERR("Connection Failed\n");
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));

    params->mode = HANTEK_MODE_TCP;
    return STATUS_SUCCESS;
}

static NTSTATUS hantek_ioctl_usb( void *args )
{
    struct hantek_ioctl_params *params = args;
    uint8_t *indata = params->in_buf;
    unsigned char *outdata = params->out_buf;
    int requestTimeout = 5000;
    int transferSize = 0;
    int r;

    params->information = 0;
    params->status = STATUS_SUCCESS;

    switch(params->io_code) {
    case 0x222059:
    {
        uint8_t bmRequestType = VENDOR_REQUEST + DEVICE_TO_HOST * (indata[0] == 1);
        uint8_t bRequest = indata[4];
        uint16_t wValue = indata[6] + indata[7] * 0x100;
        uint16_t wIndex = indata[8] + indata[9] * 0x100;

        TRACE( "control transfer\n" );
        r = libusb_control_transfer(usbdev, bmRequestType, bRequest, wValue, wIndex, outdata, (uint16_t)params->out_len, requestTimeout);
        if (r < 0)
            WARN("control transfer type %02x req %02x value %04x index %04x len %u failed: %s\n",
                 bmRequestType, bRequest, wValue, wIndex, params->out_len, libusb_error_name(r));
        if (indata[0]) {
            TRACE("control read reqlen %u readlen %d %s\n", params->out_len, r, wine_dbgstr_an((char *)outdata, r < 0 ? 0 : r));
            if (r > 0) params->information = r;
        }
        break;
    }
    case 0x222051:
        TRACE( "write bulk\n" );
        r = libusb_bulk_transfer(usbdev, 0x02, outdata, params->out_len, &transferSize, requestTimeout);
        TRACE("write bulk reqlen %u writelen %d ret %d\n", params->out_len, transferSize, r);
        if (r < 0)
            WARN("write bulk len %u failed after %d bytes: %s\n", params->out_len, transferSize, libusb_error_name(r));
        break;
    case 0x22204e:
        TRACE( "read bulk\n" );
        r = libusb_bulk_transfer(usbdev, 0x86, outdata, params->out_len, &transferSize, requestTimeout);
        TRACE("read bulk reqlen %u readlen %d ret %d %s\n", params->out_len, transferSize, r, wine_dbgstr_an((char *)outdata, transferSize));
        if (r < 0)
            WARN("read bulk len %u failed after %d bytes: %s\n", params->out_len, transferSize, libusb_error_name(r));
        params->information = transferSize;
        break;
    default:
        FIXME( "unhandled ioctl %08x\n", params->io_code );
    }

    return STATUS_SUCCESS;
}

static NTSTATUS hantek_ioctl_tcp( void *args )
{
    struct hantek_ioctl_params *params = args;
    struct hantekCommand cmd;
    struct hantekResponse res;
    ssize_t valread;

    cmd.io_code = params->io_code;
    cmd.input_len = params->in_len;
    cmd.output_len = params->out_len;

    send(sock, &cmd, sizeof(cmd), 0);
    send(sock, params->in_buf, cmd.input_len, 0);
    send(sock, params->out_buf, cmd.output_len, 0);

    valread = recv(sock, &res, sizeof(res), MSG_WAITALL);
    TRACE("res len: %zd ret: %d in_len: %d, out_len:%d\n", valread, res.ret_val, res.input_len, res.output_len);
    if(valread != sizeof(res)){
        TRACE("short header, len %zd\n", valread);
        return STATUS_PIPE_NOT_AVAILABLE;
    }

    if (res.input_len) {
        valread = recv(sock, params->in_buf, res.input_len, MSG_WAITALL);
        if(valread != res.input_len){
            TRACE("short in_data\n");
            return STATUS_PIPE_NOT_AVAILABLE;
        }
    }

    if (res.output_len) {
        valread = recv(sock, params->out_buf, res.output_len, MSG_WAITALL);
        if(valread != res.output_len){
            TRACE("short out_data len: %zd %s\n", valread, strerror(errno));
            return STATUS_PIPE_NOT_AVAILABLE;
        }
    }

    params->information = res.output_len + res.input_len;
    params->status = res.ret_val;
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
#define X(name) [unix_ ## name] = name
    X(hantek_init),
    X(hantek_ioctl_usb),
    X(hantek_ioctl_tcp),
};
