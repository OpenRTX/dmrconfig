/*
 * HID routines for Windows.
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
#include <hidsdi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "util.h"

static const unsigned char CMD_PRG[]   = "\2PROGRA";
static const unsigned char CMD_PRG2[]  = "M\2";
static const unsigned char CMD_ACK[]   = "A";
static const unsigned char CMD_READ[]  = "Raan";
static const unsigned char CMD_WRITE[] = "Waan...";
static const unsigned char CMD_ENDR[]  = "ENDR";
static const unsigned char CMD_ENDW[]  = "ENDW";
static const unsigned char CMD_CWB0[]  = "CWB\4\0\0\0\0";
static const unsigned char CMD_CWB1[]  = "CWB\4\0\1\0\0";

HANDLE dev = INVALID_HANDLE_VALUE;;         // HID device
static unsigned char receive_buf[42];       // receive buffer
static unsigned offset = 0;                 // CWD offset

//
// Send a request to the device.
// Store the reply into the rdata[] array.
// Terminate in case of errors.
//
void hid_send_recv(const unsigned char *data, unsigned nbytes, unsigned char *rdata, unsigned rlength)
{
    unsigned char buf[42];
    unsigned k;
    DWORD nbytes_received;

    memset(buf, 0, sizeof(buf));
    buf[0] = 1;
    buf[1] = 0;
    buf[2] = nbytes;
    buf[3] = nbytes >> 8;
    if (nbytes > 0)
        memcpy(buf+4, data, nbytes);
    nbytes += 4;

    if (trace_flag > 0) {
        fprintf(stderr, "---Send");
        for (k=0; k<nbytes; ++k) {
            if (k != 0 && (k & 15) == 0)
                fprintf(stderr, "\n       ");
            fprintf(stderr, " %02x", buf[k]);
        }
        fprintf(stderr, "\n");
    }
    nbytes_received = 0;
    memset(receive_buf, 0, sizeof(receive_buf));

    // Write to HID device.
    if (!WriteFile(dev, buf, sizeof(buf), NULL, NULL)) {
        fprintf(stderr, "Error %#lx sending to HID device!\n", GetLastError());
        exit(-1);
    }

    // Receive reply.
    if (!ReadFile(dev, receive_buf, sizeof(receive_buf), &nbytes_received, NULL)) {
        fprintf(stderr, "Error %#lx receiving from HID device!\n", GetLastError());
        exit(-1);
    }

    if (nbytes_received != sizeof(receive_buf)) {
        fprintf(stderr, "Short read: %u bytes instead of %u!\n",
            (unsigned)nbytes_received, (unsigned)sizeof(receive_buf));
        exit(-1);
    }
    if (trace_flag > 0) {
        fprintf(stderr, "---Recv");
        for (k=0; k<nbytes_received; ++k) {
            if (k != 0 && (k & 15) == 0)
                fprintf(stderr, "\n       ");
            fprintf(stderr, " %02x", receive_buf[k]);
        }
        fprintf(stderr, "\n");
    }
    if (receive_buf[0] != 3 || receive_buf[1] != 0 || receive_buf[3] != 0) {
        fprintf(stderr, "incorrect reply\n");
        exit(-1);
    }
    if (receive_buf[2] != rlength) {
        fprintf(stderr, "incorrect reply length %d, expected %d\n",
            receive_buf[2], rlength);
        exit(-1);
    }
    memcpy(rdata, receive_buf+4, rlength);
}

//
// Find a HID device with given GUID, vendor ID and product ID.
// Return an opened file descriptor.
//
HANDLE find_hid_device(int vid, int pid)
{
    static GUID guid = { 0x4d1e55b2, 0xf16f, 0x11cf, { 0x88, 0xcb, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 } };
    HANDLE h = INVALID_HANDLE_VALUE;

    HDEVINFO devinfo = SetupDiGetClassDevs(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
    if (devinfo == INVALID_HANDLE_VALUE) {
        printf("Cannot get devinfo!\n");
        return 0;
    }

    // Loop through available devices with a given GUID.
    int index;
    SP_INTERFACE_DEVICE_DATA iface;
    iface.cbSize = sizeof(iface);
    for (index=0; SetupDiEnumDeviceInterfaces(devinfo, NULL, &guid, index, &iface); ++index) {

        // Obtain a required size of device detail structure.
        DWORD needed;
        SetupDiGetDeviceInterfaceDetail(devinfo, &iface, NULL, 0, &needed, NULL);

        // Allocate the device detail structure.
        PSP_INTERFACE_DEVICE_DETAIL_DATA detail = (PSP_INTERFACE_DEVICE_DETAIL_DATA)alloca(needed);
        detail->cbSize = sizeof(SP_INTERFACE_DEVICE_DETAIL_DATA);
        SP_DEVINFO_DATA did = { sizeof(SP_DEVINFO_DATA) };

        // Get device information.
        if (!SetupDiGetDeviceInterfaceDetail(devinfo, &iface, detail, needed, NULL, &did)) {
            printf("Device %d: cannot get path!\n", index);
            continue;
        }
        printf("Device %d: path %s\n", index, detail->DevicePath);

        h = CreateFile(detail->DevicePath, GENERIC_WRITE | GENERIC_READ,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            continue;
        }

        // Get the Vendor ID and Product ID for this device.
        HIDD_ATTRIBUTES attrib;
        attrib.Size = sizeof(HIDD_ATTRIBUTES);
        HidD_GetAttributes(h, &attrib);
        printf("Product/Vendor: %x %x\n", attrib.ProductID, attrib.VendorID);

        // Check the VID/PID.
        if (attrib.VendorID != vid || attrib.ProductID != pid) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
            continue;
        }

        // Required device found.
        break;
    }
    SetupDiDestroyDeviceInfoList(devinfo);
    return h;
}

//
// Open the radio in programming mode.
//
const char *hid_init(int vid, int pid)
{
    // Find HID device.
    dev = find_hid_device(vid, pid);
    if (dev == INVALID_HANDLE_VALUE) {
        if (trace_flag) {
            fprintf(stderr, "Cannot find HID device %04x:%04x\n", vid, pid);
        }
        return 0;
    }

    static unsigned char reply[38];
    unsigned char ack;

    hid_send_recv(CMD_PRG, 7, &ack, 1);
    if (ack != CMD_ACK[0]) {
        fprintf(stderr, "%s: Wrong PRD acknowledge %#x, expected %#x\n",
            __func__, ack, CMD_ACK[0]);
        return 0;
    }

    hid_send_recv(CMD_PRG2, 2, reply, 16);

    hid_send_recv(CMD_ACK, 1, &ack, 1);
    if (ack != CMD_ACK[0]) {
        fprintf(stderr, "%s: Wrong PRG2 acknowledge %#x, expected %#x\n",
            __func__, ack, CMD_ACK[0]);
        return 0;
    }

    // Reply:
    // 42 46 2d 35 52 ff ff ff 56 32 31 30 00 04 80 04
    //  B  F  -  5  R           V  2  1  0

    // Terminate the string.
    char *p = memchr(reply, 0xff, sizeof(reply));
    if (p)
        *p = 0;
    return (char*)reply;
}

//
// Close HID device.
//
void hid_close()
{
    if (dev != INVALID_HANDLE_VALUE) {
        CloseHandle(dev);
        dev = INVALID_HANDLE_VALUE;
    }
}

void hid_read_block(int bno, uint8_t *data, int nbytes)
{
    unsigned addr = bno * nbytes;
    unsigned char ack, cmd[4], reply[32+4];
    int n;

    if (addr < 0x10000 && offset != 0) {
        offset = 0;
        hid_send_recv(CMD_CWB0, 8, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    } else if (addr >= 0x10000 && offset == 0) {
        offset = 0x00010000;
        hid_send_recv(CMD_CWB1, 8, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    }

    for (n=0; n<nbytes; n+=32) {
        cmd[0] = CMD_READ[0];
        cmd[1] = (addr + n) >> 8;
        cmd[2] = addr + n;
        cmd[3] = 32;
        hid_send_recv(cmd, 4, reply, sizeof(reply));
        memcpy(data + n, reply + 4, 32);
    }
}

void hid_write_block(int bno, uint8_t *data, int nbytes)
{
    unsigned addr = bno * nbytes;
    unsigned char ack, cmd[4+32];
    int n;

    if (addr < 0x10000 && offset != 0) {
        offset = 0;
        hid_send_recv(CMD_CWB0, 8, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    } else if (addr >= 0x10000 && offset == 0) {
        offset = 0x00010000;
        hid_send_recv(CMD_CWB1, 8, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    }

    for (n=0; n<nbytes; n+=32) {
        cmd[0] = CMD_WRITE[0];
        cmd[1] = (addr + n) >> 8;
        cmd[2] = addr + n;
        cmd[3] = 32;
        memcpy(cmd + 4, data + n, 32);
        hid_send_recv(cmd, 4+32, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    }
}

void hid_read_finish()
{
    unsigned char ack;

    hid_send_recv(CMD_ENDR, 4, &ack, 1);
    if (ack != CMD_ACK[0]) {
        fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
            __func__, ack, CMD_ACK[0]);
    }
}

void hid_write_finish()
{
    unsigned char ack;

    hid_send_recv(CMD_ENDW, 4, &ack, 1);
    if (ack != CMD_ACK[0]) {
        fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
            __func__, ack, CMD_ACK[0]);
    }
}
