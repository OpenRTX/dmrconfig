/*
 * HID routines, OS independent.
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

static unsigned offset = 0;                 // CWD offset

//
// Query and return the device identification string.
//
const char *hid_identify()
{
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

void hid_read_block(int bno, unsigned char *data, int nbytes)
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

void hid_write_block(int bno, unsigned char *data, int nbytes)
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
