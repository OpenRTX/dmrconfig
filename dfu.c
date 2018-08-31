/*
 * DFU routines.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include "util.h"

//
// USB request types.
//
#define REQUEST_TYPE_TO_HOST    0xA1
#define REQUEST_TYPE_TO_DEVICE  0x21

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
    statusOK        = 0,
    errTARGET       = 0x01,
    errFILE         = 0x02,
    errWRITE        = 0x03,
    errERASE        = 0x04,
    errCHECK_ERASED = 0x05,
    erPROG          = 0x06,
    errVERIFY       = 0x07,
    errADDRESS      = 0x08,
    errNOTDONE      = 0x09,
    errFIRMWARE     = 0x0A,
    errVENDOR       = 0x0B,
    errUSBR         = 0x0C,
    errPOR          = 0x0D,
    errUNKNOWN      = 0x0E,
    errSTALLEDPK    = 0x0F,
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

enum {
    USB_OK              = 0,
    USB_IO              = -1,
    USB_INVALID_PARAM   = -2,
    USB_ACCESS          = -3,
    USB_NO_DEVICE       = -4,
    USB_NOT_FOUND       = -5,
    USB_BUSY            = -6,
    USB_TIMEOUT         = -7,
    USB_OVERFLOW        = -8,
    USB_PIPE            = -9,
    USB_INTERRUPTED     = -10,
    USB_NO_MEM          = -11,
    USB_NOT_SUPPORTED   = -12,
    USB_OTHER           = -99
};

typedef struct {
    unsigned    status       : 8;
    unsigned    poll_timeout : 24;
    unsigned    state        : 8;
    unsigned    string_index : 8;
} status_t;

static libusb_context *ctx = NULL;
static libusb_device_handle *dev;
static status_t status;

static int detach(int timeout)
{
    if (trace_flag) {
        printf("--- Send DETACH\n");
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_DETACH, timeout, 0, NULL, 0, 0);
    return error;
}

static int get_status()
{
    if (trace_flag) {
        printf("--- Send GETSTATUS [6]\n");
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_HOST,
        REQUEST_GETSTATUS, 0, 0, (unsigned char*)&status, 6, 0);
    if (trace_flag && error == USB_OK) {
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
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_CLRSTATUS, 0, 0, NULL, 0, 0);
    return error;
}

static int get_state(int *pstate)
{
    unsigned char state;

    if (trace_flag) {
        printf("--- Send GETSTATE [1]\n");
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_HOST,
        REQUEST_GETSTATE, 0, 0, &state, 1, 0);
    *pstate = state;
    if (trace_flag && error == USB_OK) {
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
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_ABORT, 0, 0, NULL, 0, 0);
    return error;
}

static void wait_dfu_idle()
{
    int state, error;

    for (;;) {
        error = get_state(&state);
        if (error < 0) {
            fprintf(stderr, "%s: cannot get state: %d: %s\n",
                __func__, error, libusb_strerror(error));
            exit(-1);
        }

        switch (state) {
        case dfuIDLE:
            return;

        case appIDLE:
            error = detach(1000);
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
            fprintf(stderr, "%s: unexpected usb error in state=%d: %d: %s\n",
                __func__, state, error, libusb_strerror(error));
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
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_DNLOAD, 0, 0, cmd, 2, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command: %d: %s\n",
            __func__, error, libusb_strerror(error));
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
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_DNLOAD, 0, 0, cmd, 5, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command: %d: %s\n",
            __func__, error, libusb_strerror(error));
        exit(-1);
    }
    get_status();
    wait_dfu_idle();
}

static void erase_block(uint32_t address)
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
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_DNLOAD, 0, 0, cmd, 5, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot send command: %d: %s\n",
            __func__, error, libusb_strerror(error));
        exit(-1);
    }
    get_status();
    wait_dfu_idle();
}

static const char *identify()
{
    static uint8_t data[64];

    md380_command(0xa2, 0x01);

    if (trace_flag) {
        printf("--- Send UPLOAD [64]\n");
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_HOST,
        REQUEST_UPLOAD, 0, 0, data, 64, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot read data: %d: %s\n",
            __func__, error, libusb_strerror(error));
        exit(-1);
    }
    if (trace_flag && error == USB_OK) {
        printf("--- Recv ");
        print_hex(data, 64);
        printf("\n");
    }
    get_status();
    return (const char*) data;
}

const char *dfu_init(unsigned vid, unsigned pid)
{
    int error = libusb_init(&ctx);
    if (error < 0) {
        fprintf(stderr, "libusb init failed: %d: %s\n",
            error, libusb_strerror(error));
        exit(-1);
    }

    dev = libusb_open_device_with_vid_pid(ctx, vid, pid);
    if (!dev) {
        fprintf(stderr, "Cannot find USB device %04x:%04x\n",
            vid, pid);
        libusb_exit(ctx);
        ctx = 0;
        exit(-1);
    }
    if (libusb_kernel_driver_active(dev, 0)) {
        libusb_detach_kernel_driver(dev, 0);
    }

    error = libusb_claim_interface(dev, 0);
    if (error < 0) {
        fprintf(stderr, "Failed to claim USB interface: %d: %s\n",
            error, libusb_strerror(error));
        libusb_close(dev);
        libusb_exit(ctx);
        ctx = 0;
        exit(-1);
    }

    wait_dfu_idle();

    // Enter Programming Mode.
    md380_command(0x91, 0x01);

    // Get device identifier in a static buffer.
    const char *ident = identify();

    // Zero address.
    set_address(0x00000000);

    return ident;
}

void dfu_close()
{
    if (ctx) {
        libusb_release_interface(dev, 0);
        libusb_close(dev);
        libusb_exit(ctx);
        ctx = 0;
    }
}

void dfu_erase(int nbytes)
{
    // Enter Programming Mode.
    md380_command(0x91, 0x01);
    usleep(100000);

    erase_block(0x00000000);
    erase_block(0x00010000);
    erase_block(0x00020000);
    erase_block(0x00030000);

    if (nbytes > 256*1024) {
        erase_block(0x00110000);
        erase_block(0x00120000);
        erase_block(0x00130000);
        erase_block(0x00140000);
        erase_block(0x00150000);
        erase_block(0x00160000);
        erase_block(0x00170000);
        erase_block(0x00180000);
        erase_block(0x00190000);
        erase_block(0x001a0000);
        erase_block(0x001b0000);
        erase_block(0x001c0000);
        erase_block(0x001d0000);
    }

    // Zero address.
    set_address(0x00000000);
}

void dfu_read_block(int bno, uint8_t *data, int nbytes)
{
    if (bno >= 256)
        bno += 832;

    if (trace_flag) {
        printf("--- Send UPLOAD [%d]\n", nbytes);
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_HOST,
        REQUEST_UPLOAD, bno+2, 0, data, nbytes, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot read block %d, nbytes = %d: %d: %s\n",
            __func__, bno, nbytes, error, libusb_strerror(error));
        exit(-1);
    }
    if (trace_flag && error == USB_OK) {
        printf("--- Recv ");
        print_hex(data, nbytes);
        printf("\n");
    }
    get_status();
}

void dfu_write_block(int bno, uint8_t *data, int nbytes)
{
    if (bno >= 256)
        bno += 832;

    if (trace_flag) {
        printf("--- Send DNLOAD [%d] ", nbytes);
        print_hex(data, nbytes);
        printf("\n");
    }
    int error = libusb_control_transfer(dev, REQUEST_TYPE_TO_DEVICE,
        REQUEST_DNLOAD, bno+2, 0, data, nbytes, 0);
    if (error < 0) {
        fprintf(stderr, "%s: cannot write block %d, nbytes = %d: %d: %s\n",
            __func__, bno, nbytes, error, libusb_strerror(error));
        exit(-1);
    }

    get_status();
    wait_dfu_idle();
}
