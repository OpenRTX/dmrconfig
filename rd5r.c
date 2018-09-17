/*
 * Interface to Baofeng RD-5R.
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

#define NCHAN           1024
#define NCONTACTS       256
#define NZONES          250
#define NGLISTS         128
#define NSCANL          250
#define NMESSAGES       32

#define MEMSZ           0x20000
#define OFFSET_TIMESTMP 0x00088
#define OFFSET_SETTINGS 0x000e0
#define OFFSET_MSG      0x00170
#define OFFSET_CONTACTS 0x01788
#define OFFSET_CHANLO   0x03790 // Channels 1-128
#define OFFSET_INTRO    0x07540
#define OFFSET_ZONES    0x08030
#define OFFSET_CHANHI   0x0b1c0 // Channels 129-1024
#define OFFSET_SCANL    0x17720
#define OFFSET_GLISTS   0x1d6a0

#define GET_TIMESTAMP()     (&radio_mem[OFFSET_TIMESTMP])
#define GET_SETTINGS()      ((general_settings_t*) &radio_mem[OFFSET_SETTINGS])
#define GET_INTRO()         ((intro_text_t*) &radio_mem[OFFSET_INTRO])
#define GET_CHANNEL(i)      ((channel_t*) &radio_mem[(i)*56 + ((i)<128 ? OFFSET_CHANLO : OFFSET_CHANHI)])
#define GET_ZONE(i)         ((zone_t*) &radio_mem[OFFSET_ZONES + (i)*48])
#define GET_ZONEXT(i)       ((zone_ext_t*) &radio_mem[OFFSET_ZONEXT + (i)*224])
#define GET_SCANLIST(i)     ((scanlist_t*) &radio_mem[OFFSET_SCANL + (i)*88])
#define GET_CONTACT(i)      ((contact_t*) &radio_mem[OFFSET_CONTACTS + (i)*24])
#define GET_GROUPLIST(i)    ((grouplist_t*) &radio_mem[OFFSET_GLISTS + (i)*48])
#define GET_MESSAGE(i)      (&radio_mem[OFFSET_MSG + (i)*144])

#define VALID_TEXT(txt)     (*(txt) != 0 && *(txt) != 0xff)
#define VALID_CHANNEL(ch)   VALID_TEXT((ch)->name)
#define VALID_ZONE(z)       VALID_TEXT((z)->name)
#define VALID_SCANLIST(sl)  VALID_TEXT((sl)->name)
#define VALID_GROUPLIST(gl) VALID_TEXT((gl)->name)
#define VALID_CONTACT(ct)   VALID_TEXT((ct)->name)

//
// Channel data.
//
typedef struct {
    // Bytes 0-15
    uint8_t name[16];                   // Channel Name

    // Byte 0
    uint8_t channel_mode        : 2,    // Mode: Analog or Digital
#define MODE_ANALOG     1
#define MODE_DIGITAL    2

            bandwidth           : 2,    // Bandwidth: 12.5 or 20 or 25 kHz
#define BW_12_5_KHZ     0
#define BW_20_KHZ       1
#define BW_25_KHZ       2

            autoscan            : 1,    // Autoscan Enable
            squelch             : 1,    // Squelch
#define SQ_TIGHT        0
#define SQ_NORMAL       1

            _unused1            : 1,    // 1
            lone_worker         : 1;    // Lone Worker

    // Byte 1
    uint8_t talkaround          : 1,    // Allow Talkaround
            rx_only             : 1,    // RX Only Enable
            repeater_slot       : 2,    // Repeater Slot: 1 or 2
            colorcode           : 4;    // Color Code: 0...15

    // Byte 2
    uint8_t privacy_no          : 4,    // Privacy No. (+1): 1...16
            privacy             : 2,    // Privacy: None, Basic or Enhanced
#define PRIV_NONE       0
#define PRIV_BASIC      1
#define PRIV_ENHANCED   2

            private_call_conf   : 1,    // Private Call Confirmed
            data_call_conf      : 1;    // Data Call Confirmed

    // Byte 3
    uint8_t rx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
#define REF_LOW         0
#define REF_MEDIUM      1
#define REF_HIGH        2

            _unused2            : 1,    // 0
            emergency_alarm_ack : 1,    // Emergency Alarm Ack
            _unused3            : 2,    // 0b10
            uncompressed_udp    : 1,    // Compressed UDP Data header (0) Enable, (1) Disable
            display_pttid_dis   : 1;    // Display PTT ID (inverted)

    // Byte 4
    uint8_t tx_ref_frequency    : 2,    // RX Ref Frequency: Low, Medium or High
            _unused4            : 2,    // 0b01
            vox                 : 1,    // VOX Enable
            power               : 1,    // Power: Low, High
#define POWER_HIGH      1
#define POWER_LOW       0

            admit_criteria      : 2;    // Admit Criteria: Always, Channel Free or Correct CTS/DCS
#define ADMIT_ALWAYS    0
#define ADMIT_CH_FREE   1
#define ADMIT_TONE      2
#define ADMIT_COLOR     3

    // Byte 5
    uint8_t _unused5            : 4,    // 0
            in_call_criteria    : 2,    // In Call Criteria: Always, Follow Admit Criteria or TX Interrupt
#define INCALL_ALWAYS   0
#define INCALL_ADMIT    1

            _unused6            : 2;    // 0b11

    // Bytes 6-7
    uint16_t contact_name_index;        // Contact Name: Contact1...

    // Bytes 8-9
    uint8_t tot                 : 6,    // TOT x 15sec: 0-Infinite, 1=15s... 37=555s
            _unused13           : 2;    // 0
    uint8_t tot_rekey_delay;            // TOT Rekey Delay: 0s...255s

    // Bytes 10-11
    uint8_t emergency_system_index;     // Emergency System: None, System1...32
    uint8_t scan_list_index;            // Scan List: None, ScanList1...250

    // Bytes 12-13
    uint8_t group_list_index;           // Group List: None, GroupList1...250
    uint8_t _unused7;                   // 0

    // Bytes 14-15
    uint8_t _unused8;                   // 0
    uint8_t _unused9;                   // 0xff

    // Bytes 16-23
    uint32_t rx_frequency;              // RX Frequency: 8 digits BCD
    uint32_t tx_frequency;              // TX Frequency: 8 digits BCD

    // Bytes 24-27
    uint16_t ctcss_dcs_receive;         // CTCSS/DCS Dec: 4 digits BCD
    uint16_t ctcss_dcs_transmit;        // CTCSS/DCS Enc: 4 digits BCD

    // Bytes 28-29
    uint8_t rx_signaling_syst;          // Rx Signaling System: Off, DTMF-1...4
    uint8_t tx_signaling_syst;          // Tx Signaling System: Off, DTMF-1...4

    // Bytes 30-31
    uint8_t _unused10;                  // 0xff
    uint8_t _unused11;                  // 0xff
} channel_t;

//
// Contact data.
//
typedef struct {
    // Bytes 0-15
    uint8_t name[16];                   // Contact Name, ff terminated

    // Bytes 16-19
    uint8_t id[4];                      // BCD coded 8 digits
#define GET_ID(x) (((x)[0] >> 4) * 10000000 +\
                   ((x)[0] & 15) * 1000000 +\
                   ((x)[1] >> 4) * 100000 +\
                   ((x)[1] & 15) * 10000 +\
                   ((x)[2] >> 4) * 1000 +\
                   ((x)[2] & 15) * 100 +\
                   ((x)[3] >> 4) * 10 +\
                   ((x)[3] & 15))
#define CONTACT_ID(ct) GET_ID((ct)->id)

    // Byte 20
    uint8_t type;                       // Call Type: Group Call, Private Call or All Call
#define CALL_GROUP      0
#define CALL_PRIVATE    1
#define CALL_ALL        2

    // Bytes 21-23
    uint8_t receive_tone;               // Call Receive Tone: 0=Off, 1=On
    uint8_t ring_style;                 // Ring style: 0-10
    uint8_t _unused;                    // 0xff for used contact, 0 for blank entry

} contact_t;

//
// Zone data.
//
typedef struct {
    // Bytes 0-15
    uint8_t name[16];                   // Zone Name

    // Bytes 16-47
    uint16_t member[16];                // Member: channels 1...16
} zone_t;

//
// Group list data.
//
typedef struct {
    // Bytes 0-15
    uint8_t name[16];                   // Group List Name

    // Bytes 16-47
    uint16_t member[16];                // Contacts
} grouplist_t;

//
// Scan list data.
//
typedef struct {
    // Bytes 0-14
    uint8_t name[15];                   // Scan List Name

    // Byte 15
    uint8_t _unused             : 4,    // 0
            channel_mark        : 1,    // Channel Mark, default 1
            pl_type             : 2,    // PL Type, default 3
#define PL_NONPRI       0               // Non-Priority Channel
#define PL_DISABLE      1               // Disable
#define PL_PRI          2               // Priority Channel
#define PL_PRI_NONPRI   3               // Priority and Non-Priority Channels

            talkback            : 1;    // Talkback, default 1

    // Bytes 16-79
    uint16_t member[32];                // Channels (+1)
#define CHAN_SELECTED   1               // Selected

    // Bytes 80-85
    uint16_t priority_ch1;              // Priority Channel 1, chan+1 or 0=None, 1=Selected
    uint16_t priority_ch2;              // Priority Channel 2
    uint16_t tx_designated_ch;          // Tx Designated Channel, chan+1 or 0=Last Active Channel

    // Bytes 86-87
    uint8_t sign_hold_time;             // Signaling Hold Time (x25 = msec) default 40=1000ms
    uint8_t prio_sample_time;           // Priority Sample Time (x250 = msec) default 8=2000ms

} scanlist_t;

//
// General settings.
// TODO: verify the general settings with official CPS
//
typedef struct {
    // Bytes e0-e7
    uint8_t radio_name[8];

    // Bytes e8-eb
    uint8_t  radio_id[4];
} general_settings_t;

//
// General settings.
// TODO: verify the general settings with official CPS
//
typedef struct {
    // Bytes 7540-754f
    uint8_t intro_line1[16];

    // Bytes 7550-755f
    uint8_t intro_line2[16];
} intro_text_t;

static const char *POWER_NAME[] = { "Low", "High" };
static const char *SQUELCH_NAME[] = { "Tight", "Normal" };
static const char *BANDWIDTH[] = { "12.5", "20", "25", "25" };
static const char *CONTACT_TYPE[] = {"Group", "Private", "All", "???" };
static const char *ADMIT_NAME[] = { "-", "Free", "Tone", "Color" };
static const char *INCALL_NAME[] = { "-", "Admit", "-", "Admit" };

#ifdef PRINT_RARE_PARAMS
static const char *REF_FREQUENCY[] = { "Low", "Med", "High" };
static const char *PRIVACY_NAME[] = { "-", "Basic", "Enhanced" };
static const char *SIGNALING_SYSTEM[] = { "-", "DTMF-1", "DTMF-2", "DTMF-3", "DTMF-4" };
#endif

//
// Print a generic information about the device.
//
static void rd5r_print_version(radio_device_t *radio, FILE *out)
{
    unsigned char *timestamp = GET_TIMESTAMP();

    if (*timestamp != 0xff) {
        fprintf(out, "Last Programmed Date: %d%d%d%d-%d%d-%d%d",
            timestamp[0] >> 4, timestamp[0] & 15, timestamp[1] >> 4, timestamp[1] & 15,
            timestamp[2] >> 4, timestamp[2] & 15, timestamp[3] >> 4, timestamp[3] & 15);
        fprintf(out, " %d%d:%d%d\n",
            timestamp[4] >> 4, timestamp[4] & 15, timestamp[5] >> 4, timestamp[5] & 15);
    }
}

//
// Read memory image from the device.
//
static void download(radio_device_t *radio)
{
    int bno;

    // Read range 0x80...0x1e29f.
    for (bno=1; bno<966; bno++) {
        if (bno >= 248 && bno < 256) {
            // Skip range 0x7c00...0x8000.
            continue;
        }
        hid_read_block(bno, &radio_mem[bno*128], 128);

        ++radio_progress;
        if (radio_progress % 32 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
    //hid_read_finish();

    // Clear header and footer.
    memset(&radio_mem[0], 0xff, 128);
    memset(&radio_mem[966*128], 0xff, MEMSZ - 966*128);
    memset(&radio_mem[248*128], 0xff, 8*128);
}

//
// Baofeng RD-5R: read memory image.
//
static void rd5r_download(radio_device_t *radio)
{
    download(radio);

    // Add header.
    memcpy(&radio_mem[0], "BF-5R", 5);
}

//
// Radioddity GD-77: read memory image.
//
static void gd77_download(radio_device_t *radio)
{
    download(radio);

    // Add header.
    memcpy(&radio_mem[0], "MD-760P", 7);
}

//
// Write memory image to the device.
//
static void rd5r_upload(radio_device_t *radio, int cont_flag)
{
    int bno;

    // Write range 0x80...0x1e29f.
    for (bno=1; bno<966; bno++) {
        if (bno >= 248 && bno < 256) {
            // Skip range 0x7c00...0x8000.
            continue;
        }
        hid_write_block(bno, &radio_mem[bno*128], 128);

        ++radio_progress;
        if (radio_progress % 32 == 0) {
            fprintf(stderr, "#");
            fflush(stderr);
        }
    }
    hid_write_finish();
}

//
// Check whether the memory image is compatible with this device.
//
static int rd5r_is_compatible(radio_device_t *radio)
{
    return strncmp("BF-5R", (char*)&radio_mem[0], 5) == 0;
}

//
// Check whether the memory image is compatible with this device.
//
static int gd77_is_compatible(radio_device_t *radio)
{
    return strncmp("MD-760P", (char*)&radio_mem[0], 7) == 0;
}

//
// Set name for a given zone.
//
static void setup_zone(int index, const char *name)
{
    zone_t *z = GET_ZONE(index);
    int len = strlen(name);

    memset(z->name, 0xff, 16);
    memcpy(z->name, name, len<16 ? len : 16);
}

//
// Add channel to a zone.
// Return 0 on failure.
//
static int zone_append(int index, int cnum)
{
    zone_t *z = GET_ZONE(index);
    int i;

    for (i=0; i<16; i++) {
        if (z->member[i] == cnum)
            return 1;
        if (z->member[i] == 0) {
            z->member[i] = cnum;
            return 1;
        }
    }
    return 0;
}

static void erase_zone(int index)
{
    zone_t *z = GET_ZONE(index);

    memset(z->name, 0xff, 16);
    memset(z->member, 0, 32);
}

//
// Set parameters for a given scan list.
//
static void setup_scanlist(int index, const char *name,
    int prio1, int prio2, int txchan)
{
    scanlist_t *sl = GET_SCANLIST(index);
    int len = strlen(name);

    // Bytes 0-31
    memset(sl->name, 0xff, sizeof(sl->name));
    memcpy(sl->name, name, len < sizeof(sl->name) ? len : sizeof(sl->name));

    // Bytes 32-37
    sl->priority_ch1     = prio1;
    sl->priority_ch2     = prio2;
    sl->tx_designated_ch = txchan;
}

static void erase_scanlist(int index)
{
    scanlist_t *sl = GET_SCANLIST(index);

    memset(sl, 0, 88);
    memset(sl->name, 0xff, sizeof(sl->name));

    sl->talkback         = 1;
    sl->channel_mark     = 1;
    sl->pl_type          = PL_PRI_NONPRI;
    sl->sign_hold_time   = 0xff;
    sl->prio_sample_time = 2000 / 250;  // 2 sec
}

//
// Add channel to a zone.
// Return 0 on failure.
//
static int scanlist_append(int index, int cnum)
{
    scanlist_t *sl = GET_SCANLIST(index);
    int i;

    // First element is always Selected.
    if (sl->member[0] == 0)
        sl->member[0] = CHAN_SELECTED;

    for (i=0; i<32; i++) {
        if (sl->member[i] == cnum)
            return 1;
        if (sl->member[i] == 0) {
            sl->member[i] = cnum + 1;
            return 1;
        }
    }
    return 0;
}

static void erase_contact(int index)
{
    contact_t *ct = GET_CONTACT(index);

    memset(ct->name, 0xff, 16);
    memset(ct->id, 0, 8);
}

static void setup_contact(int index, const char *name, int type, int id, int rxtone)
{
    contact_t *ct = GET_CONTACT(index);
    int len = strlen(name);

    ct->id[0]        = id;
    ct->id[1]        = id >> 8;
    ct->id[2]        = id >> 16;
    ct->type         = type;
    ct->receive_tone = rxtone;
    ct->ring_style   = 0; // TODO
    ct->_unused      = 0xff;

    memcpy(ct->name, name, len < 16 ? len : 16);
}

static void setup_grouplist(int index, const char *name)
{
    grouplist_t *gl = GET_GROUPLIST(index);
    int len = strlen(name);

    memset(gl->name, 0xff, 16);
    memcpy(gl->name, name, len < 16 ? len : 16);
}

//
// Add contact to a grouplist.
// Return 0 on failure.
//
static int grouplist_append(int index, int cnum)
{
    grouplist_t *gl = GET_GROUPLIST(index);
    int i;

    for (i=0; i<32; i++) {
        if (gl->member[i] == cnum)
            return 1;
        if (gl->member[i] == 0) {
            gl->member[i] = cnum;
            return 1;
        }
    }
    return 0;
}

//
// Set text for a given message.
//
static void setup_message(int index, const char *text)
{
    uint8_t *msg = GET_MESSAGE(index);
    int len;

    // Skip spaces and tabs.
    while (*text == ' ' || *text == '\t')
        text++;
    len = strlen(text);
    memcpy(msg, text, len < 144 ? len : 144);
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
// Set the parameters for a given memory channel.
//
static void setup_channel(int i, int mode, char *name, double rx_mhz, double tx_mhz,
    int power, int scanlist, int autoscan, int squelch, int tot, int rxonly,
    int admit, int colorcode, int timeslot, int incall, int grouplist, int contact,
    int rxtone, int txtone, int width)
{
    channel_t *ch = GET_CHANNEL(i);

    ch->channel_mode        = mode;
    ch->bandwidth           = width;
    ch->autoscan            = autoscan;
    ch->squelch             = squelch;
    ch->rx_only             = rxonly;
    ch->repeater_slot       = timeslot;
    ch->colorcode           = colorcode;
    ch->data_call_conf      = 1;        // Always ask for SMS acknowledge
    ch->power               = power;
    ch->admit_criteria      = admit;
    ch->in_call_criteria    = incall;
    ch->contact_name_index  = contact;
    ch->tot                 = tot;
    ch->scan_list_index     = scanlist;
    ch->group_list_index    = grouplist;
    ch->rx_frequency        = mhz_to_bcd(rx_mhz);
    ch->tx_frequency        = mhz_to_bcd(tx_mhz);
    ch->ctcss_dcs_receive   = rxtone;
    ch->ctcss_dcs_transmit  = txtone;

    int len = strlen(name);
    memset(ch->name, 0xff, 16);
    memcpy(ch->name, name, len<16 ? len : 16);
}

//
// Erase the channel record.
//
static void erase_channel(int i)
{
    channel_t *ch = GET_CHANNEL(i);

    // Byte 0
    ch->channel_mode = MODE_ANALOG;
    ch->bandwidth    = BW_12_5_KHZ;
    ch->autoscan     = 0;
    ch->squelch      = SQ_NORMAL;
    ch->_unused1     = 1;
    ch->lone_worker  = 0;

    // Byte 1
    ch->talkaround    = 0;
    ch->rx_only       = 0;
    ch->repeater_slot = 1;
    ch->colorcode     = 1;

    // Byte 2
    ch->privacy_no        = 0;
    ch->privacy           = PRIV_NONE;
    ch->private_call_conf = 0;
    ch->data_call_conf    = 0;

    // Byte 3
    ch->rx_ref_frequency    = REF_LOW;
    ch->_unused2            = 0;
    ch->emergency_alarm_ack = 0;
    ch->_unused3            = 2;
    ch->uncompressed_udp    = 1;
    ch->display_pttid_dis   = 1;

    // Byte 4
    ch->tx_ref_frequency = REF_LOW;
    ch->_unused4         = 1;
    ch->vox              = 0;
    ch->power            = POWER_HIGH;
    ch->admit_criteria   = ADMIT_ALWAYS;

    // Byte 5
    ch->_unused5         = 0;
    ch->in_call_criteria = INCALL_ALWAYS;
    ch->_unused6         = 3;

    // Bytes 6-7
    ch->contact_name_index = 0;

    // Bytes 8-9
    ch->tot             = 60/15;
    ch->_unused13       = 0;
    ch->tot_rekey_delay = 0;

    // Bytes 10-11
    ch->emergency_system_index = 0;
    ch->scan_list_index        = 0;

    // Bytes 12-13
    ch->group_list_index = 0;
    ch->_unused7         = 0;

    // Bytes 14-15
    ch->_unused8 = 0;
    ch->_unused9 = 0xff;

    // Bytes 16-23
    ch->rx_frequency = 0x40000000;
    ch->tx_frequency = 0x40000000;

    // Bytes 24-27
    ch->ctcss_dcs_receive = 0xffff;
    ch->ctcss_dcs_transmit = 0xffff;

    // Bytes 28-29
    ch->rx_signaling_syst = 0;
    ch->tx_signaling_syst = 0;

    // Bytes 30-31
    ch->_unused10 = 0xff;
    ch->_unused11 = 0xff;

    // Bytes 32-63
    memset(ch->name, 0xff, 16);
}

static void print_chanlist(FILE *out, uint16_t *unsorted, int nchan, int scanlist_flag)
{
    int last  = -1;
    int range = 0;
    int n;
    uint16_t data[nchan];
#define CNUM(n) (scanlist_flag ? n-1 : n)

    // Sort the list before printing.
    memcpy(data, unsorted, nchan * sizeof(uint16_t));
    qsort(data, nchan, sizeof(uint16_t), compare_index);
    for (n=0; n<nchan; n++) {
        int item = data[n];

        if (item == 0)
            break;

        if (item == last+1) {
            range = 1;
        } else {
            if (range) {
                fprintf(out, "-%d", CNUM(last));
                range = 0;
            }
            if (n > 0)
                fprintf(out, ",");
            fprintf(out, "%d", CNUM(item));
        }
        last = item;
    }
    if (range)
        fprintf(out, "-%d", CNUM(last));
}

static void print_id(FILE *out, int verbose)
{
    general_settings_t *gs = GET_SETTINGS();
    unsigned id = GET_ID(gs->radio_id);

    if (verbose)
        fprintf(out, "\n# Unique DMR ID and name of this radio.");
    fprintf(out, "\nID: %u\nName: ", id);
    if (VALID_TEXT(gs->radio_name)) {
        print_ascii(out, gs->radio_name, 8, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\n");
}

static void print_intro(FILE *out, int verbose)
{
    intro_text_t *it = GET_INTRO();

    if (verbose)
        fprintf(out, "\n# Text displayed when the radio powers up.\n");
    fprintf(out, "Intro Line 1: ");
    if (VALID_TEXT(it->intro_line1)) {
        print_ascii(out, it->intro_line1, 16, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\nIntro Line 2: ");
    if (VALID_TEXT(it->intro_line2)) {
        print_ascii(out, it->intro_line2, 16, 0);
    } else {
        fprintf(out, "-");
    }
    fprintf(out, "\n");
}

//
// Do we have any channels of given mode?
//
static int have_channels(int mode)
{
    int i;

    for (i=0; i<NCHAN; i++) {
        channel_t *ch = GET_CHANNEL(i);

        if (!VALID_CHANNEL(ch))
            return 0;
        if (ch->channel_mode == mode)
            return 1;
    }
    return 0;
}

//
// Print base parameters of the channel:
//      Name
//      RX Frequency
//      TX Frequency
//      Power
//      Scan List
//      Autoscan
//      TOT
//      RX Only
//      Admit Criteria
//
static void print_chan_base(FILE *out, channel_t *ch, int cnum)
{
    fprintf(out, "%5d   ", cnum);
    print_ascii(out, ch->name, 16, 1);
    fprintf(out, " ");
    print_freq(out, ch->rx_frequency);
    fprintf(out, " ");
    print_offset(out, ch->rx_frequency, ch->tx_frequency);

    fprintf(out, "%-4s  ", POWER_NAME[ch->power]);

    if (ch->scan_list_index == 0)
        fprintf(out, "-    ");
    else
        fprintf(out, "%-4d ", ch->scan_list_index);

    fprintf(out, "%c  ", "-+"[ch->autoscan]);

    if (ch->tot == 0)
        fprintf(out, "-   ");
    else
        fprintf(out, "%-3d ", ch->tot * 15);

    fprintf(out, "%c  ", "-+"[ch->rx_only]);

    fprintf(out, "%-6s ", ADMIT_NAME[ch->admit_criteria]);
}

#ifdef PRINT_RARE_PARAMS
//
// Print extended parameters of the channel:
//      TOT Rekey Delay
//      RX Ref Frequency
//      RX Ref Frequency
//      Lone Worker
//      VOX
//
static void print_chan_ext(FILE *out, channel_t *ch)
{
    fprintf(out, "%-3d ", ch->tot_rekey_delay);
    fprintf(out, "%-5s ", REF_FREQUENCY[ch->rx_ref_frequency]);
    fprintf(out, "%-5s ", REF_FREQUENCY[ch->tx_ref_frequency]);
    fprintf(out, "%c  ", "-+"[ch->lone_worker]);
    fprintf(out, "%c   ", "-+"[ch->vox]);
    fprintf(out, "%c  ", "-+"[ch->talkaround]);
}
#endif

static void print_digital_channels(FILE *out, int verbose)
{
    int i;

    if (verbose) {
        fprintf(out, "# Table of digital channels.\n");
        fprintf(out, "# 1) Channel number: 1-%d\n", NCHAN);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Low\n");
        fprintf(out, "# 6) Scan list: - or index in Scanlist table\n");
        fprintf(out, "# 7) Autoscan flag: -, +\n");
        fprintf(out, "# 8) Transmit timeout timer in seconds: 0, 15, 30, 45... 555\n");
        fprintf(out, "# 9) Receive only: -, +\n");
        fprintf(out, "# 10) Admit criteria: -, Free, Color\n");
        fprintf(out, "# 11) Color code: 0, 1, 2, 3... 15\n");
        fprintf(out, "# 12) Time slot: 1 or 2\n");
        fprintf(out, "# 13) In call criteria: -, Admit, TXInt\n");
        fprintf(out, "# 14) Receive group list: - or index in Grouplist table\n");
        fprintf(out, "# 15) Contact for transmit: - or index in Contacts table\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Digital Name             Receive   Transmit Power Scan AS TOT RO Admit  Color Slot InCall RxGL TxContact");
#ifdef PRINT_RARE_PARAMS
    fprintf(out, " Dly RxRef TxRef LW VOX TA EmSys Privacy  PN PCC EAA DCC CU");
#endif
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = GET_CHANNEL(i);

        if (!VALID_CHANNEL(ch) || ch->channel_mode != MODE_DIGITAL) {
            // Select digital channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print digital parameters of the channel:
        //      Color Code
        //      Repeater Slot
        //      In Call Criteria
        //      Group List
        //      Contact Name
        fprintf(out, "%-5d %-3d  ", ch->colorcode, ch->repeater_slot);
        fprintf(out, "%-6s ", INCALL_NAME[ch->in_call_criteria]);

        if (ch->group_list_index == 0)
            fprintf(out, "-    ");
        else
            fprintf(out, "%-4d ", ch->group_list_index);

        if (ch->contact_name_index == 0)
            fprintf(out, "-");
        else
            fprintf(out, "%-4d", ch->contact_name_index);

#ifdef PRINT_RARE_PARAMS
        print_chan_ext(out, ch);

        // Extended digital parameters of the channel:
        //      Emergency System
        //      Privacy
        //      Privacy No. (+1)
        //      Private Call Confirmed
        //      Emergency Alarm Ack
        //      Data Call Confirmed
        //      DCDM switch (inverted)
        //      Leader/MS
        if (ch->emergency_system_index == 0)
            fprintf(out, "-     ");
        else
            fprintf(out, "%-5d ", ch->emergency_system_index);

        fprintf(out, "%-8s ", PRIVACY_NAME[ch->privacy]);

        if (ch->privacy == PRIV_NONE)
            fprintf(out, "-  ");
        else
            fprintf(out, "%-2d ", ch->privacy_no + 1);

        fprintf(out, "%c   ", "-+"[ch->private_call_conf]);
        fprintf(out, "%c   ", "-+"[ch->emergency_alarm_ack]);
        fprintf(out, "%c   ", "-+"[ch->data_call_conf]);
        fprintf(out, "%c   ", "+-"[ch->uncompressed_udp]);
#endif
        // Print contact name as a comment.
        if (ch->contact_name_index > 0) {
            contact_t *ct = GET_CONTACT(ch->contact_name_index - 1);
            if (VALID_CONTACT(ct)) {
                fprintf(out, " # ");
                print_ascii(out, ct->name, 16, 0);
            }
        }
        fprintf(out, "\n");
    }
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
        fprintf(out, "# 5) Transmit power: High, Low\n");
        fprintf(out, "# 6) Scan list: - or index\n");
        fprintf(out, "# 7) Autoscan flag: -, +\n");
        fprintf(out, "# 8) Transmit timeout timer in seconds: 0, 15, 30, 45... 555\n");
        fprintf(out, "# 9) Receive only: -, +\n");
        fprintf(out, "# 10) Admit criteria: -, Free, Tone\n");
        fprintf(out, "# 11) Squelch level: Normal, Tight\n");
        fprintf(out, "# 12) Guard tone for receive, or '-' to disable\n");
        fprintf(out, "# 13) Guard tone for transmit, or '-' to disable\n");
        fprintf(out, "# 14) Bandwidth in kHz: 12.5, 20, 25\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Analog  Name             Receive   Transmit Power Scan AS TOT RO Admit  Squelch RxTone TxTone Width");
#ifdef PRINT_RARE_PARAMS
    fprintf(out, " Dly RxRef TxRef LW VOX TA RxSign TxSign ID");
#endif
    fprintf(out, "\n");
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = GET_CHANNEL(i);

        if (!VALID_CHANNEL(ch) || ch->channel_mode != MODE_ANALOG) {
            // Select analog channels
            continue;
        }
        print_chan_base(out, ch, i+1);

        // Print analog parameters of the channel:
        //      Squelch
        //      CTCSS/DCS Dec
        //      CTCSS/DCS Enc
        //      Bandwidth
        fprintf(out, "%-7s ", SQUELCH_NAME[ch->squelch]);
        print_tone(out, ch->ctcss_dcs_receive);
        fprintf(out, "  ");
        print_tone(out, ch->ctcss_dcs_transmit);
        fprintf(out, "  %s", BANDWIDTH[ch->bandwidth]);

#ifdef PRINT_RARE_PARAMS
        print_chan_ext(out, ch);

        // Extended analog parameters of the channel:
        //      Rx Signaling System
        //      Tx Signaling System
        //      Display PTT ID (inverted)
        //      Non-QT/DQT Turn-off Freq.
        fprintf(out, "%-6s ", SIGNALING_SYSTEM[ch->rx_signaling_syst]);
        fprintf(out, "%-6s ", SIGNALING_SYSTEM[ch->tx_signaling_syst]);
        fprintf(out, "%c  ", "+-"[ch->display_pttid_dis]);
#endif
        fprintf(out, "\n");
    }
}

static int have_zones()
{
    zone_t *z = GET_ZONE(0);

    return VALID_ZONE(z);
}

static int have_scanlists()
{
    scanlist_t *sl = GET_SCANLIST(0);

    return VALID_SCANLIST(sl);
}

static int have_contacts()
{
    contact_t *ct = GET_CONTACT(0);

    return VALID_CONTACT(ct);
}

static int have_grouplists()
{
    grouplist_t *gl = GET_GROUPLIST(0);

    return VALID_GROUPLIST(gl);
}

static int have_messages()
{
    uint8_t *msg = GET_MESSAGE(0);

    return VALID_TEXT(msg);
}

//
// Print full information about the device configuration.
//
static void rd5r_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    int i;

    fprintf(out, "Radio: %s\n", radio->name);
    if (verbose)
        rd5r_print_version(radio, out);

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
            zone_t *z = GET_ZONE(i);

            if (!VALID_ZONE(z)) {
                // Zone is disabled.
                break;
            }

            fprintf(out, "%4d    ", i + 1);
            print_ascii(out, z->name, 16, 1);
            fprintf(out, " ");
            if (z->member[0]) {
                print_chanlist(out, z->member, 16, 0);
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
            fprintf(out, "# 3) Priority channel 1 (50%% of scans): -, Sel or index\n");
            fprintf(out, "# 4) Priority channel 2 (25%% of scans): -, Sel or index\n");
            fprintf(out, "# 5) Designated transmit channel: Last, Sel or index\n");
            fprintf(out, "# 6) List of channels: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Scanlist Name            PCh1 PCh2 TxCh ");
#ifdef PRINT_RARE_PARAMS
        fprintf(out, "Hold Smpl ");
#endif
        fprintf(out, "Channels\n");
        for (i=0; i<NSCANL; i++) {
            scanlist_t *sl = GET_SCANLIST(i);

            if (!VALID_SCANLIST(sl)) {
                // Scan list is disabled.
                break;
            }

            fprintf(out, "%5d    ", i + 1);
            print_ascii(out, sl->name, 15, 1);
            if (sl->priority_ch1 == 0) {
                fprintf(out, " -    ");
            } else if (sl->priority_ch1 == 1) {
                fprintf(out, " Sel  ");
            } else {
                fprintf(out, " %-4d ", sl->priority_ch1 - 1);
            }
            if (sl->priority_ch2 == 0) {
                fprintf(out, "-    ");
            } else if (sl->priority_ch2 == 1) {
                fprintf(out, "Sel  ");
            } else {
                fprintf(out, "%-4d ", sl->priority_ch2 - 1);
            }
            if (sl->tx_designated_ch == 0) {
                fprintf(out, "Last ");
            } else if (sl->tx_designated_ch == 1) {
                fprintf(out, "Sel  ");
            } else {
                fprintf(out, "%-4d ", sl->tx_designated_ch - 1);
            }
#ifdef PRINT_RARE_PARAMS
            fprintf(out, "%-4d %-4d ",
                sl->sign_hold_time * 25, sl->prio_sample_time * 250);
#endif
            if (sl->member[1]) {
                print_chanlist(out, sl->member + 1, 31, 1);
            } else {
                fprintf(out, "Sel");
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
            fprintf(out, "# 5) Call receive tone: -, +\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Contact Name             Type    ID       RxTone\n");
        for (i=0; i<NCONTACTS; i++) {
            contact_t *ct = GET_CONTACT(i);

            if (!VALID_CONTACT(ct)) {
                // Contact is disabled
                break;
            }

            fprintf(out, "%5d   ", i+1);
            print_ascii(out, ct->name, 16, 1);
            fprintf(out, " %-7s %-8d %s\n",
                CONTACT_TYPE[ct->type & 3], CONTACT_ID(ct), ct->receive_tone ? "+" : "-");
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
            fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
            fprintf(out, "# 3) List of contacts: numbers and ranges (N-M) separated by comma\n");
            fprintf(out, "#\n");
        }
        fprintf(out, "Grouplist Name             Contacts\n");
        for (i=0; i<NGLISTS; i++) {
            grouplist_t *gl = GET_GROUPLIST(i);

            if (!VALID_GROUPLIST(gl)) {
                // Group list is disabled.
                break;
            }

            fprintf(out, "%5d     ", i + 1);
            print_ascii(out, gl->name, 16, 1);
            fprintf(out, " ");
            if (gl->member[0]) {
                print_chanlist(out, gl->member, 16, 0);
            } else {
                fprintf(out, "-");
            }
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
            fprintf(out, "# 2) Text: up to 144 characters\n");
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
            print_ascii(out, msg, 144, 0);
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
static void rd5r_read_image(radio_device_t *radio, FILE *img)
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
static void rd5r_save_image(radio_device_t *radio, FILE *img)
{
    fwrite(&radio_mem[0], 1, MEMSZ, img);
}

//
// Erase all channels.
//
static void erase_channels()
{
    int i;

    for (i=0; i<NCHAN; i++) {
        erase_channel(i);
    }
}

//
// Erase all zones.
//
static void erase_zones()
{
    int i;

    for (i=0; i<NZONES; i++) {
        erase_zone(i);
    }
}

//
// Erase all scanlists.
//
static void erase_scanlists()
{
    int i;

    for (i=0; i<NSCANL; i++) {
        erase_scanlist(i);
    }
}

//
// Erase all contacts.
//
static void erase_contacts()
{
    int i;

    for (i=0; i<NCONTACTS; i++) {
        erase_contact(i);
    }
}

//
// Parse the scalar parameter.
//
static void rd5r_parse_parameter(radio_device_t *radio, char *param, char *value)
{
    if (strcasecmp("Radio", param) == 0) {
        if (!radio_is_compatible(value)) {
            fprintf(stderr, "Incompatible model: %s\n", value);
            exit(-1);
        }
        return;
    }
#if 0
    general_settings_t *gs = GET_SETTINGS();
    if (strcasecmp ("Name", param) == 0) {
        utf8_decode(gs->radio_name, value, 16);
        return;
    }
    if (strcasecmp ("ID", param) == 0) {
        uint32_t id = strtoul(value, 0, 0);
        gs->radio_id[0] = id;
        gs->radio_id[1] = id >> 8;
        gs->radio_id[2] = id >> 16;
        return;
    }
    if (strcasecmp ("Last Programmed Date", param) == 0) {
        // Ignore.
        return;
    }
    if (strcasecmp ("CPS Software Version", param) == 0) {
        // Ignore.
        return;
    }
    if (strcasecmp ("Intro Line 1", param) == 0) {
        utf8_decode(gs->intro_line1, value, 10);
        return;
    }
    if (strcasecmp ("Intro Line 2", param) == 0) {
        utf8_decode(gs->intro_line2, value, 10);
        return;
    }
#endif
    fprintf(stderr, "Unknown parameter: %s = %s\n", param, value);
    exit(-1);
}

//
// Parse one line of Digital channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_digital_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256], autoscan_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256], colorcode_str[256];
    char slot_str[256], incall_str[256], grouplist_str[256], contact_str[256];
    int num, power, scanlist, autoscan, tot, rxonly, admit;
    int colorcode, timeslot, incall, grouplist, contact;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str, autoscan_str,
        tot_str, rxonly_str, admit_str, colorcode_str,
        slot_str, incall_str, grouplist_str, contact_str) != 15)
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

    if (*autoscan_str == '-') {
        autoscan = 0;
    } else if (*autoscan_str == '+') {
        autoscan = 1;
    } else {
        fprintf(stderr, "Bad autoscan flag.\n");
        return 0;
    }

    tot = atoi(tot_str);
    if (tot > 555 || tot % 15 != 0) {
        fprintf(stderr, "Bad timeout timer.\n");
        return 0;
    }
    tot /= 15;

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = ADMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {
        admit = ADMIT_CH_FREE;
    } else if (strcasecmp("Color", admit_str) == 0) {
        admit = ADMIT_COLOR;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
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

    if (*incall_str == '-' || strcasecmp("Always", incall_str) == 0) {
        incall = INCALL_ALWAYS;
    } else if (strcasecmp("Admit", incall_str) == 0) {
        incall = INCALL_ADMIT;
    } else {
        fprintf(stderr, "Bad incall criteria.\n");
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

    setup_channel(num-1, MODE_DIGITAL, name_str, rx_mhz, tx_mhz,
        power, scanlist, autoscan, SQ_NORMAL, tot, rxonly, admit,
        colorcode, timeslot, incall, grouplist, contact, 0xffff, 0xffff, BW_12_5_KHZ);

    radio->channel_count++;
    return 1;
}

//
// Parse one line of Analog channel table.
// Start_flag is 1 for the first table row.
// Return 0 on failure.
//
static int parse_analog_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_str[256], name_str[256], rxfreq_str[256], offset_str[256];
    char power_str[256], scanlist_str[256], autoscan_str[256], squelch_str[256];
    char tot_str[256], rxonly_str[256], admit_str[256];
    char rxtone_str[256], txtone_str[256], width_str[256];
    int num, power, scanlist, autoscan, squelch, tot, rxonly, admit;
    int rxtone, txtone, width;
    double rx_mhz, tx_mhz;

    if (sscanf(line, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s",
        num_str, name_str, rxfreq_str, offset_str,
        power_str, scanlist_str, autoscan_str,
        tot_str, rxonly_str, admit_str, squelch_str,
        rxtone_str, txtone_str, width_str) != 14)
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

    if (*autoscan_str == '-') {
        autoscan = 0;
    } else if (*autoscan_str == '+') {
        autoscan = 1;
    } else {
        fprintf(stderr, "Bad autoscan flag.\n");
        return 0;
    }

    if (strcasecmp ("Normal", squelch_str) == 0) {
        squelch = SQ_NORMAL;
    } else if (strcasecmp ("Tight", squelch_str) == 0) {
        squelch = SQ_TIGHT;
    } else {
        fprintf (stderr, "Bad squelch level.\n");
        return 0;
    }

    tot = atoi(tot_str);
    if (tot > 555 || tot % 15 != 0) {
        fprintf(stderr, "Bad timeout timer.\n");
        return 0;
    }
    tot /= 15;

    if (*rxonly_str == '-') {
        rxonly = 0;
    } else if (*rxonly_str == '+') {
        rxonly = 1;
    } else {
        fprintf(stderr, "Bad receive only flag.\n");
        return 0;
    }

    if (*admit_str == '-' || strcasecmp("Always", admit_str) == 0) {
        admit = ADMIT_ALWAYS;
    } else if (strcasecmp("Free", admit_str) == 0) {
        admit = ADMIT_CH_FREE;
    } else if (strcasecmp("Tone", admit_str) == 0) {
        admit = ADMIT_TONE;
    } else {
        fprintf(stderr, "Bad admit criteria.\n");
        return 0;
    }

    rxtone = encode_tone(rxtone_str);
    if (rxtone < 0) {
        fprintf(stderr, "Bad receive tone.\n");
        return 0;
    }
    txtone = encode_tone(txtone_str);
    if (txtone < 0) {
        fprintf(stderr, "Bad transmit tone.\n");
        return 0;
    }

    if (strcasecmp ("12.5", width_str) == 0) {
        width = BW_12_5_KHZ;
    } else if (strcasecmp ("20", width_str) == 0) {
        width = BW_20_KHZ;
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

    setup_channel(num-1, MODE_ANALOG, name_str, rx_mhz, tx_mhz,
        power, scanlist, autoscan, squelch, tot, rxonly, admit,
        1, 1, 0, 0, 0, rxtone, txtone, width);

    radio->channel_count++;
    return 1;
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
                    if (!zone_append(znum-1, c)) {
                        fprintf(stderr, "Zone %d: too many channels.\n", znum);
                        return 0;
                    }
                    nchan++;
                }
            } else {
                // Add single channel.
                if (!zone_append(znum-1, cnum)) {
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
        txchan = 0xffff;
    } else if (strcasecmp("Sel", tx_str) == 0) {
        txchan = 0;
    } else {
        txchan = atoi(tx_str);
        if (txchan < 1 || txchan > NCHAN) {
            fprintf(stderr, "Bad transmit channel.\n");
            return 0;
        }
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
// Parse one line of Contacts table.
// Return 0 on failure.
//
static int parse_contact(int first_row, char *line)
{
    char num_str[256], name_str[256], type_str[256], id_str[256], rxtone_str[256];
    int cnum, type, id, rxtone;

    if (sscanf(line, "%s %s %s %s %s",
        num_str, name_str, type_str, id_str, rxtone_str) != 5)
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

    if (*rxtone_str == '-' || strcasecmp("No", rxtone_str) == 0) {
        rxtone = 0;
    } else if (*rxtone_str == '+' || strcasecmp("Yes", rxtone_str) == 0) {
        rxtone = 1;
    } else {
        fprintf(stderr, "Bad receive tone flag.\n");
        return 0;
    }

    setup_contact(cnum-1, name_str, type, id, rxtone);
    return 1;
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
        memset(&radio_mem[OFFSET_GLISTS], 0, NGLISTS*96);
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
        memset(&radio_mem[OFFSET_MSG], 0, NMESSAGES*288);
    }

    setup_message(mnum-1, text);
    return 1;
}

//
// Parse table header.
// Return table id, or 0 in case of error.
//
static int rd5r_parse_header(radio_device_t *radio, char *line)
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
static int rd5r_parse_row(radio_device_t *radio, int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'D': return parse_digital_channel(radio, first_row, line);
    case 'A': return parse_analog_channel(radio, first_row, line);
    case 'Z': return parse_zones(first_row, line);
    case 'S': return parse_scanlist(first_row, line);
    case 'C': return parse_contact(first_row, line);
    case 'G': return parse_grouplist(first_row, line);
    case 'M': return parse_messages(first_row, line);
    }
    return 0;
}

//
// Update timestamp.
//
static void rd5r_update_timestamp(radio_device_t *radio)
{
    unsigned char *timestamp = GET_TIMESTAMP();
    char p[16];

    // Last Programmed Date
    get_timestamp(p);
    timestamp[0] = ((p[0]  & 0xf) << 4) | (p[1]  & 0xf); // year upper
    timestamp[1] = ((p[2]  & 0xf) << 4) | (p[3]  & 0xf); // year lower
    timestamp[2] = ((p[4]  & 0xf) << 4) | (p[5]  & 0xf); // month
    timestamp[3] = ((p[6]  & 0xf) << 4) | (p[7]  & 0xf); // day
    timestamp[4] = ((p[8]  & 0xf) << 4) | (p[9]  & 0xf); // hour
    timestamp[5] = ((p[10] & 0xf) << 4) | (p[11] & 0xf); // minute
}

//
// Check that configuration is correct.
// Return 0 on error.
//
static int rd5r_verify_config(radio_device_t *radio)
{
    int i, k, nchannels = 0, nzones = 0, nscanlists = 0, ngrouplists = 0;
    int ncontacts = 0, nerrors = 0;

    // Channels: check references to scanlists, contacts and grouplists.
    for (i=0; i<NCHAN; i++) {
        channel_t *ch = GET_CHANNEL(i);

        if (!VALID_CHANNEL(ch))
            continue;

        nchannels++;
        if (ch->scan_list_index != 0) {
            scanlist_t *sl = GET_SCANLIST(ch->scan_list_index - 1);

            if (!VALID_SCANLIST(sl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': scanlist %d not found.\n", ch->scan_list_index);
                nerrors++;
            }
        }
        if (ch->contact_name_index != 0) {
            contact_t *ct = GET_CONTACT(ch->contact_name_index - 1);

            if (!VALID_CONTACT(ct)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_ascii(stderr, ch->name, 16, 0);
                fprintf(stderr, "': contact %d not found.\n", ch->contact_name_index);
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
        zone_t *z = GET_ZONE(i);

        if (!VALID_ZONE(z))
            break;

        nzones++;
        for (k=0; k<16; k++) {
            int cnum = z->member[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Zone %d '", i+1);
                    print_ascii(stderr, z->name, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Scanlists: check references to channels.
    for (i=0; i<NSCANL; i++) {
        scanlist_t *sl = GET_SCANLIST(i);

        if (!VALID_SCANLIST(sl))
            break;

        nscanlists++;
        for (k=0; k<31; k++) {
            int cnum = sl->member[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Scanlist %d '", i+1);
                    print_ascii(stderr, sl->name, 15, 0);
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
            break;

        ngrouplists++;
        for (k=0; k<32; k++) {
            int cnum = gl->member[k];

            if (cnum != 0) {
                contact_t *ct = GET_CONTACT(cnum - 1);

                if (!VALID_CONTACT(ct)) {
                    fprintf(stderr, "Grouplist %d '", i+1);
                    print_ascii(stderr, gl->name, 16, 0);
                    fprintf(stderr, "': contact %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Count contacts.
    for (i=0; i<NCONTACTS; i++) {
        contact_t *ct = GET_CONTACT(i);

        if (!VALID_CONTACT(ct))
            break;
        ncontacts++;
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
// Baofeng RD-5R
//
radio_device_t radio_rd5r = {
    "Baofeng RD-5R",
    rd5r_download,
    rd5r_upload,
    rd5r_is_compatible,
    rd5r_read_image,
    rd5r_save_image,
    rd5r_print_version,
    rd5r_print_config,
    rd5r_verify_config,
    rd5r_parse_parameter,
    rd5r_parse_header,
    rd5r_parse_row,
    rd5r_update_timestamp,
};

//
// Radioddity GD-77
//
radio_device_t radio_gd77 = {
    "Radioddity GD-77",
    gd77_download,
    rd5r_upload,
    gd77_is_compatible,
    rd5r_read_image,
    rd5r_save_image,
    rd5r_print_version,
    rd5r_print_config,
    rd5r_verify_config,
    rd5r_parse_parameter,
    rd5r_parse_header,
    rd5r_parse_row,
    rd5r_update_timestamp,
};
