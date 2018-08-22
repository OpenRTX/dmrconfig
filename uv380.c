/*
 * Interface to TYT MD-UV380.
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include "radio.h"
#include "util.h"

#define NCHAN           1000
#define NZONES          10

#define MEMSZ           0xd0000
#define OFFSET_CHANNELS 0x40000
#define OFFSET_ZONES    0x149e0

static const char *POWER_NAME[] = { "Low", "High" };

//
// Print a generic information about the device.
//
static void uv380_print_version(FILE *out)
{
    // Nothing to print.
}

//
// Read block of data, up to 64 bytes.
// When start==0, return non-zero on success or 0 when empty.
// When start!=0, halt the program on any error.
//
static int read_block(int fd, int start, unsigned char *data, int nbytes)
{
    unsigned char reply;
    int len;

    // Read data.
    len = serial_read(fd, data, nbytes);
    if (len != nbytes) {
        if (start == 0)
            return 0;
        fprintf(stderr, "Reading block 0x%04x: got only %d bytes.\n", start, len);
        exit(-1);
    }

    // Get acknowledge.
    serial_write(fd, "\x06", 1);
    if (serial_read(fd, &reply, 1) != 1) {
        fprintf(stderr, "No acknowledge after block 0x%04x.\n", start);
        exit(-1);
    }
    if (reply != 0x06) {
        fprintf(stderr, "Bad acknowledge after block 0x%04x: %02x\n", start, reply);
        exit(-1);
    }
    if (serial_verbose) {
        printf("# Read 0x%04x: ", start);
        print_hex(data, nbytes);
        printf("\n");
    } else {
        ++radio_progress;
        if (radio_progress % 16 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
    return 1;
}

//
// Write block of data, up to 64 bytes.
// Halt the program on any error.
// Return 0 on error.
//
static int write_block(int fd, int start, const unsigned char *data, int nbytes)
{
    unsigned char reply[64];
    int len;

    serial_write(fd, data, nbytes);

    // Get echo.
    len = serial_read(fd, reply, nbytes);
    if (len != nbytes) {
        fprintf(stderr, "! Echo for block 0x%04x: got only %d bytes.\n", start, len);
        return 0;
    }

    // Get acknowledge.
    if (serial_read(fd, reply, 1) != 1) {
        fprintf(stderr, "! No acknowledge after block 0x%04x.\n", start);
        return 0;
    }
    if (reply[0] != 0x06) {
        fprintf(stderr, "! Bad acknowledge after block 0x%04x: %02x\n", start, reply[0]);
        return 0;
    }
    if (serial_verbose) {
        printf("# Write 0x%04x: ", start);
        print_hex(data, nbytes);
        printf("\n");
    } else {
        ++radio_progress;
        if (radio_progress % 16 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
    return 1;
}

//
// Read memory image from the device.
//
static void uv380_download()
{
    int addr;

    // Wait for the first 8 bytes.
    while (read_block(radio_port, 0, &radio_mem[0], 8) == 0)
        continue;

    // Get the rest of data.
    for (addr=8; addr<MEMSZ; addr+=64)
        read_block(radio_port, addr, &radio_mem[addr], 64);

    // Get the checksum.
    read_block(radio_port, MEMSZ, &radio_mem[MEMSZ], 1);
}

//
// Write memory image to the device.
//
static void uv380_upload(int cont_flag)
{
    int addr;
    char buf[80];

    if (! fgets(buf, sizeof(buf), stdin))
	/*ignore*/;
    fprintf(stderr, "Sending data... ");
    fflush(stderr);

    if (! write_block(radio_port, 0, &radio_mem[0], 8)) {
        //TODO
    }
    for (addr=8; addr<MEMSZ; addr+=64) {
        if (! write_block(radio_port, addr, &radio_mem[addr], 64)) {
            //TODO
        }
    }
}

//
// Check whether the memory image is compatible with this device.
//
static int uv380_is_compatible()
{
    return strncmp("AH017$", (char*)&radio_mem[0], 6) == 0;
}

#if 0
//
// Is this zone non-empty?
//
static int have_zone(int b)
{
    unsigned char *data = &radio_mem[OFFSET_ZONES + b * 0x80];
    int c;

    for (c=0; c<NCHAN/8; c++) {
        if (data[c] != 0)
            return 1;
    }
    return 0;
}

//
// Print one line of Zones table.
//
static void print_zone(FILE *out, int i)
{
    uint8_t *data  = &radio_mem[OFFSET_ZONES + i * 0x80];
    int      last  = -1;
    int      range = 0;
    int      n;

    fprintf(out, "%4d    ", i + 1);
    for (n=0; n<NCHAN; n++) {
        int cnum = n + 1;
        int chan_in_zone = data[n/8] & (1 << (n & 7));

        if (!chan_in_zone)
            continue;

        if (cnum == last+1) {
            range = 1;
        } else {
            if (range) {
                fprintf(out, "-%d", last);
                range = 0;
            }
            if (last >= 0)
                fprintf(out, ",");
            fprintf(out, "%d", cnum);
        }
        last = cnum;
    }
    if (range)
        fprintf(out, "-%d", last);
    fprintf(out, "\n");
}
#endif

//
// Set the bitmask of zones for a given channel.
// Return 0 on failure.
//
static void setup_zone(int zone_index, int chan_index)
{
    uint8_t *data = &radio_mem[OFFSET_ZONES + zone_index*0x80 + chan_index/8];

    *data |= 1 << (chan_index & 7);
}

//
// Data structure for a channel.
//                                       Sc Gr
//          0  1  2  3  4  5  6--7  8  9 10 11 12 13 14 15
// 040000  62 14 00 c0 24 c0 01 00 04 00 00 00 01 01 00 03  b...$...........
//         16-------19 20-------23 24-25 26-27 28 29 30-31
// 040010  00 25 11 43 00 25 11 43 ff ff ff ff 00 00 fc ff  .%.C.%.C........
//         32---------------------------------------------
// 040020  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 31 00  C.h.a.n.n.e.l.1.
//         ---------------------------------------------63
// 040030  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040040  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 040050  00 25 22 43 00 25 22 43 ff ff ff ff 00 00 ff ff  .%"C.%"C........
// 040060  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 32 00  C.h.a.n.n.e.l.2.
// 040070  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040080  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 040090  00 25 33 43 00 25 33 43 ff ff ff ff 00 00 ff ff  .%3C.%3C........
// 0400a0  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 33 00  C.h.a.n.n.e.l.3.
// 0400b0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 0400c0  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 0400d0  00 25 44 43 00 25 44 43 ff ff ff ff 00 00 ff ff  .%DC.%DC........
// 0400e0  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 34 00  C.h.a.n.n.e.l.4.
// 0400f0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040100  61 14 00 c0 24 c0 00 00 04 00 00 00 00 00 00 03  a...$...........
// 040110  00 25 55 43 00 25 55 43 25 08 25 08 00 00 ff ff  .%UC.%UC%.%.....
// 040120  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 35 00  C.h.a.n.n.e.l.5.
// 040130  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040140  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 040150  00 25 11 14 00 25 11 14 ff ff ff ff 00 00 ff ff  .%...%..........
// 040160  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 36 00  C.h.a.n.n.e.l.6.
// 040170  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040180  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 040190  00 25 22 14 00 25 22 14 ff ff ff ff 00 00 ff ff  .%"..%".........
// 0401a0  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 37 00  C.h.a.n.n.e.l.7.
// 0401b0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 0401c0  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 0401d0  00 25 33 14 00 25 33 14 ff ff ff ff 00 00 ff ff  .%3..%3.........
// 0401e0  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 38 00  C.h.a.n.n.e.l.8.
// 0401f0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040200  62 14 00 e0 24 c0 01 00 04 00 00 00 01 00 00 03  b...$...........
// 040210  00 25 44 14 00 25 44 14 ff ff ff ff 00 00 ff ff  .%D..%D.........
// 040220  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 39 00  C.h.a.n.n.e.l.9.
// 040230  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
//
// 040240  61 14 00 c0 24 c0 00 00 04 00 00 00 00 00 00 03  a...$...........
// 040250  00 25 55 14 00 25 55 14 25 08 25 08 00 00 ff ff  .%U..%U.%.%.....
// 040260  43 00 68 00 61 00 6e 00 6e 00 65 00 6c 00 31 00  C.h.a.n.n.e.l.1.
// 040270  30 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  0...............
//
//                                       Sc Gr
//          0  1  2  3  4  5  6--7  8  9 10 11 12 13 14 15
// 040280  61 14 00 e0 24 c0 00 00 04 00 00 00 00 00 00 01  a...$...........
// 040290  00 00 00 40 00 00 00 40 ff ff ff ff 00 00 ff ff  ...@...@........
// 0402a0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
// 0402b0  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................

typedef struct {
    uint8_t  lone_worker;           // [0] 1 bit
    uint8_t  squelch;               //     1 bit
    uint8_t  autoscan;              //     1 bit
    uint8_t  bandwidth;             //     1 bit
    uint8_t  channel_mode;          //     2 bits

    uint8_t  colorcode;             // [1] 4 bits
    uint8_t  repeater_slot;         //     2 bits
    uint8_t  rx_only;               //     1 bit
    uint8_t  allow_talkaround;      //     1 bit - disabled

    uint8_t  data_call_conf;        // [2] 1 bit
    uint8_t  private_call_conf;     //     1 bit
    uint8_t  privacy;               //     2 bits
    uint8_t  privacy_no;            //     4 bits

    uint8_t  display_pttid;         // [3] 1 bit
    uint8_t  compressed_udp_hdr;    //     1 bit
    uint8_t  emergency_alarm_ack;   //     1 bit
    uint8_t  rx_ref_frequency;      //     2 bits

    uint8_t  admit_criteria;        // [4] 2 bits
    uint8_t  power;                 //     1 bit
    uint8_t  vox;                   //     1 bit
    uint8_t  qt_reverse;            //     1 bit
    uint8_t  reverse_burst;         //     1 bit
    uint8_t  tx_ref_frequency;      //     2 bits
                                    // [5]     unused
    uint16_t contact_name_index;    // [6-7]   16 bits
    uint8_t  tot;                   // [8]     6 bits
    uint8_t  tot_rekey_delay;       // [9]     8 bits
    uint8_t  emergency_system;      // [10]    6 bits
    uint8_t  scan_list_index;       // [11]    8 bits
    uint8_t  group_list_index;      // [12]    8 bits
                                    // [13]    unused
    uint8_t  decode_18;             // [14]    8 bits
                                    // [15]    unused
    uint32_t rx_frequency;          // [16-19] 32 bits
    uint32_t tx_frequency;          // [20-23] 32 bits
    uint16_t ctcss_dcs_decode;      // [24-25] 16 bits
    uint16_t ctcss_dcs_encode;      // [26-27] 16 bits
    uint8_t  rx_signaling_syst;     // [28]    3 bits
    uint8_t  tx_signaling_syst;     // [29]    3 bits
                                    // [30-31] unused
    uint16_t name [17];             // [32-63]
} channel_t;

//
// Read nbits from source buffer with given bit offset.
//
static unsigned decode_bits(const unsigned char *source, unsigned offset, unsigned nbits)
{
    unsigned i, result = 0;

    for (i=0; i<nbits; i++) {
        unsigned mask = 1 << (7 - (offset & 7));

        if (source[offset >> 3] & mask)
            result |= 1 << (nbits - i - 1);
        offset++;
    }
    return result;
}

//
// Read nbytes from source buffer with given byte offset.
//
static unsigned decode_bytes(const unsigned char *source, unsigned offset, unsigned nbytes)
{
    unsigned i, result = 0;

    for (i=nbytes; i>0; i--) {
        result <<= 8;
        result |= source[i + offset - 1];
    }
    return result;
}

//
// Read BCD value of nbytes from source buffer with given byte offset.
//
static unsigned decode_bcd(const unsigned char *source, unsigned offset, unsigned nbytes)
{
    unsigned i, result = 0;

    for (i=nbytes; i>0; i--) {
        int b = source[i + offset - 1];
        int a = b >> 4;
        b &= 0xf;

        if (a > 9 || b > 9)
            return 0;

        result *= 100;
        result += a*10 + b;
    }
    return result;
}

//
// Decode CTCSS/DCS tones from source buffer with given byte offset.
//
static unsigned decode_tones(const unsigned char *source, unsigned offset)
{
    unsigned char ch[2];
    unsigned hi, lo;

    ch[0] = source[offset];
    ch[1] = source[offset + 1];

    hi = (ch[1] & 0xc0) << 10;
    ch[1] &= ~0xc0;

    lo = decode_bcd(ch, 0, 16/8);
    if (lo == 0)
        return 0;

    return hi | lo;
}

//
// Read unicode text.
//
static void decode_text(const unsigned char *source, unsigned offset, uint16_t *target, unsigned nbytes)
{
    source += offset;
    while (nbytes-- > 0) {
        *target = source[0] | (source[1] << 8);
        source += 2;
        target++;
    }
    *target = 0;
}

//
// Encode utf16 text to utf8.
// Return a pointer to a static buffer.
//
static char *utf8(const uint16_t *text, unsigned nchars)
{
    static char buf[256];
    unsigned i;

    for (i=0; i<nchars && *text; i++) {
        //TODO: convert to utf8
        buf[i] = *text++;
    }
    buf[i] = 0;
    return buf;
}

//
// Get all parameters for a given channel.
//
static void decode_channel(int i, channel_t *ch)
{
    unsigned char *buf = &radio_mem[OFFSET_CHANNELS + i*64];

    memset(ch, 0, sizeof(*ch));
    ch->lone_worker         = decode_bits(buf,   0,     1);
    ch->squelch             = decode_bits(buf,   2,     1);
    ch->autoscan            = decode_bits(buf,   3,     1);
    ch->bandwidth           = decode_bits(buf,   4,     1);
    ch->channel_mode        = decode_bits(buf,   6,     2);
    ch->colorcode           = decode_bits(buf,   8,     4);
    ch->repeater_slot       = decode_bits(buf,   12,    2);
    ch->rx_only             = decode_bits(buf,   14,    1);
    ch->allow_talkaround    = decode_bits(buf,   15,    1);
    ch->data_call_conf      = decode_bits(buf,   16,    1);
    ch->private_call_conf   = decode_bits(buf,   17,    1);
    ch->privacy             = decode_bits(buf,   18,    2);
    ch->privacy_no          = decode_bits(buf,   20,    4);
    ch->display_pttid       = decode_bits(buf,   24,    1);
    ch->compressed_udp_hdr  = decode_bits(buf,   25,    1);
    ch->emergency_alarm_ack = decode_bits(buf,   28,    1);
    ch->rx_ref_frequency    = decode_bits(buf,   30,    2);
    ch->admit_criteria      = decode_bits(buf,   32,    2);
    ch->power               = decode_bits(buf,   34,    1);
    ch->vox                 = decode_bits(buf,   35,    1);
    ch->qt_reverse          = decode_bits(buf,   36,    1);
    ch->reverse_burst       = decode_bits(buf,   37,    1);
    ch->tx_ref_frequency    = decode_bits(buf,   38,    2);
    ch->contact_name_index  = decode_bytes(buf,  48/8,  16/8);
    ch->tot                 = decode_bits(buf,   66,    6);
    ch->tot_rekey_delay     = decode_bits(buf,   72,    8);
    ch->emergency_system    = decode_bits(buf,   82,    6);
    ch->scan_list_index     = decode_bits(buf,   88,    8);
    ch->group_list_index    = decode_bits(buf,   96,    8);
    ch->decode_18           = decode_bits(buf,   112,   8);
    ch->rx_frequency        = decode_bcd(buf,    128/8, 32/8);
    ch->tx_frequency        = decode_bcd(buf,    160/8, 32/8);
    ch->ctcss_dcs_decode    = decode_tones(buf,  192/8);
    ch->ctcss_dcs_encode    = decode_tones(buf,  208/8);
    ch->rx_signaling_syst   = decode_bits(buf,   229,   3);
    ch->tx_signaling_syst   = decode_bits(buf,   237,   3);
    decode_text(buf, 256/8, ch->name, 256/8);
}

//
// Set the parameters for a given memory channel.
//
static void setup_channel(int i, char *name, double rx_mhz, double tx_mhz,
    int tmode, int power, int wide, int scan, int isam)
{
#if 0
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[OFFSET_CHANNELS];

    hz_to_freq((int) (rx_mhz * 1000000.0), ch->rxfreq);

    double offset_mhz = tx_mhz - rx_mhz;
    ch->offset = 0;
    ch->txfreq[0] = ch->txfreq[1] = ch->txfreq[2] = 0;
    if (offset_mhz == 0) {
        ch->duplex = D_SIMPLEX;
    } else if (offset_mhz > 0 && offset_mhz < 256 * 0.05) {
        ch->duplex = D_POS_OFFSET;
        ch->offset = (int) (offset_mhz / 0.05 + 0.5);
    } else if (offset_mhz < 0 && offset_mhz > -256 * 0.05) {
        ch->duplex = D_NEG_OFFSET;
        ch->offset = (int) (-offset_mhz / 0.05 + 0.5);
    } else {
        ch->duplex = D_CROSS_BAND;
        hz_to_freq((int) (tx_mhz * 1000000.0), ch->txfreq);
    }
    ch->used = (rx_mhz > 0);
    ch->tmode = tmode;
    ch->power = power;
    ch->isnarrow = ! wide;
    ch->isam = isam;
    ch->step = (rx_mhz >= 400) ? STEP_12_5 : STEP_5;
    ch->_u1 = 0;
    ch->_u2 = (rx_mhz >= 400);
    ch->_u3 = 0;
    ch->_u4[0] = 15;
    ch->_u4[1] = 0;
    ch->_u5[0] = ch->_u5[1] = ch->_u5[2] = 0;

    // Scan mode.
    unsigned char *scan_data = &radio_mem[OFFSET_SCAN + i/4];
    int scan_shift = (i & 3) * 2;
    *scan_data &= ~(3 << scan_shift);
    *scan_data |= scan << scan_shift;

    encode_name(i, name);
#endif
}

//
// Print the transmit offset or frequency.
//
static void print_offset(FILE *out, int rx_hz, int tx_hz)
{
    int delta = tx_hz - rx_hz;

    if (delta == 0) {
        fprintf(out, "+0      ");
    } else if (delta > 0 && delta/50000 <= 255) {
        if (delta % 1000000 == 0)
            fprintf(out, "+%-7u", delta / 1000000);
        else
            fprintf(out, "+%-7.3f", delta / 1000000.0);
    } else if (delta < 0 && -delta/50000 <= 255) {
        delta = - delta;
        if (delta % 1000000 == 0)
            fprintf(out, "-%-7u", delta / 1000000);
        else
            fprintf(out, "-%-7.3f", delta / 1000000.0);
    } else {
        // Cross band mode.
        fprintf(out, " %-7.4f", tx_hz / 1000000.0);
    }
}

//
// Print full information about the device configuration.
//
static void uv380_print_config(FILE *out, int verbose)
{
    int i;

    fprintf(out, "Radio: TYT MD-UV380\n");

    //
    // Memory channels.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of preprogrammed channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 6 characters, no spaces\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Squelch tone for receive, or '-' to disable\n");
        fprintf(out, "# 6) Squelch tone for transmit, or '-' to disable\n");
        fprintf(out, "# 7) Transmit power: High, Mid, Low\n");
        fprintf(out, "# 8) Modulation: Wide, Narrow, AM\n");
        fprintf(out, "# 9) Scan mode: +, -, Only\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Channel Name             Receive  Transmit Power Width  Scan\n");
    for (i=0; i<NCHAN; i++) {
        channel_t ch;

        decode_channel(i, &ch);
        if (ch.name[0] == 0) {
            // Channel is disabled
            continue;
        }

        fprintf(out, "%5d   %-16s ", i+1, utf8(&ch.name[0], 16));
        if (ch.rx_frequency % 100 != 0)
            fprintf(out, "%8.4f ", ch.rx_frequency / 100000.0);
        else
            fprintf(out, "%7.3f  ", ch.rx_frequency / 100000.0);

        print_offset(out, ch.rx_frequency, ch.tx_frequency);

        fprintf(out, " %-4s  %-6s %x\n", POWER_NAME[ch.power],
            ch.bandwidth ? "Wide" : "Normal", ch.scan_list_index);
#if 0
    ch.lone_worker         1
    ch.squelch             1
    ch.autoscan            1
    ch.channel_mode        2
    ch.colorcode           4
    ch.repeater_slot       2
    ch.rx_only             1
    ch.allow_talkaround    1
    ch.data_call_conf      1
    ch.private_call_conf   1
    ch.privacy             2
    ch.privacy_no          4
    ch.display_pttid       1
    ch.compressed_udp_hdr  1
    ch.emergency_alarm_ack 1
    ch.rx_ref_frequency    2
    ch.admit_criteria      2
    ch.vox                 1
    ch.qt_reverse          1
    ch.reverse_burst       1
    ch.tx_ref_frequency    2
    ch.contact_name_index  16/8
    ch.tot                 6
    ch.tot_rekey_delay     8
    ch.emergency_system    6
    ch.group_list_index    8
    ch.decode_18           8
    ch.ctcss_dcs_decode
    ch.ctcss_dcs_encode
    ch.tx_signaling_syst   3
    ch.rx_signaling_syst   3
#endif
    }
#if 0
    //
    // Zones.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of channel zones.\n");
        fprintf(out, "# 1) Zone number: 1-%d\n", NZONES);
        fprintf(out, "# 2) List of channels: numbers and ranges (N-M) separated by comma\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Zone    Channels\n");
    for (i=0; i<NZONES; i++) {
        if (have_zone(i))
            print_zone(out, i);
    }
#endif
}

//
// Read memory image from the binary file.
//
static void uv380_read_image(FILE *img)
{
    if (fread(&radio_mem[0], 1, MEMSZ, img) != MEMSZ) {
        fprintf(stderr, "Error reading image data.\n");
        exit(-1);
    }
}

//
// Save memory image to the binary file.
//
static void uv380_save_image(FILE *img)
{
    fwrite(&radio_mem[0], 1, MEMSZ+1, img);
}

//
// Parse the scalar parameter.
//
static void uv380_parse_parameter(char *param, char *value)
{
    if (strcasecmp("Radio", param) == 0) {
        if (strcasecmp("TYT MD-UV380", value) != 0) {
            fprintf(stderr, "Bad value for %s: %s\n", param, value);
            exit(-1);
        }
        return;
    }
    fprintf(stderr, "Unknown parameter: %s = %s\n", param, value);
    exit(-1);
}

//
// Check that the radio does support this frequency.
//
static int is_valid_frequency(int mhz)
{
    if (mhz >= 108 && mhz <= 520)
        return 1;
    if (mhz >= 700 && mhz <= 999)
        return 1;
    return 0;
}

//
// Parse one line of memory channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_channel(int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], wide_str[256], scan_str[256];
    int num, tmode, power, wide, scan, isam;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str, power_str,
        wide_str, scan_str) != 9)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        ! is_valid_frequency(rx_mhz)) {
        fprintf(stderr, "Bad receive frequency.\n");
        return 0;
    }
    if (sscanf(offset_str, "%lf", &tx_mhz) != 1) {
badtx:  fprintf(stderr, "Bad transmit frequency.\n");
        return 0;
    }
    if (offset_str[0] == '-' || offset_str[0] == '+')
        tx_mhz += rx_mhz;
    if (! is_valid_frequency(tx_mhz))
        goto badtx;

    //TODO
    tmode = 0;

    if (strcasecmp("High", power_str) == 0) {
        power = 0;
    } else if (strcasecmp("Mid", power_str) == 0) {
        power = 1;
    } else if (strcasecmp("Low", power_str) == 0) {
        power = 2;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (strcasecmp("Wide", wide_str) == 0) {
        wide = 1;
        isam = 0;
    } else if(strcasecmp("Narrow", wide_str) == 0) {
        wide = 0;
        isam = 0;
    } else if(strcasecmp("AM", wide_str) == 0) {
        wide = 1;
        isam = 1;
    } else {
        fprintf(stderr, "Bad modulation width.\n");
        return 0;
    }

    if (*scan_str == '+') {
        scan = 0;
    } else if (*scan_str == '-') {
        scan = 1;
    } else if (strcasecmp("Only", scan_str) == 0) {
        scan = 2;
    } else {
        fprintf(stderr, "Bad scan flag.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the channel table.
        int i;
        for (i=0; i<NCHAN; i++) {
            setup_channel(i, 0, 0, 0, 0, 12, 1, 0, 0);
        }
    }

    setup_channel(num-1, name_str, rx_mhz, tx_mhz,
        tmode, power, wide, scan, isam);
    return 1;
}

//
// Parse one line of Zones table.
// Return 0 on failure.
//
static int parse_zones(int first_row, char *line)
{
    char num_str[256], chan_str[256];
    int bnum;

    if (sscanf(line, "%s %s", num_str, chan_str) != 2)
        return 0;

    bnum = atoi(num_str);
    if (bnum < 1 || bnum > NZONES) {
        fprintf(stderr, "Bad zone number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Zones table.
        memset(&radio_mem[OFFSET_ZONES], 0, NZONES * 0x80);
    }

    if (*chan_str == '-')
        return 1;

    char *str   = chan_str;
    int   nchan = 0;
    int   range = 0;
    int   last  = 0;

    // Parse channel list.
    for (;;) {
        char *eptr;
        int cnum = strtoul(str, &eptr, 10);

        if (eptr == str) {
            fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", bnum, str);
            return 0;
        }
        if (cnum < 1 || cnum > NCHAN) {
            fprintf(stderr, "Zone %d: wrong channel number %d.\n", bnum, cnum);
            return 0;
        }

        if (range) {
            // Add range.
            int c;
            for (c=last; c<cnum; c++) {
                setup_zone(bnum-1, c);
                nchan++;
            }
        } else {
            // Add single channel.
            setup_zone(bnum-1, cnum-1);
            nchan++;
        }

        if (*eptr == 0)
            break;

        if (*eptr != ',' && *eptr != '-') {
            fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", bnum, eptr);
            return 0;
        }
        range = (*eptr == '-');
        last = cnum;
        str = eptr + 1;
    }
    return 1;
}

//
// Parse table header.
// Return table id, or 0 in case of error.
//
static int uv380_parse_header(char *line)
{
    if (strncasecmp(line, "Channel", 7) == 0)
        return 'C';
    if (strncasecmp(line, "Zone", 4) == 0)
        return 'Z';
    return 0;
}

//
// Parse one line of table data.
// Return 0 on failure.
//
static int uv380_parse_row(int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'C': return parse_channel(first_row, line);
    case 'Z': return parse_zones(first_row, line);
    }
    return 0;
}

//
// TYT MD-UV380
//
radio_device_t radio_uv380 = {
    "TYT MD-UV380",
    uv380_download,
    uv380_upload,
    uv380_is_compatible,
    uv380_read_image,
    uv380_save_image,
    uv380_print_version,
    uv380_print_config,
    uv380_parse_parameter,
    uv380_parse_header,
    uv380_parse_row,
};
