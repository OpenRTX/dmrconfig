/*
 * HID routines for Mac OS X.
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
#include <IOKit/hid/IOHIDManager.h>
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

static volatile IOHIDDeviceRef dev;         // device handle
static unsigned char transfer_buf[42];      // device buffer
static unsigned char receive_buf[42];       // receive buffer
static volatile int nbytes_received = 0;    // receive result
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
    IOReturn result = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, buf, sizeof(buf));
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "HID output error: %d!\n", result);
        exit(-1);
    }

    // Run main application loop until reply received.
    while (nbytes_received == 0) {
        //TODO: timeout
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, 0);
    }

    if (nbytes_received != sizeof(receive_buf)) {
        fprintf(stderr, "Short read: %d bytes instead of %d!\n",
            nbytes_received, (int)sizeof(receive_buf));
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
// Callback: data is received from the HID device
//
static void callback_input(void *context,
    IOReturn result, void *sender, IOHIDReportType type,
    uint32_t reportID, uint8_t *data, CFIndex nbytes)
{
    if (result != kIOReturnSuccess) {
        fprintf(stderr, "HID input error: %d!\n", result);
        exit(-1);
    }

    if (nbytes > sizeof(receive_buf)) {
        fprintf(stderr, "Too large HID input: %d bytes!\n", (int)nbytes);
        exit(-1);
    }

    nbytes_received = nbytes;
    if (nbytes > 0)
        memcpy(receive_buf, data, nbytes);
}

//
// Callback: device specified in the matching dictionary has been added
//
static void callback_open(void *context,
    IOReturn result, void *sender, IOHIDDeviceRef deviceRef)
{
    IOReturn o = IOHIDDeviceOpen(deviceRef, kIOHIDOptionsTypeSeizeDevice);
    if (o != kIOReturnSuccess) {
        fprintf(stderr, "Cannot open HID device!\n");
        exit(-1);
    }

    // Register input callback.
    IOHIDDeviceRegisterInputReportCallback(deviceRef,
        transfer_buf, sizeof(transfer_buf), callback_input, 0);

    dev = deviceRef;
}

//
// Callback: device specified in the matching dictionary has been removed
//
static void callback_close(void *ontext,
    IOReturn result, void *sender, IOHIDDeviceRef deviceRef)
{
    // De-register input callback.
    IOHIDDeviceRegisterInputReportCallback(deviceRef, transfer_buf, sizeof(transfer_buf), NULL, NULL);
}

//
// Launch the IOHIDManager.
//
const char *hid_init(int vid, int pid)
{
    // Create the USB HID Manager.
    IOHIDManagerRef HIDManager = IOHIDManagerCreate(kCFAllocatorDefault,
                                                    kIOHIDOptionsTypeNone);

    // Create an empty matching dictionary for filtering USB devices in our HID manager.
    CFMutableDictionaryRef matchDict = CFDictionaryCreateMutable(kCFAllocatorDefault,
        2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // Specify the USB device manufacturer and product in our matching dictionary.
    CFDictionarySetValue(matchDict, CFSTR(kIOHIDVendorIDKey), CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid));
    CFDictionarySetValue(matchDict, CFSTR(kIOHIDProductIDKey), CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid));

    // Apply the matching to our HID manager.
    IOHIDManagerSetDeviceMatching(HIDManager, matchDict);
    CFRelease(matchDict);

    // The HID manager will use callbacks when specified USB devices are connected/disconnected.
    IOHIDManagerRegisterDeviceMatchingCallback(HIDManager, &callback_open, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(HIDManager, &callback_close, NULL);

    // Add the HID manager to the main run loop
    IOHIDManagerScheduleWithRunLoop(HIDManager, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

    // Open the HID mangager
    IOReturn IOReturn = IOHIDManagerOpen(HIDManager, kIOHIDOptionsTypeNone);
    if (IOReturn != kIOReturnSuccess) {
        if (trace_flag) {
            fprintf(stderr, "Cannot find USB device %04x:%04x\n",
                vid, pid);
        }
        return 0;
    }

    // Run main application loop until device found.
    while (!dev) {
        //TODO: timeout
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, 0);
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
    if (!dev)
        return;

    IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
    dev = 0;
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
