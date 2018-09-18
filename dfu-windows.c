/*
 * DFU routines for Windows.
 *
 * Copyright (C) 2018 Serge Vakulenko, KK6ABQ
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. The name of the author may not be used to endorse or promote products
 *      derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "util.h"

//
// DFU request types.
//
#define PU_VENDOR_REQUEST   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x0805, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define URB_FUNCTION_CLASS_INTERFACE    0x001B

#define VENDOR_DIRECTION_IN             1
#define VENDOR_DIRECTION_OUT            0

enum {
    REQUEST_DETACH      = 0,
    REQUEST_DNLOAD      = 1,
    REQUEST_UPLOAD      = 2,
    REQUEST_GETSTATUS   = 3,
    REQUEST_CLRSTATUS   = 4,
    REQUEST_GETSTATE    = 5,
    REQUEST_ABORT       = 6,
};

enum {
    appIDLE                 = 0,
    appDETACH               = 1,
    dfuIDLE                 = 2,
    dfuDNLOAD_SYNC          = 3,
    dfuDNBUSY               = 4,
    dfuDNLOAD_IDLE          = 5,
    dfuMANIFEST_SYNC        = 6,
    dfuMANIFEST             = 7,
    dfuMANIFEST_WAIT_RESET  = 8,
    dfuUPLOAD_IDLE          = 9,
    dfuERROR                = 10,
};

typedef struct {
    unsigned    status       : 8;
    unsigned    poll_timeout : 24;
    unsigned    state        : 8;
    unsigned    string_index : 8;
} status_t;

typedef struct {
    USHORT Function;
    ULONG Direction;
    UCHAR Request;
    USHORT Value;
    USHORT Index;
    ULONG Length;
} CNTRPIPE_RQ, *PCNTRPIPE_RQ;

static HANDLE dev;
static status_t status;

static int dev_request(int request, int value)
{
    CNTRPIPE_RQ rq;
    DWORD nbytes = 0;

    rq.Function  = URB_FUNCTION_CLASS_INTERFACE;
    rq.Direction = VENDOR_DIRECTION_OUT;
    rq.Request   = request;
    rq.Value     = value;
    rq.Index     = 0;
    rq.Length    = 0;

    if (!DeviceIoControl(dev, PU_VENDOR_REQUEST, &rq, sizeof(rq),
                         NULL, 0, &nbytes, NULL)) {
        return -1;
    }
    return 0;
}

static int dev_write(int request, int value, int length, PBYTE data)
{
    int rqlen = sizeof(CNTRPIPE_RQ) + length;
    PCNTRPIPE_RQ rq = alloca(rqlen);
    DWORD nbytes = 0;

    rq->Function  = URB_FUNCTION_CLASS_INTERFACE;
    rq->Direction = VENDOR_DIRECTION_OUT;
    rq->Request   = request;
    rq->Value     = value;
    rq->Index     = 0;
    rq->Length    = length;

    memcpy(rq + 1, data, length);

    if (!DeviceIoControl(dev, PU_VENDOR_REQUEST, rq, rqlen,
                         NULL, 0, &nbytes, NULL)) {
        return -1;
    }
    return 0;
}

static int dev_read(int request, int value, int length, PBYTE buffer)
{
    CNTRPIPE_RQ rq;
    DWORD nbytes = 0;

    rq.Function  = URB_FUNCTION_CLASS_INTERFACE;
    rq.Direction = VENDOR_DIRECTION_IN;
    rq.Request   = request;
    rq.Value     = value;
    rq.Index     = 0;
    rq.Length    = length;

    if (!DeviceIoControl(dev, PU_VENDOR_REQUEST, &rq, sizeof(rq),
                         buffer, length, &nbytes, NULL)) {
        return -1;
    }
    if (nbytes != length) {
        return -1;
    }
    return 0;
}

static int detach(int timeout)
{
    if (trace_flag) {
        printf("--- Send DETACH\n");
    }
    int error = dev_request(REQUEST_DETACH, timeout);
    return error;
}

static int get_status()
{
    if (trace_flag) {
        printf("--- Send GETSTATUS [6]\n");
    }
    int error = dev_read(REQUEST_GETSTATUS, 0, 6, (unsigned char*)&status);
    if (trace_flag && error >= 0) {
        printf("--- Recv ");
        print_hex((unsigned char*)&status, 6);
        printf("\n");
    }
    return error;
}

static int clear_status()
{
    if (trace_flag) {
        printf("--- Send CLRSTATUS\n");
    }
    int error = dev_request(REQUEST_CLRSTATUS, 0);
    return error;
}

static int get_state(int *pstate)
{
    unsigned char state;

    if (trace_flag) {
        printf("--- Send GETSTATE [1]\n");
    }
    int error = dev_read(REQUEST_GETSTATE, 0, 1, &state);
    *pstate = state;
    if (trace_flag && error >= 0) {
        printf("--- Recv ");
        print_hex(&state, 1);
        printf("\n");
    }
    return error;
}

static int dfu_abort()
{
    if (trace_flag) {
        printf("--- Send ABORT\n");
    }
    int error = dev_request(REQUEST_ABORT, 0);
    return error;
}

static void wait_dfu_idle()
{
    int state, error;

    for (;;) {
        error = get_state(&state);
        if (error < 0) {
            fprintf(stderr, "%s: cannot get state\n", __func__);
            exit(-1);
        }

        switch (state) {
        case dfuIDLE:
            return;

        case appIDLE:
            error = detach(5000);
            break;

        case dfuERROR:
            error = clear_status();
            break;

        case appDETACH:
        case dfuDNBUSY:
        case dfuMANIFEST_WAIT_RESET:
            usleep(100000);
            continue;

        default:
            error = dfu_abort();
            break;
        }

        if (error < 0) {
            fprintf(stderr, "%s: unexpected error in state=%d\n",
                __func__, state);
            exit(-1);
        }
    }
}

static void md380_command(uint8_t a, uint8_t b)
{
    unsigned char cmd[2] = { a, b };

    if (trace_flag) {
        printf("--- Send DNLOAD [2] ");
        print_hex(cmd, 2);
        printf("\n");
    }
    int error = dev_write(REQUEST_DNLOAD, 0, 2, cmd);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command\n", __func__);
        exit(-1);
    }
    get_status();
    usleep(100000);
    wait_dfu_idle();
}

static void set_address(uint32_t address)
{
    unsigned char cmd[5] = { 0x21,
        (uint8_t)address,
        (uint8_t)(address >> 8),
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 24),
    };

    if (trace_flag) {
        printf("--- Send DNLOAD [5] ");
        print_hex(cmd, 5);
        printf("\n");
    }
    int error = dev_write(REQUEST_DNLOAD, 0, 5, cmd);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command\n", __func__);
        exit(-1);
    }
    get_status();
    wait_dfu_idle();
}

static void erase_block(uint32_t address, int progress_flag)
{
    unsigned char cmd[5] = { 0x41,
        (uint8_t)address,
        (uint8_t)(address >> 8),
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 24),
    };

    if (trace_flag) {
        printf("--- Send DNLOAD [5] ");
        print_hex(cmd, 5);
        printf("\n");
    }
    int error = dev_write(REQUEST_DNLOAD, 0, 5, cmd);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command\n", __func__);
        exit(-1);
    }
    get_status();
    wait_dfu_idle();

    if (progress_flag) {
        fprintf(stderr, "#");
        fflush(stderr);
    }
}

static const char *identify()
{
    static uint8_t data[64];

    md380_command(0xa2, 0x01);

    if (trace_flag) {
        printf("--- Send UPLOAD [64]\n");
    }
    int error = dev_read(REQUEST_UPLOAD, 0, 64, data);
    if (error < 0) {
        fprintf(stderr, "%s: cannot read data\n", __func__);
        exit(-1);
    }
    if (trace_flag) {
        printf("--- Recv ");
        print_hex(data, 64);
        printf("\n");
    }
    get_status();
    return (const char*) data;
}

//
// Find path for a device with a given GUID.
// Return the first path found (dynamically allocated).
// Return 0 when no device with such GUID is present.
//
static char *find_path(GUID *guid)
{
    char *path = 0;

    HDEVINFO devinfo = SetupDiGetClassDevs(guid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
    if (devinfo == INVALID_HANDLE_VALUE) {
        printf("Cannot get devinfo!\n");
        return 0;
    }

    // Loop through available devices with a given GUID.
    int index;
    SP_INTERFACE_DEVICE_DATA iface;
    iface.cbSize = sizeof(iface);
    for (index=0; SetupDiEnumDeviceInterfaces(devinfo, NULL, guid, index, &iface); ++index) {

        // Obtain a required size of device detail structure.
        DWORD needed;
        SetupDiGetDeviceInterfaceDetail(devinfo, &iface, NULL, 0, &needed, NULL);

        // Allocate the device detail structure.
        PSP_INTERFACE_DEVICE_DETAIL_DATA detail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)alloca(needed);
        detail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
        SP_DEVINFO_DATA did = { sizeof(SP_DEVINFO_DATA) };

        // Get device information.
        if (SetupDiGetDeviceInterfaceDetail(devinfo, &iface, detail, needed, NULL, &did)) {
            // Use the first device found.
            //printf("Device %d: path %s\n", index, detail->DevicePath);
            path = strdup(detail->DevicePath);
            break;
        }
        printf("Device %d: cannot get path!\n", index);
    }
    SetupDiDestroyDeviceInfoList(devinfo);
    return path;
}

const char *dfu_init(unsigned vid, unsigned pid)
{
    static GUID guid_0483_df11 = { 0x3fe809ab, 0xfb91, 0x4cb5, { 0xa6, 0x43, 0x69, 0x67, 0x0d, 0x52, 0x36, 0x6e } };
    char *path = 0;

    // Find path for device.
    if (vid == 0x0483 && pid == 0xdf11) {
        path = find_path(&guid_0483_df11);
    } else {
        fprintf(stderr, "No guid for vid=%04x, pid=%04x!\n", vid, pid);
        exit(-1);
    }

    if (!path) {
        if (trace_flag) {
            fprintf(stderr, "Cannot find DFU device %04x:%04x\n", vid, pid);
        }
        return 0;
    }

    // Open the device.
    dev = CreateFile(path, GENERIC_WRITE | GENERIC_READ,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (! dev) {
        printf("%s: Cannot open\n", path);
        exit(-1);
    }

    // Deallocate path.
    free(path);
    path = 0;

    // Enter Programming Mode.
    wait_dfu_idle();
    md380_command(0x91, 0x01);

    // Get device identifier in a static buffer.
    const char *ident = identify();

    // Zero address.
    set_address(0x00000000);

    return ident;
}

void dfu_close()
{
    if (dev) {
        CloseHandle(dev);
        dev = 0;
    }
}

void dfu_erase(unsigned start, unsigned finish)
{
    // Enter Programming Mode.
    get_status();
    wait_dfu_idle();
    md380_command(0x91, 0x01);
    usleep(100000);

    if (start == 0) {
        // Erase 256kbytes of configuration memory.
        erase_block(0x00000000, 1);
        erase_block(0x00010000, 1);
        erase_block(0x00020000, 1);
        erase_block(0x00030000, 1);

        if (finish > 256*1024) {
            // Erase 768kbytes of extended configuration memory.
            erase_block(0x00110000, 1);
            erase_block(0x00120000, 1);
            erase_block(0x00130000, 1);
            erase_block(0x00140000, 1);
            erase_block(0x00150000, 1);
            erase_block(0x00160000, 1);
            erase_block(0x00170000, 1);
            erase_block(0x00180000, 1);
            erase_block(0x00190000, 1);
            erase_block(0x001a0000, 1);
            erase_block(0x001b0000, 1);
            erase_block(0x001c0000, 1);
            erase_block(0x001d0000, 1);
        }
    } else {
        // Erase callsign database.
        int addr;

        for (addr=start; addr<finish; addr+=0x00010000) {
            erase_block(addr, (addr & 0x00070000) == 0x00070000);
        }
    }

    // Zero address.
    set_address(0x00000000);
}

void dfu_read_block(int bno, uint8_t *data, int nbytes)
{
    if (bno >= 256 && bno < 2048)
        bno += 832;

    if (trace_flag) {
        printf("--- Send UPLOAD [%d]\n", nbytes);
    }
    int error = dev_read(REQUEST_UPLOAD, bno+2, nbytes, data);
    if (error < 0) {
        fprintf(stderr, "%s: cannot read block %d, nbytes = %d\n",
            __func__, bno, nbytes);
        exit(-1);
    }
    if (trace_flag > 1) {
        printf("--- Recv ");
        print_hex(data, nbytes);
        printf("\n");
    }
    get_status();
}

void dfu_write_block(int bno, uint8_t *data, int nbytes)
{
    if (bno >= 256 && bno < 2048)
        bno += 832;

    if (trace_flag) {
        printf("--- Send DNLOAD [%d] ", nbytes);
        if (trace_flag > 1)
            print_hex(data, nbytes);
        printf("\n");
    }
    int error = dev_write(REQUEST_DNLOAD, bno+2, nbytes, data);
    if (error < 0) {
        fprintf(stderr, "%s: cannot write block %d, nbytes = %d\n",
            __func__, bno, nbytes);
        exit(-1);
    }

    get_status();
    wait_dfu_idle();
}

void dfu_reboot()
{
    unsigned char cmd[2] = { 0x91, 0x05 };

    if (trace_flag) {
        printf("--- Send DNLOAD [2] ");
        print_hex(cmd, 2);
        printf("\n");
    }
    wait_dfu_idle();
    int error = dev_write(REQUEST_DNLOAD, 0, 2, cmd);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command\n", __func__);
        exit(-1);
    }
    get_status();
}
