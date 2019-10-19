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
#define NRADIOIDS       250
#define NSCANL          250
#define NMESSAGES       100
#define NCALLSIGNS      160000
#define CALLSIGN_SIZE   (12*1024*1024)  // Size of callsign data

//
// Offsets in the image file.
//
#define OFFSET_BANK1        0x000040    // Channels
#define OFFSET_ZONELISTS    0x03e8c0    // Channel lists of zones
#define OFFSET_SCANLISTS    0x05dcc0    // Scanlists
#define OFFSET_MESSAGES     0x069f40    // Messages
#define OFFSET_ENCRYPT_KEY  0x070c40	// Encryption keys
#define OFFSET_ZONE_MAP     0x070940    // Bitmap of valid zones
#define OFFSET_SCANL_MAP    0x070980    // Bitmap of valid scanlists
#define OFFSET_CHAN_MAP     0x070a40    // Bitmap of valid channels
#define OFFSET_SETTINGS     0x071600    // General settings
#define OFFSET_ZCHAN_A      0x071700    // Zone A channel
#define OFFSET_ZCHAN_B      0x071900    // Zone B channel
#define OFFSET_ZONENAMES    0x071dc0    // Names of zones
#define OFFSET_RADIOID      0x073d00    // Table of radio IDs
#define OFFSET_RADIOID_MAP  0x070960    // Bitmap of enabled radio IDs
#define OFFSET_CONTACT_LIST 0x076500    // List of valid contact indices
#define OFFSET_CONTACT_MAP  0x080140    // Bitmap of invalid contacts
#define OFFSET_CONTACTS     0x080640    // Contacts
#define OFFSET_GLISTS       0x174b00    // RX group lists

//
// Addresses in the radio flash memory.
//
#define ADDR_CALLDB_LIST    0x04000000  // Map of callsign database
#define ADDR_CONT_ID_LIST   0x04280000  // Map of contact IDs to contacts
#define ADDR_CALLDB_SIZE    0x044c0000  // Sizes of callsign database
#define ADDR_CALLDB_DATA    0x04500000  // Data of callsign database

#define GET_SETTINGS()      ((general_settings_t*) &radio_mem[OFFSET_SETTINGS])
#define GET_RADIOID(i)      ((radioid_t*) &radio_mem[OFFSET_RADIOID + (i)*32])
#define GET_ZONEMAP()       (&radio_mem[OFFSET_ZONE_MAP])
#define GET_CONTACT_MAP()   (&radio_mem[OFFSET_CONTACT_MAP])
#define GET_RADIOID_MAP()   (&radio_mem[OFFSET_RADIOID_MAP])
#define GET_CONTACT_LIST()  ((uint32_t*) &radio_mem[OFFSET_CONTACT_LIST])
#define GET_SCANL_MAP()     (&radio_mem[OFFSET_SCANL_MAP])
#define GET_ZONENAME(i)     (&radio_mem[OFFSET_ZONENAMES + (i)*32])
#define GET_ZONE_CHAN_A(i)  ((i) + (uint16_t*) &radio_mem[OFFSET_ZCHAN_A])
#define GET_ZONE_CHAN_B(i)  ((i) + (uint16_t*) &radio_mem[OFFSET_ZCHAN_B])
#define GET_ZONELIST(i)     ((uint16_t*) &radio_mem[OFFSET_ZONELISTS + (i)*512])
#define GET_CONTACT(i)      ((contact_t*) &radio_mem[OFFSET_CONTACTS + (i)*100])
#define GET_GROUPLIST(i)    ((grouplist_t*) &radio_mem[OFFSET_GLISTS + (i)*320])
#define GET_SCANLIST(i)     ((scanlist_t*) &radio_mem[OFFSET_SCANLISTS + (i)*192])
#define GET_MESSAGE(i)      ((uint8_t*) &radio_mem[OFFSET_MESSAGES + (i)*256])
#define GET_ENC_KEY(i)      ((uint8_t*) &radio_mem[OFFSET_ENCRYPT_KEY + (i)*2])

#define VALID_TEXT(txt)     (*(txt) != 0 && *(txt) != 0xff)
#define VALID_GROUPLIST(gl) ((gl)->member[0] != 0xffffffff && VALID_TEXT((gl)->name))

//
// Size of memory image.
// Essentialy a sum of all fragments defined ind868um-map.h.
//
#define MEMSZ           1606528

//
// D868UV radio has a huge internal address space, more than 64 Mbytes.
// The configuration data are dispersed over this space.
// Here is a table of fragments: starting address and length.
// We read these fragments and save them into a file continuously.
//
typedef struct {
    unsigned address;
    unsigned length;
    unsigned offset;
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
    uint16_t    dcs_transmit;           // DCS Encode: 0=D000N, 17=D021N, 1023=D777I
    uint16_t    dcs_receive;            // DCS Decode: 0=D000N, 17=D021N, 1023=D777I

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

//
// Entry of callsign map: 8 bytes.
//
typedef struct {
    uint32_t    id;                     // DMR ID
    uint32_t    offset;                 // Offset in the callsign data blob
} callsign_map_t;

//
// Sizes of callsign database.
//
typedef struct {
    uint32_t    count;                  // Number of records
    uint32_t    last;                   // Last address of data blob
    uint32_t    _unused3;
    uint32_t    _unused4;
} callsign_sizes_t;

static const char *POWER_NAME[] = { "Low", "Mid", "High", "Turbo" };
static const char *DIGITAL_ADMIT_NAME[] = { "-", "Free", "NColor", "Color" };
static const char *ANALOG_ADMIT_NAME[] = { "-", "Free", "Tone", "Tone" };
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
// Return true when the specified region has to be skipped.
// Skip unused channels, contacts, zones and scanlists.
//
static int skip_region(unsigned addr, unsigned file_offset, uint8_t *mem, unsigned nbytes)
{
    int index;

    // Channels.
    if (addr >= 0x00800000 && addr < 0x01000000) {
        index = (file_offset - OFFSET_BANK1) / 64;
        if (index < NCHAN) {
            uint8_t *bitmap = &radio_mem[OFFSET_CHAN_MAP];

            if ((bitmap[index / 8] >> (index & 7)) & 1) {
                // Channel is valid, don't skip.
                return 0;
            }

            // Invalid channel: skip it, erase data.
            if (mem) {
                memset(mem, 0xff, nbytes);
            }
            return 1;
        }
    }

    // Contacts.
    if (addr >= 0x02680000 && addr < 0x02900000) {
        index = (file_offset - OFFSET_CONTACTS) / 100;
        if (index < NCONTACTS) {
            uint8_t *cmap = GET_CONTACT_MAP();

            if ((cmap[index / 8] >> (index & 7)) & 1) {
                // Invalid contact: skip it, erase data.
                if (mem) {
                    memset(mem, 0xff, nbytes);
                }
                return 1;
            }

            // Contact is valid, don't skip.
            return 0;
        }
    }

    // Zones.
    if (addr >= 0x01000000 && addr < 0x01080000) {
        index = (file_offset - OFFSET_ZONELISTS) / 512;
        if (index < NZONES) {
            uint8_t *zmap = GET_ZONEMAP();

            if ((zmap[index / 8] >> (index & 7)) & 1) {
                // Zone is valid, don't skip.
                return 0;
            }

            // Invalid zone: skip it, erase data.
            if (mem) {
                memset(mem, 0xff, nbytes);
            }
            return 1;
        }
    }

    // Scanlists.
    if (addr >= 0x01080000 && addr < 0x01640000) {
        index = (file_offset - OFFSET_SCANLISTS) / 192;
        if (index < NSCANL) {
            uint8_t *slmap = GET_SCANL_MAP();

            if ((slmap[index / 8] >> (index & 7)) & 1) {
                // Scanlist is valid, don't skip.
                return 0;
            }

            // Invalid scanlist: skip it, erase data.
            if (mem) {
                memset(mem, 0xff, nbytes);
            }
            return 1;
        }
    }
    return 0;
}

//
// Read memory image from the device.
//
static void d868uv_download(radio_device_t *radio)
{
    fragment_t *f;

    // Read bitmaps first.
    for (f=region_map; f->length; f++) {
        if (f->offset != 0) {
            serial_read_region(f->address, &radio_mem[f->offset], f->length);
        }
    }

    // Read other regions sequentially.
    unsigned file_offset = 0;
    unsigned bytes_transferred = 0;
    unsigned last_printed = 0;
    //printf("Address     Offset\n");
    for (f=region_map; f->length; f++) {
        unsigned addr = f->address;
        unsigned nbytes = f->length;

        //printf("%08x    %06x\n", addr, file_offset);
        while (nbytes > 0) {
            unsigned n = (nbytes > 64) ? 64 : nbytes;

            if (! skip_region(addr, file_offset, &radio_mem[file_offset], n)) {
                if (f->offset == 0)
                    serial_read_region(addr, &radio_mem[file_offset], n);
                bytes_transferred += n;
            }
            file_offset += n;
            addr += n;
            nbytes -= n;

            if (bytes_transferred / (32*1024) != last_printed) {
                fprintf(stderr, "#");
                fflush(stderr);
                last_printed = bytes_transferred / (32*1024);
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
// Write memory image to the device.
//
static void d868uv_upload(radio_device_t *radio, int cont_flag)
{
    fragment_t *f;
    unsigned file_offset = 0;
    unsigned bytes_transferred = 0;
    unsigned last_printed = 0;

    for (f=region_map; f->length; f++) {
        unsigned addr = f->address;
        unsigned nbytes = f->length;

        while (nbytes > 0) {
            unsigned n = (nbytes > 64) ? 64 : nbytes;

            if (! skip_region(addr, file_offset, 0, 0)) {
                serial_write_region(addr, &radio_mem[file_offset], n);
                bytes_transferred += n;
            }
            file_offset += n;
            addr += n;
            nbytes -= n;

            if (bytes_transferred / (32*1024) != last_printed) {
                fprintf(stderr, "#");
                fflush(stderr);
                last_printed = bytes_transferred / (32*1024);
            }
        }
    }
    if (file_offset != MEMSZ) {
        fprintf(stderr, "\nWrong MEMSZ=%u for D868UV!\n", MEMSZ);
        fprintf(stderr, "Should be %u; check d868uv-map.h!\n", file_offset);
        exit(-1);
    }

    //
    // Build and upload a map of IDs to contacts.
    // The map has to be sorted by ID.
    //
    uint64_t map[8*NCONTACTS + 72];
    int index, ncontacts = 0;

    memset(map, 0xff, sizeof(map));
    for (index=0; index<NCONTACTS; index++) {
        contact_t *ct = get_contact(index);
        if (!ct)
            continue;

        // A map entry consists of 8 bytes.
        // Bit 0: set for groups.
        // Bits 31-1: DMR ID of the contact.
        // Bytes 4-7: contact index.
        uint64_t item = ct->id[0] << 25 | ct->id[1] << 17 |
                        ct->id[2] << 9 | ct->id[3] << 1;
        if (ct->type == CALL_GROUP)
            item |= 1;
        item |= (uint64_t) index << 32;

        int k;
        for (k=0; k<NCONTACTS; k++) {
            if (map[k] == item) {
                // The item is already in the list.
                break;
            }
            if (map[k] == 0xffffffffffffffff) {
                // Append to the end of the list.
                map[k] = item;
                ncontacts = k + 1;
                break;
            }
            if ((uint32_t)map[k] > (uint32_t)item) {
                // Insert item there and shift the rest.
                uint64_t prev = map[k];
                map[k] = item;
                item = prev;
            }
        }
    }
    //printf("\n");
    //print_hex((uint8_t*)map, ncontacts*8 + 8);
    //printf("\n");
    serial_write_region(ADDR_CONT_ID_LIST, (uint8_t*)map, (ncontacts*8 + 8 + 63) / 64 * 64);
}

//
// Check whether the memory image is compatible with this device.
//
static int d868uv_is_compatible(radio_device_t *radio)
{
    if (memcmp("D868UVE", (char*)&radio_mem[0], 7) == 0)
        return 1;
    if (memcmp("D878UV", (char*)&radio_mem[0], 6) == 0)
        return 1;
    if (memcmp("D6X2UV", (char*)&radio_mem[0], 6) == 0)
        return 1;
    return 0;
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
// Return scan list index for specified channel.
// It depends on radio type.
//
static int get_scanlist_index(radio_device_t *radio, channel_t *ch)
{
    if (radio == &radio_dmr6x2) {
        // Radio DMR-6x2 has eight scan lists per channel.
        return ch->aprs_channel;
    } else {
        return ch->scan_list_index;
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
static void print_chan_base(FILE *out, radio_device_t *radio, channel_t *ch, int cnum)
{
    fprintf(out, "%5d   ", cnum);
    print_ascii(out, ch->name, 16, 1);
    fprintf(out, " ");
    print_rx_freq(out, ch->rx_frequency);
    fprintf(out, " ");
    print_tx_offset(out, ch->tx_offset, ch->repeater_mode);

    fprintf(out, "%-5s ", POWER_NAME[ch->power]);

    int scanlist_index = get_scanlist_index(radio, ch);
    if (scanlist_index == 0xff)
        fprintf(out, "-    ");
    else
        fprintf(out, "%-4d ", scanlist_index + 1);

    // Transmit timeout timer on D868UV is configured globally,
    // not per channel. So we don't print it here.
    fprintf(out, "-   ");

    fprintf(out, "%c  ", "-+"[ch->rx_only]);
}

static void print_digital_channels(FILE *out, radio_device_t *radio, int verbose)
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
        fprintf(out, "# 10) Encryption Type (- (Off) or Norm or Enh.)\n");
        fprintf(out, "# 11) Encryption Key (- (Off) or 1-32)\n");
        fprintf(out, "# 12) Color code: 0, 1, 2, 3... 15\n");
        fprintf(out, "# 13) Time slot: 1 or 2\n");
        fprintf(out, "# 14) Receive group list: - or index in Grouplist table\n");
        fprintf(out, "# 15) Radio ID\n");
        fprintf(out, "# 16) Contact for transmit: - or index in Contacts table\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Digital Name             Receive   Transmit Power Scan TOT RO Admit EncType EncKey  Color Slot RxGL RadioID TxContact");
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;
        if (ch->channel_mode != MODE_DIGITAL && ch->channel_mode != MODE_D_A) {
            // Select digital channels
            continue;
        }
        print_chan_base(out, radio, ch, i+1);

        // Print digital parameters of the channel:
        //      Admit Criteria
        //      Encryption Type
        //      Encryption Key
        //      Color Code
        //      Repeater Slot
        //      Group List
	//      Radio ID
        //      Contact Name
        fprintf(out, "%-5s ", DIGITAL_ADMIT_NAME[ch->tx_permit]);

	if (ch->encryption == 0) {
	    fprintf(out, "-       -       ");
	} else {
            fprintf(out, "%-6s  ", ch->enh_encryption == 1 ? "Enh." : "Norm");
	    fprintf(out, "%-6d  ", ch->encryption);
	}

	fprintf(out, "%-5d %-3d  ", ch->color_code, 1 + ch->slot2);

        if (ch->group_list_index == 0xff)
            fprintf(out, "-    ");
        else
            fprintf(out, "%-4d ", ch->group_list_index + 1);
	
	fprintf(out, "%-7d ", ch->id_index);

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

static void print_analog_channels(FILE *out, radio_device_t *radio, int verbose)
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
        print_chan_base(out, radio, ch, i+1);

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
        print_digital_channels(out, radio, verbose);
    }
    if (have_channels(MODE_ANALOG)) {
        fprintf(out, "\n");
        print_analog_channels(out, radio, verbose);
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

    //
    // Radio IDs.
    //
        fprintf(out, "\n");
        if (verbose) {
            fprintf(out, "# Table of RadioIDs.\n");
            fprintf(out, "# 1) RadioID Index number (upto %d)\n", NRADIOIDS);
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) Call ID: 1...16777215\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "RadioId Name             ID       \n");

        uint8_t *rmap = GET_RADIOID_MAP();
        for (i=0; i<NRADIOIDS; i++) {
            radioid_t *ri = GET_RADIOID(i);

            if (!((rmap[i / 8] >> (i & 7)) & 1)) {
                // RadioID is disabled
                continue;
            }

            fprintf(out, "%3d     ", i);
            print_ascii(out, ri->name, 16, 1);
            fprintf(out, " %-8d\n", GET_ID(ri->id));
        }

    // General settings.
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
// Check that the radio does support this frequency.
//
static int is_valid_frequency(int mhz)
{
    if (mhz >= 136 && mhz <= 174)
        return 1;
    if (mhz >= 400 && mhz <= 480)
        return 1;
    return 0;
}

//
// Find CTCSS value in standard table.
// Otherwise return NCTCSS.
//
static int ctcss_index(unsigned value)
{
    int i;

    for (i=0; i<NCTCSS; i++)
        if (value == CTCSS_TONES[i])
            break;
    return i;
}

//
// Set the parameters for a given memory channel.
//
static void setup_channel(radio_device_t *radio, int i, int mode, char *name,
    double rx_mhz, double tx_mhz, int power, int scanlist, int rxonly,
    int admit, int enc_type, int enc_key, int colorcode, int timeslot, int grouplist, int radioid, int contact,
    int rxtone, int txtone, int width)
{
    channel_t *ch     = get_bank(i >> 7) + (i % 128);
    uint8_t   *bitmap = &radio_mem[OFFSET_CHAN_MAP];

    bitmap[i / 8] |= 1 << (i & 7);

    memset(ch, 0, sizeof(channel_t));
    ascii_decode(ch->name, name, 16, 0);

    ch->rx_frequency = mhz_to_ghefcdab(rx_mhz);
    if (tx_mhz > rx_mhz) {
        ch->repeater_mode   = RM_TXPOS;
        ch->tx_offset       = mhz_to_ghefcdab(tx_mhz - rx_mhz);
    } else if (tx_mhz < rx_mhz) {
        ch->repeater_mode   = RM_TXNEG;
        ch->tx_offset       = mhz_to_ghefcdab(rx_mhz - tx_mhz);
    } else {
        ch->repeater_mode   = RM_SIMPLEX;
        ch->tx_offset       = 0x00000100;
    }

    ch->channel_mode        = mode;
    ch->power               = power;
    ch->bandwidth           = width;
    ch->rx_only             = rxonly;
    ch->slot2               = (timeslot == 2);
    ch->color_code          = colorcode;
    ch->tx_permit           = admit;
    ch->enh_encryption      = enc_type;
    ch->id_index	    = radioid;
    ch->encryption          = enc_key;
    ch->contact_index       = contact - 1;
    ch->group_list_index    = grouplist - 1;
    ch->custom_ctcss        = 251.1 * 10;

    if (radio == &radio_dmr6x2) {
        // Radio DMR-6x2 has eight scan lists per channel.
        ch->scan_list_index = 0;            // Channel Measure Mode = Off
        ch->aprs_channel    = scanlist - 1; // Scan list 1
        memset(ch->_unused55, 0xff, 7);     // Scan lists 2-8 = Disable
    } else {
        ch->scan_list_index = scanlist - 1;
    }

    // rxtone and txtone are positive for DCS and negative for CTCSS.
    if (rxtone > 0) {                   // Receive DCS
        ch->rx_dcs = 1;
        ch->dcs_receive = rxtone - 1;
    } else if (rxtone < 0) {            // Receive CTCSS
        ch->rx_ctcss = 1;
        ch->ctcss_receive = ctcss_index(-rxtone);
        if (ch->ctcss_receive == NCTCSS) {
            ch->custom_ctcss = -rxtone;
        }
    }

    if (ch->rx_ctcss == 0 && ch->rx_dcs == 0) {
        ch->squelch_mode = SQ_CARRIER;
    } else {
        ch->squelch_mode = SQ_TONE;
    }

    if (txtone > 0) {                   // Transmit DCS
        ch->tx_dcs = 1;
        ch->dcs_transmit = txtone - 1;
    } else if (txtone < 0) {            // Transmit CTCSS
        ch->tx_ctcss = 1;
        ch->ctcss_transmit = ctcss_index(-txtone);
        if (ch->ctcss_transmit == NCTCSS) {
            ch->custom_ctcss = -txtone;
        }
    }
}

//
// Erase all channels.
//
static void erase_channels()
{
    memset(&radio_mem[OFFSET_BANK1], 0xff, NCHAN * 64);
    memset(&radio_mem[OFFSET_CHAN_MAP], 0, (NCHAN + 7) / 8);
}

//
// Erase all zones.
//
static void erase_zones()
{
    int i;

    for (i=0; i<NZONES; i++) {
        memset(GET_ZONENAME(i), 0xff, 16);
        memset(GET_ZONELIST(i), 0xff, 2*250);
    }
    memset(GET_ZONEMAP(), 0, (NZONES + 7) / 8);
}

//
// Erase all scanlists.
//
static void erase_scanlists()
{
    int i;

    for (i=0; i<NSCANL; i++) {
        memset(GET_SCANLIST(i), 0xff, 192);
    }
    memset(GET_SCANL_MAP(), 0, (NSCANL + 7) / 8);
}

//
// Parse one line of Digital channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_digital_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256];
    char enc_type_str[256], enc_key_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256], colorcode_str[256];
    char slot_str[256], grouplist_str[256], radioid_str[256], contact_str[256];
    int num, power, scanlist, rxonly, admit;
    int enc_type, enc_key;
    int colorcode, timeslot, grouplist, radioid, contact;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str,
        tot_str, rxonly_str, admit_str, enc_type_str, enc_key_str, colorcode_str,
        slot_str, grouplist_str, radioid_str, contact_str) != 16)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        !is_valid_frequency(rx_mhz)) {
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

    if (strcasecmp("High", power_str) == 0) {
        power = POWER_HIGH;
    } else if (strcasecmp("Low", power_str) == 0) {
        power = POWER_LOW;
    } else if (strcasecmp("Mid", power_str) == 0) {
        power = POWER_MIDDLE;
    } else if (strcasecmp("Turbo", power_str) == 0) {
        power = POWER_TURBO;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (*scanlist_str == '-') {
        scanlist = 0;
    } else {
        scanlist = atoi(scanlist_str);
        if (scanlist == 0 || scanlist > NSCANL) {
            fprintf(stderr, "Bad scanlist.\n");
            return 0;
        }
    }

    // Ignore TOT.

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = PERMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {
        admit = PERMIT_CH_FREE;
    } else if (strcasecmp("Color", admit_str) == 0) {
        admit = PERMIT_CC_SAME;
    } else if (strcasecmp("NColor", admit_str) == 0) {
        admit = PERMIT_CC_DIFF;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
    }

    if (*enc_key_str == '-' || *enc_key_str == '0') {
	    enc_key = 0;
    } else {
	    enc_key = atoi(enc_key_str);
	    if (enc_key < 0 || enc_key > 32) {
 		fprintf(stderr, "Bad encryption Key.\n");
		return 0;
	    }
    }

    if (*enc_type_str == 'E' || *enc_type_str == 'e') {
	    enc_type = 1;
    } else {
	    enc_type = 0;
    }

    colorcode = atoi(colorcode_str);
    if (colorcode < 0 || colorcode > 15) {
        fprintf(stderr, "Bad color code.\n");
        return 0;
    }

    timeslot = atoi(slot_str);
    if (timeslot < 1 || timeslot > 2) {
        fprintf(stderr, "Bad timeslot.\n");
        return 0;
    }

    if (*grouplist_str == '-') {
        grouplist = 0;
    } else {
        grouplist = atoi(grouplist_str);
        if (grouplist == 0 || grouplist > NGLISTS) {
            fprintf(stderr, "Bad receive grouplist.\n");
            return 0;
        }
    }

    if (*radioid_str == '-') {
        radioid = 0;
    } else {
        radioid = atoi(radioid_str);
        if (radioid < 0 || radioid > NRADIOIDS) {
            fprintf(stderr, "Bad RadioID.\n");
            return 0;
        }
    }

    if (*contact_str == '-') {
        contact = 0;
    } else {
        contact = atoi(contact_str);
        if (contact == 0 || contact > NCONTACTS) {
            fprintf(stderr, "Bad transmit contact.\n");
            return 0;
        }
    }

    if (first_row && radio->channel_count == 0) {
        // On first entry, erase all channels, zones and scanlists.
        erase_channels();
        erase_zones();
        erase_scanlists();
    }

    setup_channel(radio, num-1, MODE_DIGITAL, name_str, rx_mhz, tx_mhz,
        power, scanlist, rxonly, admit, enc_type, enc_key, colorcode, timeslot,
        grouplist, radioid, contact, 0, 0, BW_12_5_KHZ);

    radio->channel_count++;
    return 1;
}

//
// Convert tone string to positive for DCS and negative for CTCSS.
// On error, return -1.
// Four possible formats:
// nnn.n - CTCSS frequency
// DnnnN - DCS normal
// DnnnI - DCS inverted
// '-'   - Disabled
//
static int encode_ctcss_dcs(char *str)
{
    int val;

    if (*str == '-') {
        // Disabled
        return 0;

    } else if (*str == 'D' || *str == 'd') {
        //
        // DCS tone
        //
        char *e;
        val = strtoul(++str, &e, 8);
        if (val < 0 || val > 511) {
            return -1;
        }

        if (*e == 'N' || *e == 'n') {
            val += 1;
        } else if (*e == 'I' || *e == 'i') {
            val += 513;
        } else {
            return -1;
        }
    } else if (*str >= '0' && *str <= '9') {
        //
        // CTCSS tone
        //
        float hz;
        if (sscanf(str, "%f", &hz) != 1)
            return -1;

        // Round to integer.
        val = hz * 10.0 + 0.5;
        val = -val;
    } else {
        return -1;
    }

    return val;
}

//
// Parse one line of Analog channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_analog_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256], squelch_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256];
    char rxtone_str[256], txtone_str[256], width_str[256];
    int num, power, scanlist, rxonly, admit;
    int rxtone, txtone, width;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str,
        tot_str, rxonly_str, admit_str, squelch_str,
        rxtone_str, txtone_str, width_str) != 13)
        return 0;

    num = atoi(num_str);
    if (num < 1 || num > NCHAN) {
        fprintf(stderr, "Bad channel number.\n");
        return 0;
    }

    if (sscanf(rxfreq_str, "%lf", &rx_mhz) != 1 ||
        !is_valid_frequency(rx_mhz)) {
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

    if (strcasecmp("High", power_str) == 0) {
        power = POWER_HIGH;
    } else if (strcasecmp("Low", power_str) == 0) {
        power = POWER_LOW;
    } else if (strcasecmp("Mid", power_str) == 0) {
        power = POWER_MIDDLE;
    } else if (strcasecmp("Turbo", power_str) == 0) {
        power = POWER_TURBO;
    } else {
        fprintf(stderr, "Bad power level.\n");
        return 0;
    }

    if (*scanlist_str == '-') {
        scanlist = 0;
    } else {
        scanlist = atoi(scanlist_str);
        if (scanlist == 0 || scanlist > NSCANL) {
            fprintf(stderr, "Bad scanlist.\n");
            return 0;
        }
    }

    // Ignore TOT.

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = PERMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {    // Busy Lock = Repeater
        admit = PERMIT_CH_FREE;
    } else if (strcasecmp("Tone", admit_str) == 0) {    // Busy Lock = Busy
        admit = PERMIT_CC_SAME;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
    }

    // Ignore squelch.

    rxtone = encode_ctcss_dcs(rxtone_str);
    if (rxtone == -1) {
        fprintf(stderr, "Bad receive tone.\n");
        return 0;
    }
    txtone = encode_ctcss_dcs(txtone_str);
    if (txtone == -1) {
        fprintf(stderr, "Bad transmit tone.\n");
        return 0;
    }

    if (strcasecmp ("12.5", width_str) == 0) {
        width = BW_12_5_KHZ;
    } else if (strcasecmp ("25", width_str) == 0) {
        width = BW_25_KHZ;
    } else {
        fprintf (stderr, "Bad width.\n");
        return 0;
    }

    if (first_row && radio->channel_count == 0) {
        // On first entry, erase all channels, zones and scanlists.
        erase_channels();
    }

    setup_channel(radio, num-1, MODE_ANALOG, name_str, rx_mhz, tx_mhz,
        power, scanlist, rxonly, admit, 0, 0, 0, 1,
        0, 0, 0, rxtone, txtone, width);

    radio->channel_count++;
    return 1;
}

//
// Set name for a given zone.
//
static void setup_zone(int index, const char *name)
{
    uint8_t *zmap = GET_ZONEMAP();

    zmap[index / 8] |= 1 << (index & 7);
    ascii_decode(GET_ZONENAME(index), name, 16, 0);
}

//
// Add channel to a zone.
// Return 0 on failure.
//
static int zone_append(int index, int cnum)
{
    uint16_t *zlist = GET_ZONELIST(index);
    uint16_t *zchan_a = GET_ZONE_CHAN_A(index);
    uint16_t *zchan_b = GET_ZONE_CHAN_B(index);
    int i;

    for (i=0; i<250; i++) {
        if (zlist[i] == cnum)
            return 1;
        if (zlist[i] == 0xffff) {
            zlist[i] = cnum;

            if (i == 0) {
                // Set A and B channels.
                zchan_a[index] = cnum;
                zchan_b[index] = cnum;
            } else if (i == 1) {
                // Set B channel.
                zchan_b[index] = cnum;
            }
            return 1;
        }
    }
    return 0;
}

//
// Parse one line of Zones table.
// Return 0 on failure.
//
static int parse_zones(int first_row, char *line)
{
    char num_str[256], name_str[256], chan_str[256];
    int znum;

    if (sscanf(line, "%s %s %s", num_str, name_str, chan_str) != 3)
        return 0;

    znum = strtoul(num_str, 0, 10);
    if (znum < 1 || znum > NZONES) {
        fprintf(stderr, "Bad zone number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Zones table.
        erase_zones();
    }

    setup_zone(znum-1, name_str);

    if (*chan_str != '-') {
        char *str   = chan_str;
        int   nchan = 0;
        int   range = 0;
        int   last  = 0;

        // Parse channel list.
        for (;;) {
            char *eptr;
            int cnum = strtoul(str, &eptr, 10);

            if (eptr == str) {
                fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", znum, str);
                return 0;
            }
            if (cnum < 1 || cnum > NCHAN) {
                fprintf(stderr, "Zone %d: wrong channel number %d.\n", znum, cnum);
                return 0;
            }

            if (range) {
                // Add range.
                int c;
                for (c=last+1; c<=cnum; c++) {
                    if (!zone_append(znum-1, c-1)) {
                        fprintf(stderr, "Zone %d: too many channels.\n", znum);
                        return 0;
                    }
                    nchan++;
                }
            } else {
                // Add single channel.
                if (!zone_append(znum-1, cnum-1)) {
                    fprintf(stderr, "Zone %d: too many channels.\n", znum);
                    return 0;
                }
                nchan++;
            }

            if (*eptr == 0)
                break;

            if (*eptr != ',' && *eptr != '-') {
                fprintf(stderr, "Zone %d: wrong channel list '%s'.\n", znum, eptr);
                return 0;
            }
            range = (*eptr == '-');
            last = cnum;
            str = eptr + 1;
        }
    }
    return 1;
}

//
// Set parameters for a given scan list.
//
static void setup_scanlist(int index, const char *name,
    int prio1, int prio2, int txchan)
{
    scanlist_t *sl = GET_SCANLIST(index);
    uint8_t *slmap = GET_SCANL_MAP();

    slmap[index / 8] |= 1 << (index & 7);
    memset(sl, 0, 192);
    memset(sl->member, 0xff, 100);
    ascii_decode(sl->name, name, 16, 0);

    sl->priority_ch1   = prio1;     // Priority Channel 1: 0=Current Channel, 0xffff=Off
    sl->priority_ch2   = prio2;     // Priority Channel 2: 0=Current Channel, 0xffff=Off
    sl->revert_channel = txchan;    // Revert Channel: Selected or Last Called

    if (prio2 != 0xffff) {          // Priority Channel Select
        if (prio1 != 0xffff) {
            sl->prio_ch_select = PRIO_CHAN_SEL12;
        } else {
            sl->prio_ch_select = PRIO_CHAN_SEL2;
        }
    } else {
        if (prio1 != 0xffff) {
            sl->prio_ch_select = PRIO_CHAN_SEL1;
        } else {
            sl->prio_ch_select = PRIO_CHAN_OFF;
        }
    }

    sl->look_back_a   = 20;         // Look Back Time A: 2.0s
    sl->look_back_b   = 30;         // Look Back Time B: 3.0s
    sl->dropout_delay = 31;         // Dropout Delay Time: 3.1s
    sl->dwell         = 31;         // Dwell Time: 3.1s
}

//
// Add channel to a zone.
// Return 0 on failure.
//
static int scanlist_append(int index, int cnum)
{
    scanlist_t *sl = GET_SCANLIST(index);
    int i;

    for (i=0; i<50; i++) {
        if (sl->member[i] == cnum-1)
            return 1;
        if (sl->member[i] == 0xffff) {
            sl->member[i] = cnum-1;
            return 1;
        }
    }
    return 0;
}

//
// Parse one line of Scanlist table.
// Return 0 on failure.
//
static int parse_scanlist(int first_row, char *line)
{
    char num_str[256], name_str[256], prio1_str[256], prio2_str[256];
    char tx_str[256], chan_str[256];
    int snum, prio1, prio2, txchan;

    if (sscanf(line, "%s %s %s %s %s %s",
        num_str, name_str, prio1_str, prio2_str, tx_str, chan_str) != 6)
        return 0;

    snum = atoi(num_str);
    if (snum < 1 || snum > NSCANL) {
        fprintf(stderr, "Bad scan list number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Scanlists table.
        erase_scanlists();
    }

    if (*prio1_str == '-') {
        prio1 = 0xffff;
    } else if (strcasecmp("Sel", prio1_str) == 0) {
        prio1 = 0;
    } else {
        prio1 = atoi(prio1_str);
        if (prio1 < 1 || prio1 > NCHAN) {
            fprintf(stderr, "Bad priority channel 1.\n");
            return 0;
        }
    }

    if (*prio2_str == '-') {
        prio2 = 0xffff;
    } else if (strcasecmp("Sel", prio2_str) == 0) {
        prio2 = 0;
    } else {
        prio2 = atoi(prio2_str);
        if (prio2 < 1 || prio2 > NCHAN) {
            fprintf(stderr, "Bad priority channel 2.\n");
            return 0;
        }
    }

    if (strcasecmp("Last", tx_str) == 0) {
        txchan = REVCH_LAST_CALLED;
    } else if (strcasecmp("Sel", tx_str) == 0 || strcasecmp("-", tx_str) == 0) {
        txchan = REVCH_SELECTED;
    } else {
        fprintf(stderr, "Bad transmit channel.\n");
        return 0;
    }

    setup_scanlist(snum-1, name_str, prio1, prio2, txchan);

    if (*chan_str != '-') {
        char *str   = chan_str;
        int   nchan = 0;
        int   range = 0;
        int   last  = 0;

        // Parse channel list.
        for (;;) {
            char *eptr;
            int cnum = strtoul(str, &eptr, 10);

            if (eptr == str) {
                fprintf(stderr, "Scan list %d: wrong channel list '%s'.\n", snum, str);
                return 0;
            }
            if (cnum < 1 || cnum > NCHAN) {
                fprintf(stderr, "Scan list %d: wrong channel number %d.\n", snum, cnum);
                return 0;
            }

            if (range) {
                // Add range.
                int c;
                for (c=last+1; c<=cnum; c++) {
                    if (!scanlist_append(snum-1, c)) {
                        fprintf(stderr, "Scan list %d: too many channels.\n", snum);
                        return 0;
                    }
                    nchan++;
                }
            } else {
                // Add single channel.
                if (!scanlist_append(snum-1, cnum)) {
                    fprintf(stderr, "Scan list %d: too many channels.\n", snum);
                    return 0;
                }
                nchan++;
            }

            if (*eptr == 0)
                break;

            if (*eptr != ',' && *eptr != '-') {
                fprintf(stderr, "Scan list %d: wrong channel list '%s'.\n", snum, eptr);
                return 0;
            }
            range = (*eptr == '-');
            last = cnum;
            str = eptr + 1;
        }
    }
    return 1;
}

//
// Erase all radioids.
//
static void erase_radioids()
{
    memset(&radio_mem[OFFSET_RADIOID], 0xff, NRADIOIDS*32);
    memset(GET_RADIOID_MAP(), 0x00, 32);
}

static void setup_radioid(int index, const char *name, const char *dmrid)
{
    radioid_t *ri = GET_RADIOID(index);
    memset(ri, 0, 32);

    ascii_decode(ri->name, name, 16, 0);
    uint32_t id = strtoul(dmrid, 0, 0);
    ri->id[0] = ((id / 10000000) << 4) | ((id / 1000000) % 10);
    ri->id[1] = ((id / 100000 % 10) << 4) | ((id / 10000) % 10);
    ri->id[2] = ((id / 1000 % 10) << 4) | ((id / 100) % 10);
    ri->id[3] = ((id / 10 % 10) << 4) | (id % 10);

    // Update radioid map.
    uint8_t *rmap = GET_RADIOID_MAP();
    rmap[index / 8] |= (1UL << (index & 7));

}

static int parse_radioid(int first_row, char *line)
{
    char num_str[256], name_str[256], dmrid_str[256];
    int cnum, id;

    if (sscanf(line, "%s %s %s", num_str, name_str, dmrid_str) != 3)
        return 0;

    cnum = atoi(num_str);
    if (cnum < 0 || cnum > NRADIOIDS) {
        fprintf(stderr, "Bad RadioID number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Contacts table.
        erase_radioids();
    }

    id = atoi(dmrid_str);
    if (id < 1 || id > 0xffffffff) {
        fprintf(stderr, "Bad radio ID.\n");
        return 0;
    }

    setup_radioid(cnum, name_str, dmrid_str);
    return 1;
}

//
// Erase all contacts.
//
static void erase_contacts()
{
    memset(&radio_mem[OFFSET_CONTACTS], 0xff, NCONTACTS*100);
    memset(GET_CONTACT_LIST(), 0xff, NCONTACTS*4);
    memset(GET_CONTACT_MAP(), 0xff, (NCONTACTS + 7) / 8);
}

static void setup_contact(int index, const char *name, int type, int id, int rxalert)
{
    // Fill contact record.
    contact_t *ct = GET_CONTACT(index);
    memset(ct, 0, 100);
    ascii_decode(ct->name, name, 16, 0);

    ct->id[0] = ((id / 10000000) << 4) | ((id / 1000000) % 10);
    ct->id[1] = ((id / 100000 % 10) << 4) | ((id / 10000) % 10);
    ct->id[2] = ((id / 1000 % 10) << 4) | ((id / 100) % 10);
    ct->id[3] = ((id / 10 % 10) << 4) | (id % 10);

    ct->type       = type;
    ct->call_alert = rxalert;

    // Update contact map.
    uint8_t *cmap = GET_CONTACT_MAP();
    cmap[index / 8] &= ~(1 << (index & 7));

    // Append to the contact list.
    uint32_t *clist = GET_CONTACT_LIST();
    int i;

    for (i=0; i<NCONTACTS; i++) {
        int item = clist[i];

        if (item == index) {
            // The channel is already in the list.
            break;
        }
        if (item == 0xffffffff) {
            // Append to the end of the list.
            clist[i] = index;
            break;
        }
        if (item > index) {
            // Insert index there and shift the rest.
            clist[i] = index;
            index = item;
        }
    }
}

//
// Parse one line of Contacts table.
// Return 0 on failure.
//
static int parse_contact(int first_row, char *line)
{
    char num_str[256], name_str[256], type_str[256], id_str[256], rxalert_str[256];
    int cnum, type, id, rxalert;

    if (sscanf(line, "%s %s %s %s %s",
        num_str, name_str, type_str, id_str, rxalert_str) != 5)
        return 0;

    cnum = atoi(num_str);
    if (cnum < 1 || cnum > NCONTACTS) {
        fprintf(stderr, "Bad contact number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Contacts table.
        erase_contacts();
    }

    if (strcasecmp("Group", type_str) == 0) {
        type = CALL_GROUP;
    } else if (strcasecmp("Private", type_str) == 0) {
        type = CALL_PRIVATE;
    } else if (strcasecmp("All", type_str) == 0) {
        type = CALL_ALL;
    } else {
        fprintf(stderr, "Bad call type.\n");
        return 0;
    }

    id = atoi(id_str);
    if (id < 1 || id > 0xffffff) {
        fprintf(stderr, "Bad call ID.\n");
        return 0;
    }

    if (*rxalert_str == '-' || strcasecmp("No", rxalert_str) == 0) {
        rxalert = ALERT_NONE;
    } else if (*rxalert_str == '+' || strcasecmp("Yes", rxalert_str) == 0) {
        rxalert = ALERT_RING;
    } else if (strcasecmp("Online", rxalert_str) == 0) {
        rxalert = ALERT_ONLINE;
    } else {
        fprintf(stderr, "Bad receive tone flag.\n");
        return 0;
    }

    setup_contact(cnum-1, name_str, type, id, rxalert);
    return 1;
}

static void setup_grouplist(int index, const char *name)
{
    grouplist_t *gl = GET_GROUPLIST(index);

    ascii_decode(gl->name, name, 35, 0);
    memset(gl->unused, 0, sizeof(gl->unused));
}

//
// Add contact to a grouplist.
// Return 0 on failure.
//
static int grouplist_append(int index, int cnum)
{
    grouplist_t *gl = GET_GROUPLIST(index);
    int i;

    for (i=0; i<64; i++) {
        if (gl->member[i] == cnum-1)
            return 1;
        if (gl->member[i] == 0xffffffff) {
            gl->member[i] = cnum-1;
            return 1;
        }
    }
    return 0;
}

//
// Parse one line of Grouplist table.
// Return 0 on failure.
//
static int parse_grouplist(int first_row, char *line)
{
    char num_str[256], name_str[256], list_str[256];
    int glnum;

    if (sscanf(line, "%s %s %s", num_str, name_str, list_str) != 3)
        return 0;

    glnum = strtoul(num_str, 0, 10);
    if (glnum < 1 || glnum > NGLISTS) {
        fprintf(stderr, "Bad group list number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Grouplists table.
        memset(&radio_mem[OFFSET_GLISTS], 0xff, NGLISTS*320);
    }

    setup_grouplist(glnum-1, name_str);

    if (*list_str != '-') {
        char *str   = list_str;
        int   range = 0;
        int   last  = 0;

        // Parse contact list.
        for (;;) {
            char *eptr;
            int cnum = strtoul(str, &eptr, 10);

            if (eptr == str) {
                fprintf(stderr, "Group list %d: wrong contact list '%s'.\n", glnum, str);
                return 0;
            }
            if (cnum < 1 || cnum > NCONTACTS) {
                fprintf(stderr, "Group list %d: wrong contact number %d.\n", glnum, cnum);
                return 0;
            }

            if (range) {
                // Add range.
                int c;
                for (c=last+1; c<=cnum; c++) {
                    if (!grouplist_append(glnum-1, c)) {
                        fprintf(stderr, "Group list %d: too many contacts.\n", glnum);
                        return 0;
                    }
                }
            } else {
                // Add single contact.
                if (!grouplist_append(glnum-1, cnum)) {
                    fprintf(stderr, "Group list %d: too many contacts.\n", glnum);
                    return 0;
                }
            }

            if (*eptr == 0)
                break;

            if (*eptr != ',' && *eptr != '-') {
                fprintf(stderr, "Group list %d: wrong contact list '%s'.\n", glnum, eptr);
                return 0;
            }
            range = (*eptr == '-');
            last = cnum;
            str = eptr + 1;
        }
    }
    return 1;
}



//
// Set text for a given message.
//
static void setup_message(int index, const char *text)
{
    uint8_t *msg = GET_MESSAGE(index);

    // Skip spaces and tabs.
    while (*text == ' ' || *text == '\t')
        text++;
    ascii_decode(msg, text, 200, 0);
}

//
// Parse one line of Messages table.
// Return 0 on failure.
//
static int parse_messages(int first_row, char *line)
{
    char *text;
    int mnum;

    mnum = strtoul(line, &text, 10);
    if (text == line || mnum < 1 || mnum > NMESSAGES) {
        fprintf(stderr, "Bad message number.\n");
        return 0;
    }

    if (first_row) {
        // On first entry, erase the Messages table.
        memset(&radio_mem[OFFSET_MESSAGES], 0xff, NMESSAGES*256);
    }

    setup_message(mnum-1, text);
    return 1;
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
    if (strncasecmp(line, "RadioID", 7) == 0)
        return 'R';
    return 0;
}

//
// Parse one line of table data.
// Return 0 on failure.
//
static int d868uv_parse_row(radio_device_t *radio, int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'D': return parse_digital_channel(radio, first_row, line);
    case 'A': return parse_analog_channel(radio, first_row, line);
    case 'Z': return parse_zones(first_row, line);
    case 'S': return parse_scanlist(first_row, line);
    case 'C': return parse_contact(first_row, line);
    case 'G': return parse_grouplist(first_row, line);
    case 'M': return parse_messages(first_row, line);
    case 'R': return parse_radioid(first_row, line);
    }
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
    int i, k, nchannels = 0, nzones = 0, nscanlists = 0, ngrouplists = 0;
    int ncontacts = 0, nerrors = 0;

    // Channels: check references to scanlists, contacts and grouplists.
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = get_channel(i);

        if (!ch)
            continue;

        nchannels++;
        int scanlist_index = get_scanlist_index(radio, ch);
        if (scanlist_index != 0xff) {
            scanlist_t *sl = get_scanlist(scanlist_index);

            if (!sl) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': scanlist %d not found.\n", scanlist_index + 1);
                nerrors++;
            }
        }
        if (ch->contact_index != 0xffff) {
            contact_t *ct = get_contact(ch->contact_index);

            if (!ct) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': contact %d not found.\n", ch->contact_index + 1);
                nerrors++;
            }
        }
        if (ch->group_list_index != 0xff) {
            grouplist_t *gl = GET_GROUPLIST(ch->group_list_index);

            if (!VALID_GROUPLIST(gl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': grouplist %d not found.\n", ch->group_list_index + 1);
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
        for (k=0; k<50; k++) {
            int cindex = sl->member[k];

            if (cindex != 0xffff) {
                channel_t *ch = get_channel(cindex);

                if (!ch) {
                    fprintf(stderr, "Scanlist %d '", i+1);
                    print_ascii(stderr, sl->name, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cindex+1);
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

    // Contacts have to be continuous for UV380.
    for (i=0; i<ncontacts; i++) {
        if (!get_contact(i)) {
            fprintf(stderr, "Contact %d is missing.\n", i+1);
            fprintf(stderr, "Contacts must be continuous for %s.\n", radio->name);
            nerrors++;
            break;
        }
    }

    if (nerrors > 0) {
        fprintf(stderr, "Total %d errors.\n", nerrors);
        return 0;
    }
    fprintf(stderr, "Total %d channels, %d zones, %d scanlists, %d contacts, %d grouplists.\n",
        nchannels, nzones, nscanlists, ncontacts, ngrouplists);
    return 1;
}

//
// Read and dump the callsign database.
//
static void dump_csv(radio_device_t *radio)
{
    callsign_sizes_t sz = {0};

    //
    // Dump sizes.
    //
    serial_read_region(ADDR_CALLDB_SIZE, (uint8_t*) &sz, 16);
    printf("Sizes:\n");
    print_hex_addr_data(ADDR_CALLDB_SIZE, (uint8_t*)&sz, sizeof(sz));
    printf("\n");

    //
    // Dump callsign map.
    //
    unsigned addr = ADDR_CALLDB_LIST;
    unsigned index;

    printf("Map:\n");
    for (index = 0; index < sz.count; index += 16000) {
        unsigned n = (sz.count - index) * 8;
        if (n > 128000)
            n = 128000;

        uint8_t map[128000];
        serial_read_region(addr, map, n);
        print_hex_addr_data(addr, map, n);
        addr += 256*1024;
    }
    printf("\n");

    //
    // Dump data.
    //
    printf("Data:\n");
    addr = ADDR_CALLDB_DATA;
    for (index = 0; index < 10000000; index += 100000) {
        int n = sz.last - addr;
        if (n < 0)
            break;
        else if (n > 100000)
            n = 100000;
        else
            n = (n + 15) & ~15; // align

        uint8_t data[100000];
        serial_read_region(addr, data, n);
        print_hex_addr_data(addr, data, n);
        addr += 256*1024;
    }
}

//
// Sorting callback for callsign map.
//
static int compare_callsign_map(const void *ap, const void *bp)
{
    uint32_t a = *(uint32_t*) ap;
    uint32_t b = *(uint32_t*) bp;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

//
// Write CSV file to the callsign database.
//
// The callsign database consists of three parts:
// (1) Map of DMR IDs to data offsets: 8 bytes per record.
//      04000000: 02-60-04-02-00-00-00-00-04-60-04-02-35-00-00-00 .`.......`..5...
//      04000010: 06-60-04-02-70-00-00-00-08-60-04-02-a7-00-00-00 .`..p....`......
//                ^^^^^^^^^^^ ^^^^^^^^^^^
//                radio id<<1 offset
//
//     The map is stored in 128000-byte chunks with 256kbyte step:
//      04000000-0401f3ff, 04040000-0405f3ff, 04080000-0409f3ff, 040c0000-040df3ff,
//      04100000-0411f3ff, 04140000-0415f3ff, 04180000-0419f3ff, 041c0000-...
//
//     Up to 10 chunks in total. Last range is 04240000-0425f3ff.
//     Max 160000 callsigns.
//
// (2) Sizes: count of records and last data address.
//      044c0000: bf-b7-01-00-bd-f6-34-05-00-00-00-00-00-00-00-00 ......4.........
//                ^^^^^^^^^^^ ^^^^^^^^^^^
//                count       last address
//
// (3) Data records: radio id, name, city, callsign, state, country, remarks.
//      04500000: 00-01-02-30-01-00-57-61-79-6e-65-20-45-64-77-61 ...0..Wayne Edwa
//      04500010: 72-64-00-54-6f-72-6f-6e-74-6f-00-56-45-33-54-48 rd.Toronto.VE3TH
//      04500020: 57-00-4f-6e-74-61-72-69-6f-00-43-61-6e-61-64-61 W.Ontario.Canada
//      04500030: 00-44-4d-52-00-                                 .DMR.
//
//     The data are stored in 100000-byte chunks with 256kbyte step:
//      04500000-0451869f, 04540000-0455869f, ... 05340000-0535869f and so on.
//
static void d868uv_write_csv(radio_device_t *radio, FILE *csv)
{
    callsign_map_t map[NCALLSIGNS];
    callsign_sizes_t sz = {0};

    // Allocate data.
    char *data = malloc(CALLSIGN_SIZE);
    if (!data) {
        fprintf(stderr, "Out of memory!\n");
        return;
    }
    memset(data, 0, CALLSIGN_SIZE);
    memset(map, 0xff, sizeof(map));

    //
    // Parse CSV file.
    //
    // The file has the following format:
    // Radio ID,Callsign,Name,City,State,Country,Remarks
    // 3114542,KK6ABQ,Sergey Vakulenko,Santa Clara,California,United States,DMR
    //
    // Need to rearrange the fields like:
    // Radio ID, Name, City, Callsign, State, Country, Remarks
    //
    unsigned nbytes = 0;
    char *radioid, *callsign, *name, *city, *state, *country, *remarks;

    if (csv_init(csv) < 0) {
        free(data);
        return;
    }
    while (csv_read(csv, &radioid, &callsign, &name, &city, &state, &country, &remarks)) {
        radioid  = trim_spaces(radioid,  16);
        callsign = trim_spaces(callsign, 16);
        name     = trim_spaces(name,     16);
        city     = trim_spaces(city,     15);
        state    = trim_spaces(state,    16);
        country  = trim_spaces(country,  16);
        remarks  = trim_spaces(remarks,  16);
        //printf("%s,%s,%s,%s,%s,%s,%s\n", radioid, callsign, name, city, state, country, remarks);

        unsigned id = strtoul(radioid, 0, 10);
        if (id < 1 || id > 0xffffff) {
            fprintf(stderr, "Bad id: %d\n", id);
            fprintf(stderr, "Line: '%s,%s,%s,%s,%s,%s,%s'\n",
                radioid, callsign, name, city, state, country, remarks);
            return;
        }

        // Eastern egg: when file contains id 1 with callsign 'dump',
        // read the callsign database from the radio
        // and save to a file.
        if (id == 1 && strcmp(callsign, "dump") == 0) {
            free(data);
            dump_csv(radio);
            return;
        }

        // Add map record.
        if (sz.count >= NCALLSIGNS) {
            fprintf(stderr, "WARNING: Too many callsigns!\n");
            fprintf(stderr, "Skipping the rest.\n");
            break;
        }
        callsign_map_t *m = &map[sz.count];
        sz.count++;
        m->id = ((id / 10     % 10) << 5)  |  (id            % 10) << 1 |
                ((id / 1000   % 10) << 13) | ((id / 100)     % 10) << 9 |
                ((id / 100000 % 10) << 21) | ((id / 10000)   % 10) << 17 |
                ((id / 10000000)    << 29) | ((id / 1000000) % 10) << 25;
        m->offset = nbytes;

        // Fill data.
        char *p = &data[nbytes];

        // Radio ID.
        *p++ = 0;
        *p++ = ((id / 10000000) << 4) | ((id / 1000000) % 10);
        *p++ = ((id / 100000 % 10) << 4) | ((id / 10000) % 10);
        *p++ = ((id / 1000 % 10) << 4) | ((id / 100) % 10);
        *p++ = ((id / 10 % 10) << 4) | (id % 10);
        *p++ = 0;

        // Name, city, callsign, state, country, remarks.
        strcpy(p, name);     p += strlen(p) + 1;
        strcpy(p, city);     p += strlen(p) + 1;
        strcpy(p, callsign); p += strlen(p) + 1;
        strcpy(p, state);    p += strlen(p) + 1;
        strcpy(p, country);  p += strlen(p) + 1;
        strcpy(p, remarks);  p += strlen(p) + 1;

        nbytes = p - data;
    }
    fprintf(stderr, "Total %d contacts, %d bytes.\n", sz.count, nbytes);

    sz.last = ADDR_CALLDB_DATA + (nbytes / 100000) * 256*1024 + (nbytes % 100000);

    // Append extra zeroes and align.
    nbytes = (nbytes + 63) & ~15;

    // Sort the map by DMR ID.
    qsort(map, sz.count, sizeof(map[0]), compare_callsign_map);

    if (! trace_flag) {
        fprintf(stderr, "Write: ");
        fflush(stderr);
    }

    //
    // Write callsign map.
    //
    unsigned addr = ADDR_CALLDB_LIST;
    unsigned index;

//#define DUMP_NO_WRITE
#ifdef DUMP_NO_WRITE
    printf("Map:\n");
#endif
    for (index = 0; index < sz.count; index += 16000) {
        unsigned n = (sz.count - index) * 8;
        if (n > 128000)
            n = 128000;

#ifdef DUMP_NO_WRITE
        // Dump the data, for debugging purposes.
        print_hex_addr_data(addr, (uint8_t*) &map[index], n);
#else
        serial_write_region(addr, (uint8_t*) &map[index], n);
#endif
        addr += 256*1024;

        fprintf(stderr, "#");
        fflush(stderr);
    }

    //
    // Write sizes.
    //
#ifdef DUMP_NO_WRITE
    printf("\nSizes:\n");
    print_hex_addr_data(ADDR_CALLDB_SIZE, (uint8_t*) &sz, 16);
#else
    serial_write_region(ADDR_CALLDB_SIZE, (uint8_t*) &sz, 16);
#endif

    //
    // Write data.
    //
    addr = ADDR_CALLDB_DATA;
#ifdef DUMP_NO_WRITE
    printf("\nData:\n");
#endif
    for (index = 0; index < nbytes; index += 100000) {
        unsigned n = nbytes - index;
        if (n > 100000)
            n = 100000;

#ifdef DUMP_NO_WRITE
        print_hex_addr_data(addr, (uint8_t*) &data[index], n);
#else
        serial_write_region(addr, (uint8_t*) &data[index], n);
#endif
        addr += 256*1024;

        fprintf(stderr, "#");
        fflush(stderr);
    }

    if (! trace_flag)
        fprintf(stderr, "# done.\n");
    free(data);
}

//
// Anytone AT-D868UV
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
    d868uv_write_csv,
};

//
// Anytone AT-D878UV
//
radio_device_t radio_d878uv = {
    "Anytone AT-D878UV",
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
    d868uv_write_csv,
};

//
// BTECH DMR-6x2
//
radio_device_t radio_dmr6x2 = {
    "BTECH DMR-6x2",
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
    d868uv_write_csv,
};
