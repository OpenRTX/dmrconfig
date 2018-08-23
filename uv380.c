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

#define NCHAN           3000
#define NCONTACTS       10000
#define NZONES          250

#define MEMSZ           0xd0000
#define OFFSET_ID       0x02084
#define OFFSET_NAME     0x020b0
#define OFFSET_ZONES    0x149e0
#define OFFSET_ZONEXT   0x31000
#define OFFSET_CHANNELS 0x40000
#define OFFSET_CONTACTS 0x70000

//
// Channel data.
//
typedef struct {
    //
    // Byte 0
    //
    uint8_t channel_mode        : 2,    // Mode: Analog or Digital
#define MODE_ANALOG     1
#define MODE_DIGITAL    2

            bandwidth           : 2,    // Bandwidth: 12.5 or 20 or 25 kHz
#define BW_12_5_KHZ     0
#define BW_20_KHZ       1
#define BW_25_KHZ       2

            autoscan            : 1,    // Autoscan Enable
            _unused1            : 2,    // 0b11
            lone_worker         : 1;    // Lone Worker

    //
    // Byte 1
    //
    uint8_t _unused2            : 1,    // 0
            rx_only             : 1,    // RX Only Enable
            repeater_slot       : 2,    // Repeater Slot: 1 or 2
            colorcode           : 4;    // Color Code: 1...15

    //
    // Byte 2
    //
    uint8_t privacy_no          : 4,    // Privacy No. (+1): 1...16
            privacy             : 2,    // Privacy: None, Basic or Enhanced
#define PRIV_NONE       0
#define PRIV_BASIC      1
#define PRIV_ENHANCED   2

            private_call_conf   : 1,    // Private Call Confirmed
            data_call_conf      : 1;    // Data Call Confirmed

    //
    // Byte 3
    //
    uint8_t rx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
#define REF_LOW         0
#define REF_MEDIUM      1
#define REF_HIGH        2

            _unused3            : 1,    // 0
            emergency_alarm_ack : 1,    // Emergency Alarm Ack
            _unused4            : 3,    // 0b110
            display_pttid_dis   : 1;    // Display PTT ID (inverted)

    //
    // Byte 4
    //
    uint8_t tx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
            _unused5            : 2,    // 0b01
            vox                 : 1,    // VOX Enable
            _unused6            : 1,    // 1
            admit_criteria      : 2;    // Admit Criteria: Always, Channel Free or Correct CTS/DCS
#define ADMIT_ALWAYS    0
#define ADMIT_CH_FREE   1
#define ADMIT_TONE      2

    //
    // Byte 5
    //
    uint8_t _unused7            : 4,    // 0
            in_call_criteria    : 2,    // In Call Criteria: Always, Follow Admit Criteria or TX Interrupt
#define INCALL_ALWAYS   0
#define INCALL_ADMIT    1
#define INCALL_TXINT    2

            turn_off_freq       : 2;    // Non-QT/DQT Turn-off Freq.: None, 259.2Hz or 55.2Hz
#define TURNOFF_NONE    3
#define TURNOFF_259_2HZ 0
#define TURNOFF_55_2HZ  1

    //
    // Bytes 6-7
    //
    uint16_t contact_name_index;        // Contact Name: Contact1...

    //
    // Bytes 8-9
    //
    uint8_t tot;                        // TOT x 15sec: 0-Infinite, 1=15s... 37=255s
    uint8_t tot_rekey_delay;            // TOT Rekey Delay: 0s...255s

    //
    // Bytes 10-11
    //
    uint8_t emergency_system_index;     // Emergency System: None, System1...32
    uint8_t scan_list_index;            // Scan List: None, ScanList1...250

    //
    // Bytes 12-13
    //
    uint8_t group_list_index;           // Group List: None, GroupList1...250
    uint8_t _unused8;                   // 0

    //
    // Bytes 14-15
    //
    uint8_t _unused9;                   // 0
    uint8_t squelch;                    // Squelch: 0...9

    //
    // Bytes 16-23
    //
    uint32_t rx_frequency;              // RX Frequency: 8 digits BCD
    uint32_t tx_frequency;              // TX Frequency: 8 digits BCD

    //
    // Bytes 24-27
    //
    uint16_t ctcss_dcs_decode;          // CTCSS/DCS Dec: 4 digits BCD
    uint16_t ctcss_dcs_encode;          // CTCSS/DCS Enc: 4 digits BCD

    //
    // Bytes 28-29
    //
    uint8_t rx_signaling_syst;          // Rx Signaling System: Off, DTMF-1...4
    uint8_t tx_signaling_syst;          // Tx Signaling System: Off, DTMF-1...4

    //
    // Byte 30
    //
    uint8_t power               : 2,    // Power: Low, Middle, High
#define POWER_HIGH      3
#define POWER_LOW       0
#define POWER_MIDDLE    2

            _unused10           : 6;    // 0b111111

    //
    // Byte 31
    //
    uint8_t dcdm_switch_dis     : 1,    // DCDM switch (inverted)
            _unused11           : 3,    // 0b111
            leader_ms           : 1,    // Leader/MS: Leader or MS
#define DCDM_LEADER     0
#define DCDM_MS         1

            _unused12           : 3;    // 0b111

    //
    // Bytes 32-63
    //
    uint16_t name[16];                  // Channel Name (Unicode)
} channel_t;

//
// Contact data.
//
typedef struct {
    //
    // Bytes 0-2
    //
    uint32_t id                 : 24;   // Call ID: 1...16777215

    //
    // Byte 3
    //
    uint8_t type                : 2,    // Call Type: Group Call, Private Call or All Call
#define CALL_GROUP      1
#define CALL_PRIVATE    2
#define CALL_ALL        3

            _unused1            : 3,    // 0
            receive_tone        : 1,    // Call Receive Tone: No or yes
            _unused2            : 2;    // 0b11

    //
    // Bytes 4-19
    //
    uint16_t name[16];                  // Contact Name (Unicode)
} contact_t;

//
// Zone data.
//
typedef struct {
    //
    // Bytes 0-15
    //
    uint16_t name[16];                  // Zone Name (Unicode)

    //
    // Bytes 6-31
    //
    uint16_t member_a[16];              // Member A: channels 1...16
} zone_t;

typedef struct {
    //
    // Bytes 0-95
    //
    uint16_t ext_a[48];                 // Member A: channels 17...64

    //
    // Bytes 96-223
    //
    uint16_t member_b[64];              // Member B: channels 1...64
} zone_ext_t;

static const char *POWER_NAME[] = { "Low", "???", "Mid", "High" };
static const char *BANDWIDTH[] = { "12.5", "20", "25" };
static const char *CONTACT_TYPE[] = { "-", "Group", "Private", "All" };

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

#if 0
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
#endif

//
// Print utf16 text as utf8.
//
static void print_unicode(FILE *out, const uint16_t *text, unsigned nchars, int fill_flag)
{
    unsigned i;

    for (i=0; i<nchars && *text; i++) {
        //TODO: convert to utf8
        putc(*text++, out);
    }
    if (fill_flag) {
        for (; i<nchars; i++) {
            putc(' ', out);
        }
    }
}

//
// Print frequency (BCD value).
//
static void print_freq(FILE *out, unsigned data)
{
    fprintf(out, "%d%d%d.%d%d%d", (data >> 28) & 15, (data >> 24) & 15,
        (data >> 20) & 15, (data >> 16) & 15,
        (data >> 12) & 15, (data >> 8) & 15);

    if ((data & 0xff) == 0)
        putc(' ', out);
    else
        fprintf(out, "%d", (data >> 4) & 15);
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
// Convert a 4-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
static int freq_to_hz(uint32_t bcd)
{
    int a = (bcd >> 28) & 15;
    int b = (bcd >> 24) & 15;
    int c = (bcd >> 20) & 15;
    int d = (bcd >> 16) & 15;
    int e = (bcd >> 12) & 15;
    int f = (bcd >> 8)  & 15;
    int g = (bcd >> 4)  & 15;
    int h =  bcd        & 15;

    return (((((((a*10 + b) * 10 + c) * 10 + d) * 10 + e) * 10 + f) * 10 + g) * 10 + h) * 10;
}

//
// Print the transmit offset or frequency.
//
static void print_offset(FILE *out, uint32_t rx_bcd, uint32_t tx_bcd)
{
    int rx_hz = freq_to_hz(rx_bcd);
    int tx_hz = freq_to_hz(tx_bcd);
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

static void print_chanlist(FILE *out, uint16_t *data, int nchan)
{
    int last  = -1;
    int range = 0;
    int n;

    for (n=0; n<=nchan; n++) {
        int cnum = data[n];

        if (cnum == 0)
            break;

        if (cnum == last+1) {
            range = 1;
        } else {
            if (range) {
                fprintf(out, "-%d", last);
                range = 0;
            }
            if (n > 0)
                fprintf(out, ",");
            fprintf(out, "%d", cnum);
        }
        last = cnum;
    }
    if (range)
        fprintf(out, "-%d", last);
}

//
// Print full information about the device configuration.
//
static void uv380_print_config(FILE *out, int verbose)
{
    int i;

    fprintf(out, "Radio: TYT MD-UV380");
    if (radio_mem[OFFSET_NAME] != 0) {
        fprintf(out, "\nName: ");
        print_unicode(out, (uint16_t*) &radio_mem[OFFSET_NAME], 16, 0);
    }
    fprintf(out, "\nID: %d\n", *(uint32_t*) &radio_mem[OFFSET_ID]);

    //
    // Channels.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of preprogrammed channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, no spaces\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Mid, Low\n");
        fprintf(out, "# 6) Bandwidth in kHz: 12.5, 20, 25\n");
        fprintf(out, "# 7) Scan list: - or index\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Channel Name             Receive  Transmit Power Width  Scan\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = (channel_t*) &radio_mem[OFFSET_CHANNELS + i*64];

        if (ch->name[0] == 0) {
            // Channel is disabled
            continue;
        }

        fprintf(out, "%5d   ", i+1);
        print_unicode(out, ch->name, 16, 1);
        fprintf(out, " ");
        print_freq(out, ch->rx_frequency);
        fprintf(out, " ");
        print_offset(out, ch->rx_frequency, ch->tx_frequency);

        fprintf(out, " %-4s  %-6s ",
            POWER_NAME[ch->power], BANDWIDTH[ch->bandwidth]);

        if (ch->scan_list_index == 0)
            fprintf(out, "-\n");
        else
            fprintf(out, "%d\n", ch->scan_list_index);
    }

    //
    // Zones.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of channel zones.\n");
        fprintf(out, "# 1) Zone number: 1-%d\n", NZONES);
        fprintf(out, "# 2) Name: up to 16 characters, no spaces\n");
        fprintf(out, "# 3) List of channels: numbers and ranges (N-M) separated by comma\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Zone    Name             Channels\n");
    for (i=0; i<NZONES; i++) {
        zone_t     *z     = (zone_t*) &radio_mem[OFFSET_ZONES + i*64];
        zone_ext_t *zext  = (zone_ext_t*) &radio_mem[OFFSET_ZONEXT + i*224];

        if (z->name[0] == 0) {
            // Zone is disabled.
            continue;
        }

        fprintf(out, "%4da   ", i + 1);
        print_unicode(out, z->name, 16, 1);
        fprintf(out, " ");
        if (z->member_a[0]) {
            print_chanlist(out, z->member_a, 16);
            if (zext->ext_a[0]) {
                fprintf(out, ",");
                print_chanlist(out, zext->ext_a, 48);
            }
        } else {
            fprintf(out, "-");
        }
        fprintf(out, "\n");

        fprintf(out, "%4db   -                ", i + 1);
        if (zext->member_b[0]) {
            print_chanlist(out, zext->member_b, 64);
        } else {
            fprintf(out, "-");
        }
        fprintf(out, "\n");
    }

    //
    // Contacts.
    //
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of contacts.\n");
        fprintf(out, "# 1) Contact number: 1-%d\n", NCONTACTS);
        fprintf(out, "# 2) Name: up to 16 characters, no spaces\n");
        fprintf(out, "# 3) Call type: Group, Private, All\n");
        fprintf(out, "# 4) Call ID: 1...16777215\n");
        fprintf(out, "# 5) Call receive tone: -, Yes\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Contact Name             Type    ID       RxTone\n");
    for (i=0; i<NCONTACTS; i++) {
        contact_t *ct = (contact_t*) &radio_mem[OFFSET_CONTACTS + i*36];

        if (ct->name[0] == 0) {
            // Channel is disabled
            continue;
        }

        fprintf(out, "%5d   ", i+1);
        print_unicode(out, ct->name, 16, 1);
        fprintf(out, " %-7s %-8d %s\n",
            CONTACT_TYPE[ct->type], ct->id, ct->receive_tone ? "Yes" : "-");
    }
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
