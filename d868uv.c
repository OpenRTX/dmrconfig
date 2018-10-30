/*
 * Interface to Anytone D868UV.
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
#include <sys/stat.h>
#include "radio.h"
#include "util.h"

//
// Sizes of configuration tables.
//
#define NCHAN           4000
#define NCONTACTS       10000
#define NZONES          250
#define NGLISTS         250
#define NSCANL          250
#define NMESSAGES       100

//
// Offsets in the image file.
//
#define OFFSET_BANK1        0x000040    // Channels
#define OFFSET_ZONELISTS    0x03e8c0    // Channel lists of zones
#define OFFSET_SCANLISTS    0x05dcc0    // Scanlists
#define OFFSET_MESSAGES     0x069f40    // Messages
#define OFFSET_ZONE_MAP     0x070940    // Bitmap of valid zones
#define OFFSET_SCANL_MAP    0x070980    // Bitmap of valid scanlists
#define OFFSET_CHAN_MAP     0x070a40    // Bitmap of valid channels
#define OFFSET_SETTINGS     0x071600    // General settings
#define OFFSET_ZONENAMES    0x071dc0    // Names of zones
#define OFFSET_RADIOID      0x073d00    // Table of radio IDs
#define OFFSET_CONTACT_MAP  0x080140    // Bitmap of invalid contacts
#define OFFSET_CONTACTS     0x080640    // Contacts
#define OFFSET_GLISTS       0x174b00    // RX group lists

#define GET_SETTINGS()      ((general_settings_t*) &radio_mem[OFFSET_SETTINGS])
#define GET_RADIOID()       ((radioid_t*) &radio_mem[OFFSET_RADIOID])
#define GET_ZONEMAP()       (&radio_mem[OFFSET_ZONE_MAP])
#define GET_CONTACT_MAP()   (&radio_mem[OFFSET_CONTACT_MAP])
#define GET_SCANL_MAP()     (&radio_mem[OFFSET_SCANL_MAP])
#define GET_ZONENAME(i)     (&radio_mem[OFFSET_ZONENAMES + (i)*32])
#define GET_ZONELIST(i)     ((uint16_t*) &radio_mem[OFFSET_ZONELISTS + (i)*512])
#define GET_CONTACT(i)      ((contact_t*) &radio_mem[OFFSET_CONTACTS + (i)*100])
#define GET_GROUPLIST(i)    ((grouplist_t*) &radio_mem[OFFSET_GLISTS + (i)*320])
#define GET_SCANLIST(i)     ((scanlist_t*) &radio_mem[OFFSET_SCANLISTS + (i)*192])
#define GET_MESSAGE(i)      ((uint8_t*) &radio_mem[OFFSET_MESSAGES + (i)*256])

#define VALID_TEXT(txt)     (*(txt) != 0 && *(txt) != 0xff)
#define VALID_GROUPLIST(gl) ((gl)->member[0] != 0xffffffff && VALID_TEXT((gl)->name))

//
// Size of memory image.
// Essentialy a sum of all fragments defined ind868um-map.h.
//
#define MEMSZ           1607296

//
// D868UV radio has a huge internal address space, more than 64 Mbytes.
// The configuration data are dispersed over this space.
// Here is a table of fragments: starting address and length.
// We read these fragments and save them into a file continuously.
//
typedef struct {
    unsigned address;
    unsigned length;
} fragment_t;

static fragment_t region_map[] = {
#include "d868uv-map.h"
};

//
// Channel data.
//
typedef struct {

    // Bytes 0-7
    uint32_t    rx_frequency;           // RX Frequency: 8 digits BCD
    uint32_t    tx_offset;              // TX Offset: 8 digits BCD

    // Byte 8
    uint8_t     channel_mode    : 2,    // Mode: Analog or Digital
#define MODE_ANALOG     0               // Analog
#define MODE_DIGITAL    1               // Digital
#define MODE_A_D        2               // A+D, transmit analog
#define MODE_D_A        3               // D+A, transmit digital

                power           : 2,    // Power: Low, Middle, High, Turbo
#define POWER_LOW       0
#define POWER_MIDDLE    1
#define POWER_HIGH      2
#define POWER_TURBO     3

                bandwidth       : 1,    // Bandwidth: 12.5 or 25 kHz
#define BW_12_5_KHZ     0
#define BW_25_KHZ       1

                _unused8        : 1,    // 0
                repeater_mode   : 2;    // Sign of TX frequency offset
#define RM_SIMPLEX      0               // TX frequency = RX frequency
#define RM_TXPOS        1               // Positive TX offset
#define RM_TXNEG        2               // Negative TX offset

    // Byte 9
    uint8_t     rx_ctcss        : 1,    // CTCSS Decode
                rx_dcs          : 1,    // DCS Decode
                tx_ctcss        : 1,    // CTCSS Encode
                tx_dcs          : 1,    // DCS Encode
                reverse         : 1,    // Reverse
                rx_only         : 1,    // TX Prohibit
                call_confirm    : 1,    // Call Confirmation
                talkaround      : 1;    // Talk Around

    // Bytes 10-15
    uint8_t     ctcss_transmit;         // CTCSS Encode: 0=62.5, 50=254.1, 51=Define
    uint8_t     ctcss_receive;          // CTCSS Decode: 0=62.5, 50=254.1, 51=Define
    uint16_t    dcs_transmit;           // DCS Encode: 0=D000N, 17=D021N, 1023=D777N
    uint16_t    dcs_receive;            // DCS Decode: 0=D000N, 17=D021N, 1023=D777N

    // Bytes 16-19
    uint16_t    custom_ctcss;           // 0x09cf=251.1, 0x0a28=260
    uint8_t     tone2_decode;           // 2Tone Decode: 0x00=1, 0x0f=16
    uint8_t     _unused19;              // 0

    // Bytes 20-23
    uint16_t    contact_index;          // Contact: 0=Contact1, 1=Contact2, ...
    uint16_t    _unused22;              // 0

    // Byte 24
    uint8_t     id_index;               // Index in Radio ID table

    // Byte 25
    uint8_t     ptt_id          : 2,    // PTT ID
#define PTTID_OFF       0
#define PTTID_START     1
#define PTTID_END       2
#define PTTID_START_END 3

                _unused25_1     : 2,    // 0

                squelch_mode    : 1,    // Squelch Mode
#define SQ_CARRIER      0               // Carrier
#define SQ_TONE         1               // CTCSS/DCS

                _unused25_2     : 3;    // 0

    // Byte 26
    uint8_t     tx_permit       : 2,    // TX Permit
#define PERMIT_ALWAYS   0               // Always
#define PERMIT_CH_FREE  1               // Channel Free
#define PERMIT_CC_DIFF  2               // Different Color Code
#define PERMIT_CC_SAME  3               // Same Color Code

                _unused26_1     : 2,    // 0

                _opt_signal     : 2,    // Optional Signal
#define OPTSIG_OFF      0               // Off
#define OPTSIG_DTMF     1               // DTMF
#define OPTSIG_2TONE    2               // 2Tone
#define OPTSIG_5TONE    3               // 5Tone

                _unused26_2     : 2;    // 0

    // Bytes 27-31
    uint8_t     scan_list_index;        // Scan List: 0xff=None, 0=ScanList1...
    uint8_t     group_list_index;       // Receive Group List: 0xff=None, 0=GroupList1...
    uint8_t     id_2tone;               // 2Tone ID: 0=1, 0x17=24
    uint8_t     id_5tone;               // 5Tone ID: 0=1, 0x63=100
    uint8_t     id_dtmf;                // DTMF ID: 0=1, 0x0f=16

    // Byte 32
    uint8_t     color_code;             // Color Code: 0-15

    // Byte 33
    uint8_t     slot2           : 1,    // Slot: Slot2
                _unused33_1     : 1,    // 0
                simplex_tdma    : 1,    // Simplex TDMA: On
                _unused33_2     : 1,    // 0
                tdma_adaptive   : 1,    // TDMA Adaptive: On
                _unused33_3     : 1,    // 0
                enh_encryption  : 1,    // Encryption Type: Enhanced Encryption
                work_alone      : 1;    // Work Alone: On

    // Byte 34
    uint8_t     encryption;             // Digital Encryption: 1-32, 0=Off

    // Bytes 35-51
    uint8_t     name[16];               // Channel Name, zero filled
    uint8_t     _unused51;              // 0

    // Byte 52
    uint8_t     ranging         : 1,    // Ranging: On
                through_mode    : 1,    // Through Mode: On
                _unused52       : 6;    // 0

    // Byte 53
    uint8_t     aprs_report     : 1,    // APRS Report: On
                _unused53       : 7;    // 0

    // Bytes 54-63
    uint8_t     aprs_channel;           // APRS Report Channel: 0x00=1, ... 0x07=8
    uint8_t     _unused55[9];           // 0
} channel_t;

//
// General settings: 0x640 bytes at 0x02500000.
//
typedef struct {

    // Bytes 0-5.
    uint8_t  _unused0[6];

    // Bytes 6-7.
    uint8_t  power_on;          // Power-on Interface
#define PWON_DEFAULT    0       // Default
#define PWON_CUST_CHAR  1       // Custom Char
#define PWON_CUST_PICT  2       // Custom Picture

    uint8_t  _unused7;

    // Bytes 8-0x5ff.
    uint8_t  _unused8[0x5f8];

    // Bytes 0x600-0x61f
    uint8_t intro_line1[16];    // Up to 14 characters
    uint8_t intro_line2[16];    // Up to 14 characters

    // Bytes 0x620-0x63f
    uint8_t password[16];       // Up to 8 ascii digits
    uint8_t _unused630[16];     // 0xff

} general_settings_t;

//
// Radio ID table: 250 entries, 0x1f40 bytes at 0x02580000.
//
typedef struct {

    // Bytes 0-3.
    uint8_t id[4];              // Up to 8 BCD digits
#define GET_ID(x) (((x)[0] >> 4) * 10000000 +\
                   ((x)[0] & 15) * 1000000 +\
                   ((x)[1] >> 4) * 100000 +\
                   ((x)[1] & 15) * 10000 +\
                   ((x)[2] >> 4) * 1000 +\
                   ((x)[2] & 15) * 100 +\
                   ((x)[3] >> 4) * 10 +\
                   ((x)[3] & 15))
    // Byte 4.
    uint8_t _unused4;           // 0

    // Bytes 5-20
    uint8_t name[16];           // Name

    // Bytes 21-31
    uint8_t _unused21[11];      // 0

} radioid_t;

//
// Contact data: 100 bytes per record.
//
typedef struct {

    // Byte 0
    uint8_t type;                       // Call Type: Group Call, Private Call or All Call
#define CALL_PRIVATE    0
#define CALL_GROUP      1
#define CALL_ALL        2

    // Bytes 1-16
    uint8_t name[16];                   // Contact Name (ASCII)

    // Bytes 17-34
    uint8_t _unused17[18];              // 0

    // Bytes 35-38
    uint8_t id[4];                      // Call ID: BCD coded 8 digits
#define GET_ID(x) (((x)[0] >> 4) * 10000000 +\
                   ((x)[0] & 15) * 1000000 +\
                   ((x)[1] >> 4) * 100000 +\
                   ((x)[1] & 15) * 10000 +\
                   ((x)[2] >> 4) * 1000 +\
                   ((x)[2] & 15) * 100 +\
                   ((x)[3] >> 4) * 10 +\
                   ((x)[3] & 15))
#define CONTACT_ID(ct) GET_ID((ct)->id)

    // Byte 39
    uint8_t call_alert;                 // Call Alert: None, Ring, Online Alert
#define ALERT_NONE      0
#define ALERT_RING      1
#define ALERT_ONLINE    2

    // Bytes 40-99
    uint8_t _unused40[60];              // 0

} contact_t;

//
// Group list data.
//
typedef struct {

    // Bytes 0-255
    uint32_t member[64];                // Contacts: 0=Contact1, 0xffffffff=Empty

    // Bytes 256-319
    uint8_t name[35];                   // Group List Name (ASCII)
    uint8_t unused[29];                 // 0

} grouplist_t;

//
// Scan list data: 192 bytes.
//
typedef struct {

    // Bytes 0-1
    uint8_t     _unused0;               // 0
    uint8_t     prio_ch_select;         // Priority Channel Select
#define PRIO_CHAN_OFF   0               // Off
#define PRIO_CHAN_SEL1  1               // Priority Channel Select1
#define PRIO_CHAN_SEL2  2               // Priority Channel Select2
#define PRIO_CHAN_SEL12 3               // Priority Channel Select1 + Priority Channel Select2

    // Bytes 2-5
    uint16_t    priority_ch1;           // Priority Channel 1: 0=Current Channel, 0xffff=Off
    uint16_t    priority_ch2;           // Priority Channel 2: 0=Current Channel, 0xffff=Off

    // Bytes 6-13
    uint16_t    look_back_a;            // Look Back Time A, sec*10
    uint16_t    look_back_b;            // Look Back Time B, sec*10
    uint16_t    dropout_delay;          // Dropout Delay Time, sec*10
    uint16_t    dwell;                  // Dwell Time, sec*10

    // Byte 14
    uint8_t     revert_channel;         // Revert Channel
#define REVCH_SELECTED      0           // Selected
#define REVCH_SEL_TB        1           // Selected + TalkBack
#define REVCH_PRIO_CH1      2           // Priority Channel Select1
#define REVCH_PRIO_CH2      3           // Priority Channel Select2
#define REVCH_LAST_CALLED   4           // Last Called
#define REVCH_LAST_USED     5           // Last Used
#define REVCH_PRIO_CH1_TB   6           // Priority Channel Select1 + TalkBack
#define REVCH_PRIO_CH2_TB   7           // Priority Channel Select2 + TalkBack

    // Bytes 15-31
    uint8_t     name[16];               // Scan List Name (ASCII)
    uint8_t     _unused31;              // 0

    // Bytes 32-131
    uint16_t member[50];                // Channels, 0xffff=empty

    // Bytes 132-191
    uint8_t _unused132[60];             // 0

} scanlist_t;

static const char *POWER_NAME[] = { "Low", "Mid", "High", "Turbo" };
static const char *DIGITAL_ADMIT_NAME[] = { "-", "Free", "NColor", "Color" };
static const char *ANALOG_ADMIT_NAME[] = { "-", "Tone", "Free", "Free" };
static const char *BANDWIDTH[] = { "12.5", "25" };
static const char *CONTACT_TYPE[] = { "Private", "Group", "All", "Unknown" };
static const char *ALERT_TYPE[] = { "-", "+", "Online", "Unknown" };

//
// CTCSS tones, Hz*10.
//
#define NCTCSS  51

static const int CTCSS_TONES[NCTCSS] = {
     625,  670,  693,  719,  744,  770,  797,  825,  854,  885,
     915,  948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230,
    1273, 1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655,
    1679, 1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966,
    1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503,
    2541,
};

//
// Print a generic information about the device.
//
static void d868uv_print_version(radio_device_t *radio, FILE *out)
{
    // Empty.
}

//
// Read memory image from the device.
//
static void d868uv_download(radio_device_t *radio)
{
    fragment_t *f;
    unsigned file_offset = 0;
    unsigned last_printed = 0;

//printf("Address     Offset\n");
    for (f=region_map; f->length; f++) {
        unsigned addr = f->address;
        unsigned nbytes = f->length;
//printf("%08x    %06x\n", addr, file_offset);

        while (nbytes > 0) {
            unsigned n = (nbytes > 32*1024) ? 32*1024 : nbytes;
            serial_read_region(addr, &radio_mem[file_offset], n);
            file_offset += n;
            addr += n;
            nbytes -= n;

            if (file_offset / (32*1024) != last_printed) {
                fprintf(stderr, "#");
                fflush(stderr);
                last_printed = file_offset / (32*1024);
            }
        }
    }
    if (file_offset != MEMSZ) {
        fprintf(stderr, "\nWrong MEMSZ=%u for D868UV!\n", MEMSZ);
        fprintf(stderr, "Should be %u; check d868uv-map.h!\n", file_offset);
        exit(-1);
    }
}

//
// Write memory image to the device.
//
static void d868uv_upload(radio_device_t *radio, int cont_flag)
{
    fragment_t *f;
    unsigned file_offset;
    unsigned last_printed = 0;

    // Skip first region.
    file_offset = region_map[0].length;
    for (f=region_map+1; f->length; f++) {
        unsigned addr = f->address;
        unsigned nbytes = f->length;

        while (nbytes > 0) {
            unsigned n = (nbytes > 32*1024) ? 32*1024 : nbytes;
            serial_write_region(addr, &radio_mem[file_offset], n);
            file_offset += n;
            addr += n;
            nbytes -= n;

            if (file_offset / (32*1024) != last_printed) {
                fprintf(stderr, "#");
                fflush(stderr);
                last_printed = file_offset / (32*1024);
            }
        }
    }
    if (file_offset != MEMSZ) {
        fprintf(stderr, "\nWrong MEMSZ=%u for D868UV!\n", MEMSZ);
        fprintf(stderr, "Should be %u; check d868uv-map.h!\n", file_offset);
        exit(-1);
    }
}

//
// Check whether the memory image is compatible with this device.
//
static int d868uv_is_compatible(radio_device_t *radio)
{
    return 1;
}

static void print_id(FILE *out, int verbose)
{
    radioid_t *ri = GET_RADIOID();
    unsigned id = GET_ID(ri->id);

    if (verbose)
        fprintf(out, "\n# Unique DMR ID and name of this radio.");
    fprintf(out, "\nID: %u\nName: ", id);
    if (VALID_TEXT(ri->name)) {
        print_ascii(out, ri->name, 16, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\n");
}

static void print_intro(FILE *out, int verbose)
{
    general_settings_t *gs = GET_SETTINGS();

    if (verbose)
        fprintf(out, "\n# Text displayed when the radio powers up.\n");
    fprintf(out, "Intro Line 1: ");
    if (VALID_TEXT(gs->intro_line1)) {
        print_ascii(out, gs->intro_line1, 14, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\nIntro Line 2: ");
    if (VALID_TEXT(gs->intro_line2)) {
        print_ascii(out, gs->intro_line2, 14, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\n");
}

//
// Get channel bank by index.
//
static channel_t *get_bank(int i)
{
    return (channel_t*) &radio_mem[OFFSET_BANK1 + i*0x2000];
}

//
// Get channel by index.
//
static channel_t *get_channel(int i)
{
    channel_t *bank   = get_bank(i >> 7);
    uint8_t   *bitmap = &radio_mem[OFFSET_CHAN_MAP];

    if ((bitmap[i / 8] >> (i & 7)) & 1)
        return &bank[i % 128];
    else
        return 0;
}

//
// Do we have any channels of given mode?
//
static int have_channels(int mode)
{
    int i;

    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;
        if (ch->channel_mode == mode)
            return 1;

        // Treat D+A mode as digital.
        if (mode == MODE_DIGITAL && ch->channel_mode == MODE_D_A)
            return 1;

        // Treat A+D mode as analog.
        if (mode == MODE_ANALOG && ch->channel_mode == MODE_A_D)
            return 1;
    }
    return 0;
}

//
// Return true when any contacts are present.
//
static int have_contacts()
{
    uint8_t *cmap = GET_CONTACT_MAP();
    int i;

    for (i=0; i<(NCONTACTS+7)/8; i++) {
        if (cmap[i] != 0xff)
            return 1;
    }
    return 0;
}

//
// Get contact by index.
//
static contact_t *get_contact(int i)
{
    uint8_t *cmap = GET_CONTACT_MAP();

    if ((cmap[i / 8] >> (i & 7)) & 1)
        return 0;

    return GET_CONTACT(i);
}

//
// Print frequency (BCD value).
//
static void print_rx_freq(FILE *out, unsigned data)
{
    fprintf(out, "%d%d%d.%d%d%d", (data >> 4) & 15, data & 15,
        (data >> 12) & 15, (data >> 8) & 15,
        (data >> 20) & 15, (data >> 16) & 15);

    if (((data >> 24) & 0xff) == 0) {
        fputs("  ", out);
    } else {
        fprintf(out, "%d", (data >> 28) & 15);
        if (((data >> 24) & 15) == 0) {
            fputs(" ", out);
        } else {
            fprintf(out, "%d", (data >> 24) & 15);
        }
    }
}

//
// Convert a 4-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
static int bcd_to_hz(unsigned bcd)
{
    int a = (bcd >> 4)  & 15;
    int b =  bcd        & 15;
    int c = (bcd >> 12) & 15;
    int d = (bcd >> 8)  & 15;
    int e = (bcd >> 20) & 15;
    int f = (bcd >> 16) & 15;
    int g = (bcd >> 28) & 15;
    int h = (bcd >> 24) & 15;

    return (((((((a*10 + b) * 10 + c) * 10 + d) * 10 + e) * 10 + f) * 10 + g) * 10 + h) * 10;
}

//
// Print the transmit offset or frequency.
// TX value is a delta.
//
static void print_tx_offset(FILE *out, unsigned tx_offset_bcd, unsigned mode)
{
    int offset;

    switch (mode) {
    default:
    case RM_SIMPLEX:            // TX frequency = RX frequency
        fprintf(out, "+0       ");
        break;

    case RM_TXPOS:              // Positive TX offset
        offset = bcd_to_hz(tx_offset_bcd);
        fprintf(out, "+");
        print_mhz(out, offset);
        break;

    case RM_TXNEG:              // Negative TX offset
        offset = bcd_to_hz(tx_offset_bcd);
        fprintf(out, "-");
        print_mhz(out, offset);
        break;
    }
}

//
// Print base parameters of the channel:
//      Name
//      RX Frequency
//      TX Frequency
//      Power
//      Scan List
//      TOT
//      RX Only
//
static void print_chan_base(FILE *out, channel_t *ch, int cnum)
{
    fprintf(out, "%5d   ", cnum);
    print_ascii(out, ch->name, 16, 1);
    fprintf(out, " ");
    print_rx_freq(out, ch->rx_frequency);
    fprintf(out, " ");
    print_tx_offset(out, ch->tx_offset, ch->repeater_mode);

    fprintf(out, "%-5s ", POWER_NAME[ch->power]);

    if (ch->scan_list_index == 0xff)
        fprintf(out, "-    ");
    else
        fprintf(out, "%-4d ", ch->scan_list_index + 1);

    // Transmit timeout timer on D868UV is configured globally,
    // not per channel. So we don't print it here.
    fprintf(out, "-   ");

    fprintf(out, "%c  ", "-+"[ch->rx_only]);
}

static void print_digital_channels(FILE *out, int verbose)
{
    int i;

    if (verbose) {
        fprintf(out, "# Table of digital channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Mid, Low, Turbo\n");
        fprintf(out, "# 6) Scan list: - or index in Scanlist table\n");
        fprintf(out, "# 7) Transmit timeout timer: (unused)\n");
        fprintf(out, "# 8) Receive only: -, +\n");
        fprintf(out, "# 9) Admit criteria: -, Free, Color, NColor\n");
        fprintf(out, "# 10) Color code: 0, 1, 2, 3... 15\n");
        fprintf(out, "# 11) Time slot: 1 or 2\n");
        fprintf(out, "# 12) Receive group list: - or index in Grouplist table\n");
        fprintf(out, "# 13) Contact for transmit: - or index in Contacts table\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Digital Name             Receive   Transmit Power Scan TOT RO Admit  Color Slot RxGL TxContact");
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;
        if (ch->channel_mode != MODE_DIGITAL && ch->channel_mode != MODE_D_A) {
            // Select digital channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print digital parameters of the channel:
        //      Admit Criteria
        //      Color Code
        //      Repeater Slot
        //      Group List
        //      Contact Name
        fprintf(out, "%-6s ", DIGITAL_ADMIT_NAME[ch->tx_permit]);
        fprintf(out, "%-5d %-3d  ", ch->color_code, 1 + ch->slot2);

        if (ch->group_list_index == 0xff)
            fprintf(out, "-    ");
        else
            fprintf(out, "%-4d ", ch->group_list_index + 1);

        if (ch->contact_index == 0xffff)
            fprintf(out, "-");
        else
            fprintf(out, "%-4d", ch->contact_index + 1);

        // Print contact name as a comment.
        if (ch->contact_index != 0xffff) {
            contact_t *ct = get_contact(ch->contact_index);

            if (ct) {
                fprintf(out, " # ");
                print_ascii(out, ct->name, 16, 0);
            }
        }
        fprintf(out, "\n");
    }
}

//
// Print CTSS tone.
//
static void print_ctcss(FILE *out, unsigned index, unsigned custom)
{
    int      dhz = (index < NCTCSS) ? CTCSS_TONES[index] : custom;
    unsigned a   = dhz / 1000;
    unsigned b   = (dhz / 100) % 10;
    unsigned c   = (dhz / 10) % 10;
    unsigned d   = dhz % 10;

    if (a == 0)
        fprintf(out, "%d%d.%d ", b, c, d);
    else
        fprintf(out, "%d%d%d.%d", a, b, c, d);
}

//
// Print DCS tone.
//
static void print_dcs(FILE *out, unsigned dcs)
{
    unsigned i = (dcs >> 9) & 1;
    unsigned a = (dcs >> 6) & 7;
    unsigned b = (dcs >> 3) & 7;
    unsigned c = dcs & 7;

    fprintf(out, "D%d%d%d%c", a, b, c, i ? 'I' : 'N');
}

static void print_analog_channels(FILE *out, int verbose)
{
    int i;

    if (verbose) {
        fprintf(out, "# Table of analog channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Mid, Low, Turbo\n");
        fprintf(out, "# 6) Scan list: - or index\n");
        fprintf(out, "# 7) Transmit timeout timer: (unused)\n");
        fprintf(out, "# 8) Receive only: -, +\n");
        fprintf(out, "# 9) Admit criteria: -, Free, Tone\n");
        fprintf(out, "# 10) Squelch level: Normal (unused)\n");
        fprintf(out, "# 11) Guard tone for receive, or '-' to disable\n");
        fprintf(out, "# 12) Guard tone for transmit, or '-' to disable\n");
        fprintf(out, "# 13) Bandwidth in kHz: 12.5, 25\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Analog  Name             Receive   Transmit Power Scan TOT RO Admit  Squelch RxTone TxTone Width");
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;
        if (ch->channel_mode != MODE_ANALOG && ch->channel_mode != MODE_A_D) {
            // Select analog channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print analog parameters of the channel:
        //      Admit Criteria
        //      Squelch
        //      CTCSS/DCS Dec
        //      CTCSS/DCS Enc
        //      Bandwidth
        fprintf(out, "%-6s ", ANALOG_ADMIT_NAME[ch->tx_permit]);
        fprintf(out, "%-7s ", "Normal");

        if (ch->rx_ctcss)
            print_ctcss(out, ch->ctcss_receive, ch->custom_ctcss);
        else if (ch->rx_dcs)
            print_dcs(out, ch->dcs_receive);
        else
            fprintf(out, "-    ");

        fprintf(out, "  ");
        if (ch->tx_ctcss)
            print_ctcss(out, ch->ctcss_transmit, ch->custom_ctcss);
        else if (ch->tx_dcs)
            print_dcs(out, ch->dcs_transmit);
        else
            fprintf(out, "-    ");

        fprintf(out, "  %s", BANDWIDTH[ch->bandwidth]);
        fprintf(out, "\n");
    }
}

//
// Return true when any zones are present.
//
static int have_zones()
{
    uint8_t *zmap = GET_ZONEMAP();
    int i;

    for (i=0; i<(NZONES+7)/8; i++) {
        if (zmap[i] != 0)
            return 1;
    }
    return 0;
}

//
// Return true when any scanlists are present.
//
static int have_scanlists()
{
    uint8_t *slmap = GET_SCANL_MAP();
    int i;

    for (i=0; i<(NSCANL+7)/8; i++) {
        if (slmap[i] != 0)
            return 1;
    }
    return 0;
}

//
// Find a zone with given index.
// Return false when zone is not valid.
// Set zname and zlist to a zone name and member list.
//
static int get_zone(int i, uint8_t **zname, uint16_t **zlist)
{
    uint8_t *zmap = GET_ZONEMAP();

    if ((zmap[i / 8] >> (i & 7)) & 1) {
        // Zone is valid.
        *zname = GET_ZONENAME(i);
        *zlist = GET_ZONELIST(i);
        return 1;
    } else {
        return 0;
    }
}

//
// Get scanlist by index.
//
static scanlist_t *get_scanlist(int i)
{
    uint8_t *slmap = GET_SCANL_MAP();

    if ((slmap[i / 8] >> (i & 7)) & 1)
        return GET_SCANLIST(i);

    return 0;
}

static void print_chanlist16(FILE *out, uint16_t *unsorted, int nchan)
{
    int last  = -1;
    int range = 0;
    int n;
    uint16_t data[nchan];

    // Sort the list before printing.
    memcpy(data, unsorted, nchan * sizeof(uint16_t));
    qsort(data, nchan, sizeof(uint16_t), compare_index_ffff);
    for (n=0; n<nchan; n++) {
        int cnum = data[n];

        if (cnum == 0xffff)
            break;
        cnum++;

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

static void print_chanlist32(FILE *out, uint32_t *unsorted, int nchan)
{
    int last  = -1;
    int range = 0;
    int n;
    uint32_t data[nchan];

    // Sort the list before printing.
    memcpy(data, unsorted, nchan * sizeof(uint32_t));
    qsort(data, nchan, sizeof(uint32_t), compare_index_ffffffff);
    for (n=0; n<nchan; n++) {
        int cnum = data[n];
        if (cnum == 0xffffffff)
            break;
        cnum++;

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

static int have_grouplists()
{
    int i;

    for (i=0; i<NGLISTS; i++) {
        grouplist_t *gl = GET_GROUPLIST(i);

        if (VALID_GROUPLIST(gl))
            return 1;
    }
    return 0;
}

static int have_messages()
{
    int i;

    for (i=0; i<NMESSAGES; i++) {
        uint8_t *msg = GET_MESSAGE(i);

        if (VALID_TEXT(msg))
            return 1;
    }
    return 0;
}

//
// Print full information about the device configuration.
//
static void d868uv_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    int i;

    fprintf(out, "Radio: %s\n", radio->name);
    if (verbose)
        d868uv_print_version(radio, out);

    //
    // Channels.
    //
    if (have_channels(MODE_DIGITAL)) {
        fprintf(out, "\n");
        print_digital_channels(out, verbose);
    }
    if (have_channels(MODE_ANALOG)) {
        fprintf(out, "\n");
        print_analog_channels(out, verbose);
    }

    //
    // Zones.
    //
    if (have_zones()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of channel zones.\n");
            fprintf(out, "# 1) Zone number: 1-%d\n", NZONES);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Zone    Name             Channels\n");
        for (i=0; i<NZONES; i++) {
            uint8_t *zname;
            uint16_t *zlist;

            if (!get_zone(i, &zname, &zlist)) {
                // Zone is disabled.
                continue;
            }

            fprintf(out, "%5d   ", i + 1);
            print_ascii(out, zname, 16, 1);
            fprintf(out, " ");
            if (*zlist != 0xffff) {
                print_chanlist16(out, zlist, 250);
            } else {
                fprintf(out, "-");
            }
            fprintf(out, "\n");
        }
    }

    //
    // Scan lists.
    //
    if (have_scanlists()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of scan lists.\n");
            fprintf(out, "# 1) Scan list number: 1-%d\n", NSCANL);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) Priority channel 1: -, Curr or index\n");
            fprintf(out, "# 4) Priority channel 2: -, Curr or index\n");
            fprintf(out, "# 5) Designated transmit channel: Sel or Last\n");
            fprintf(out, "# 6) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Scanlist Name            PCh1 PCh2 TxCh Channels\n");
        for (i=0; i<NSCANL; i++) {
            scanlist_t *sl = get_scanlist(i);

            if (!sl) {
                // Scan list is disabled.
                continue;
            }

            fprintf(out, "%5d   ", i + 1);
            print_ascii(out, sl->name, 16, 1);

            if ((sl->prio_ch_select == PRIO_CHAN_SEL1 ||
                 sl->prio_ch_select == PRIO_CHAN_SEL12) &&
                sl->priority_ch1 != 0xffff) {
                if (sl->priority_ch1 == 0) {
                    fprintf(out, " Curr ");
                } else {
                    fprintf(out, " %-4d ", sl->priority_ch1);
                }
            } else {
                fprintf(out, " -    ");
            }

            if ((sl->prio_ch_select == PRIO_CHAN_SEL2 ||
                 sl->prio_ch_select == PRIO_CHAN_SEL12) &&
                sl->priority_ch2 != 0xffff) {
                if (sl->priority_ch2 == 0) {
                    fprintf(out, "Curr ");
                } else {
                    fprintf(out, "%-4d ", sl->priority_ch2);
                }
            } else {
                fprintf(out, "-    ");
            }

            if (sl->revert_channel == REVCH_LAST_CALLED) {
                fprintf(out, "Last ");
            } else {
                fprintf(out, "Sel  ");
            }

            if (sl->member[0] != 0xffff) {
                print_chanlist16(out, sl->member, 50);
            } else {
                fprintf(out, "-");
            }
            fprintf(out, "\n");
        }
    }

    //
    // Contacts.
    //
    if (have_contacts()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of contacts.\n");
            fprintf(out, "# 1) Contact number: 1-%d\n", NCONTACTS);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) Call type: Group, Private, All\n");
            fprintf(out, "# 4) Call ID: 1...16777215\n");
            fprintf(out, "# 5) Incoming call alert: -, +, Online\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Contact Name             Type    ID       RxTone\n");
        for (i=0; i<NCONTACTS; i++) {
            contact_t *ct = get_contact(i);

            if (!ct) {
                // Contact is disabled
                continue;
            }

            fprintf(out, "%5d   ", i+1);
            print_ascii(out, ct->name, 16, 1);
            fprintf(out, " %-7s %-8d %s\n", CONTACT_TYPE[ct->type & 3],
                CONTACT_ID(ct), ALERT_TYPE[ct->call_alert & 3]);
        }
    }

    //
    // Group lists.
    //
    if (have_grouplists()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of group lists.\n");
            fprintf(out, "# 1) Group list number: 1-%d\n", NGLISTS);
            fprintf(out, "# 2) Name: up to 35 characters, use '_' instead of space\n");
            fprintf(out, "# 3) List of contacts: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Grouplist Name                              Contacts\n");
        for (i=0; i<NGLISTS; i++) {
            grouplist_t *gl = GET_GROUPLIST(i);

            if (!VALID_GROUPLIST(gl)) {
                // Group list is disabled.
                continue;
            }

            fprintf(out, "%5d   ", i + 1);
            print_ascii(out, gl->name, 35, 1);
            fprintf(out, " ");
            print_chanlist32(out, gl->member, 64);
            fprintf(out, "\n");
        }
    }

    //
    // Text messages.
    //
    if (have_messages()) {
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of text messages.\n");
            fprintf(out, "# 1) Message number: 1-%d\n", NMESSAGES);
            fprintf(out, "# 2) Text: up to 200 characters\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Message Text\n");
        for (i=0; i<NMESSAGES; i++) {
            uint8_t *msg = GET_MESSAGE(i);

            if (!VALID_TEXT(msg)) {
                // Message is disabled
                continue;
            }

            fprintf(out, "%5d   ", i+1);
            print_ascii(out, msg, 200, 0);
            fprintf(out, "\n");
        }
    }

    // General settings.
    print_id(out, verbose);
    print_intro(out, verbose);
}

//
// Read memory image from the binary file.
//
static void d868uv_read_image(radio_device_t *radio, FILE *img)
{
    struct stat st;

    // Guess device type by file size.
    if (fstat(fileno(img), &st) < 0) {
        fprintf(stderr, "Cannot get file size.\n");
        exit(-1);
    }
    switch (st.st_size) {
    case MEMSZ:
        // IMG file.
        if (fread(&radio_mem[0], 1, MEMSZ, img) != MEMSZ) {
            fprintf(stderr, "Error reading image data.\n");
            exit(-1);
        }
        break;
    default:
        fprintf(stderr, "Unrecognized file size %u bytes.\n", (int) st.st_size);
        exit(-1);
    }
}

//
// Save memory image to the binary file.
//
static void d868uv_save_image(radio_device_t *radio, FILE *img)
{
    fwrite(&radio_mem[0], 1, MEMSZ, img);
}

//
// Parse the scalar parameter.
//
static void d868uv_parse_parameter(radio_device_t *radio, char *param, char *value)
{
    if (strcasecmp("Radio", param) == 0) {
        if (!radio_is_compatible(value)) {
            fprintf(stderr, "Incompatible model: %s\n", value);
            exit(-1);
        }
        return;
    }

    radioid_t *ri = GET_RADIOID();
    if (strcasecmp ("Name", param) == 0) {
        ascii_decode(ri->name, value, 16, 0);
        return;
    }
    if (strcasecmp ("ID", param) == 0) {
        uint32_t id = strtoul(value, 0, 0);
        ri->id[0] = ((id / 10000000) << 4) | ((id / 1000000) % 10);
        ri->id[1] = ((id / 100000 % 10) << 4) | ((id / 10000) % 10);
        ri->id[2] = ((id / 1000 % 10) << 4) | ((id / 100) % 10);
        ri->id[3] = ((id / 10 % 10) << 4) | (id % 10);
        return;
    }

    general_settings_t *gs = GET_SETTINGS();
    if (strcasecmp ("Intro Line 1", param) == 0) {
        ascii_decode_uppercase(gs->intro_line1, value, 14, 0);
        gs->power_on = PWON_CUST_CHAR;
        return;
    }
    if (strcasecmp ("Intro Line 2", param) == 0) {
        ascii_decode_uppercase(gs->intro_line2, value, 14, 0);
        gs->power_on = PWON_CUST_CHAR;
        return;
    }
    fprintf(stderr, "Unknown parameter: %s = %s\n", param, value);
    exit(-1);
}

//
// Parse table header.
// Return table id, or 0 in case of error.
//
static int d868uv_parse_header(radio_device_t *radio, char *line)
{
    if (strncasecmp(line, "Digital", 7) == 0)
        return 'D';
    if (strncasecmp(line, "Analog", 6) == 0)
        return 'A';
    if (strncasecmp(line, "Zone", 4) == 0)
        return 'Z';
    if (strncasecmp(line, "Scanlist", 8) == 0)
        return 'S';
    if (strncasecmp(line, "Contact", 7) == 0)
        return 'C';
    if (strncasecmp(line, "Grouplist", 9) == 0)
        return 'G';
    if (strncasecmp(line, "Message", 7) == 0)
        return 'M';
    return 0;
}

//
// Parse one line of table data.
// Return 0 on failure.
//
static int d868uv_parse_row(radio_device_t *radio, int table_id, int first_row, char *line)
{
    //TODO
#if 0
    switch (table_id) {
    case 'D': return parse_digital_channel(radio, first_row, line);
    case 'A': return parse_analog_channel(radio, first_row, line);
    case 'Z': return parse_zones(first_row, line);
    case 'S': return parse_scanlist(first_row, line);
    case 'C': return parse_contact(first_row, line);
    case 'G': return parse_grouplist(first_row, line);
    case 'M': return parse_messages(first_row, line);
    }
#endif
    return 0;
}

//
// Update timestamp.
//
static void d868uv_update_timestamp(radio_device_t *radio)
{
    // No timestamp.
}

//
// Check that configuration is correct.
// Return 0 on error.
//
static int d868uv_verify_config(radio_device_t *radio)
{
    //TODO
#if 0
    int i, k, nchannels = 0, nzones = 0, nscanlists = 0, ngrouplists = 0;
    int ncontacts = 0, nerrors = 0;

    // Channels: check references to scanlists, contacts and grouplists.
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;

        nchannels++;
        if (ch->scan_list_index != 0) {
            scanlist_t *sl = get_scanlist(ch->scan_list_index - 1);

            if (!VALID_SCANLIST(sl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': scanlist %d not found.\n", ch->scan_list_index);
                nerrors++;
            }
        }
        if (ch->contact_index != 0) {
            contact_t *ct = get_contact(ch->contact_index - 1);

            if (!ct) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': contact %d not found.\n", ch->contact_index);
                nerrors++;
            }
        }
        if (ch->group_list_index != 0) {
            grouplist_t *gl = GET_GROUPLIST(ch->group_list_index - 1);

            if (!VALID_GROUPLIST(gl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': grouplist %d not found.\n", ch->group_list_index);
                nerrors++;
            }
        }
    }

    // Zones: check references to channels.
    for (i=0; i<NZONES; i++) {
        uint8_t *zname;
        uint16_t *zlist;

        if (!get_zone(i, &zname, &zlist))
            continue;

        nzones++;

        for (k=0; k<250; k++) {
            int cnum = zlist[k] + 1;

            if (cnum != 0xffff) {
                channel_t *ch = get_channel(cnum - 1);

                if (!ch) {
                    fprintf(stderr, "Zone %da '", i+1);
                    print_ascii(stderr, zname, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Scanlists: check references to channels.
    for (i=0; i<NSCANL; i++) {
        scanlist_t *sl = get_scanlist(i);

        if (!sl)
            continue;

        nscanlists++;
        for (k=0; k<31; k++) {
            int cnum = sl->member[k];

            if (cnum != 0) {
                channel_t *ch = get_channel(cnum - 1);

                if (!ch) {
                    fprintf(stderr, "Scanlist %d '", i+1);
                    print_ascii(stderr, sl->name, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Grouplists: check references to contacts.
    for (i=0; i<NGLISTS; i++) {
        grouplist_t *gl = GET_GROUPLIST(i);

        if (!VALID_GROUPLIST(gl))
            continue;

        ngrouplists++;
        for (k=0; k<64; k++) {
            int cnum = gl->member[k];

            if (cnum != 0xffffffff) {
                contact_t *ct = get_contact(cnum);

                if (!ct) {
                    fprintf(stderr, "Grouplist %d '", i+1);
                    print_ascii(stderr, gl->name, 35, 0);
                    fprintf(stderr, "': contact %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Count contacts.
    for (i=0; i<NCONTACTS; i++) {
        contact_t *ct = get_contact(i);

        if (ct)
            ncontacts++;
    }

    if (nerrors > 0) {
        fprintf(stderr, "Total %d errors.\n", nerrors);
        return 0;
    }
    fprintf(stderr, "Total %d channels, %d zones, %d scanlists, %d contacts, %d grouplists.\n",
        nchannels, nzones, nscanlists, ncontacts, ngrouplists);
#endif
    return 1;
}

//
// TYT MD-UV380
//
radio_device_t radio_d868uv = {
    "Anytone AT-D868UV",
    d868uv_download,
    d868uv_upload,
    d868uv_is_compatible,
    d868uv_read_image,
    d868uv_save_image,
    d868uv_print_version,
    d868uv_print_config,
    d868uv_verify_config,
    d868uv_parse_parameter,
    d868uv_parse_header,
    d868uv_parse_row,
    d868uv_update_timestamp,
    //TODO: d868uv_write_csv,
};
