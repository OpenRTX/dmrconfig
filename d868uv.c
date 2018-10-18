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
#define OFFSET_BANK1        0x000040
#define OFFSET_CHAN_BITMAP  0x070a40
#define OFFSET_SETTINGS     0x071600
#define OFFSET_RADIOID      0x073d00

#define GET_SETTINGS()      ((general_settings_t*) &radio_mem[OFFSET_SETTINGS])
#define GET_RADIOID()       ((radioid_t*) &radio_mem[OFFSET_RADIOID])

#define VALID_TEXT(txt)     (*(txt) != 0 && *(txt) != 0xff)

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
    // Bytes 0-63
    uint8_t data[64];

} channel_t;

//
// General settings: 0x640 bytes at 0x02500000.
// TODO: verify the general settings with official CPS
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
// Print a generic information about the device.
//
static void d868uv_print_version(radio_device_t *radio, FILE *out)
{
    //TODO
#if 0
    unsigned char *timestamp = GET_TIMESTAMP();
    static const char charmap[16] = "0123456789:;<=>?";

    if (*timestamp != 0xff) {
        fprintf(out, "Last Programmed Date: %d%d%d%d-%d%d-%d%d",
            timestamp[0] >> 4, timestamp[0] & 15, timestamp[1] >> 4, timestamp[1] & 15,
            timestamp[2] >> 4, timestamp[2] & 15, timestamp[3] >> 4, timestamp[3] & 15);
        fprintf(out, " %d%d:%d%d:%d%d\n",
            timestamp[4] >> 4, timestamp[4] & 15, timestamp[5] >> 4, timestamp[5] & 15,
            timestamp[6] >> 4, timestamp[6] & 15);
        fprintf(out, "CPS Software Version: V%c%c.%c%c\n",
            charmap[timestamp[7] & 15], charmap[timestamp[8] & 15],
            charmap[timestamp[9] & 15], charmap[timestamp[10] & 15]);
    }
#endif
}

//
// Read memory image from the device.
//
static void d868uv_download(radio_device_t *radio)
{
    fragment_t *f;
    unsigned file_offset = 0;
    unsigned last_printed = 0;

    for (f=region_map; f->length; f++) {
        unsigned addr = f->address;
        unsigned nbytes = f->length;

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
// Print full information about the device configuration.
//
static void d868uv_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    fprintf(out, "Radio: %s\n", radio->name);
    if (verbose)
        d868uv_print_version(radio, out);

    //TODO

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
// Get channel bank by index.
//
static channel_t *get_bank(int i)
{
    return (channel_t*) &radio_mem[OFFSET_BANK1 + i*0x2000];
}

//
// Get channel by index.
//
/*static*/ channel_t *get_channel(int i)
{
    channel_t *bank   = get_bank(i >> 7);
    uint8_t   *bitmap = &radio_mem[OFFSET_CHAN_BITMAP];

    if ((bitmap[i / 8] >> (i & 7)) & 1)
        return &bank[i % 128];
    else
        return 0;
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
        channel_t *ch = GET_CHANNEL(i);

        if (!VALID_CHANNEL(ch))
            continue;

        nchannels++;
        if (ch->scan_list_index != 0) {
            scanlist_t *sl = GET_SCANLIST(ch->scan_list_index - 1);

            if (!VALID_SCANLIST(sl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_unicode(stderr, ch->name, 16, 0);
                fprintf(stderr, "': scanlist %d not found.\n", ch->scan_list_index);
                nerrors++;
            }
        }
        if (ch->contact_name_index != 0) {
            contact_t *ct = GET_CONTACT(ch->contact_name_index - 1);

            if (!VALID_CONTACT(ct)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_unicode(stderr, ch->name, 16, 0);
                fprintf(stderr, "': contact %d not found.\n", ch->contact_name_index);
                nerrors++;
            }
        }
        if (ch->group_list_index != 0) {
            grouplist_t *gl = GET_GROUPLIST(ch->group_list_index - 1);

            if (!VALID_GROUPLIST(gl)) {
                fprintf(stderr, "Channel %d '", i+1);
                print_unicode(stderr, ch->name, 16, 0);
                fprintf(stderr, "': grouplist %d not found.\n", ch->group_list_index);
                nerrors++;
            }
        }
    }

    // Zones: check references to channels.
    for (i=0; i<NZONES; i++) {
        zone_t     *z    = GET_ZONE(i);
        zone_ext_t *zext = GET_ZONEXT(i);

        if (!VALID_ZONE(z))
            continue;

        nzones++;

        // Zone A
        for (k=0; k<16; k++) {
            int cnum = z->member_a[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Zone %da '", i+1);
                    print_unicode(stderr, z->name, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
        for (k=0; k<48; k++) {
            int cnum = zext->ext_a[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Zone %da '", i+1);
                    print_unicode(stderr, z->name, 16, 0);
                    fprintf(stderr, "': channel %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }

        // Zone B
        for (k=0; k<64; k++) {
            int cnum = zext->member_b[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Zone %db '", i+1);
                    print_unicode(stderr, z->name, 16, 0);
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
            continue;

        nscanlists++;
        for (k=0; k<31; k++) {
            int cnum = sl->member[k];

            if (cnum != 0) {
                channel_t *ch = GET_CHANNEL(cnum - 1);

                if (!VALID_CHANNEL(ch)) {
                    fprintf(stderr, "Scanlist %d '", i+1);
                    print_unicode(stderr, sl->name, 16, 0);
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
        for (k=0; k<32; k++) {
            int cnum = gl->member[k];

            if (cnum != 0) {
                contact_t *ct = GET_CONTACT(cnum - 1);

                if (!VALID_CONTACT(ct)) {
                    fprintf(stderr, "Grouplist %d '", i+1);
                    print_unicode(stderr, gl->name, 16, 0);
                    fprintf(stderr, "': contact %d not found.\n", cnum);
                    nerrors++;
                }
            }
        }
    }

    // Count contacts.
    for (i=0; i<NCONTACTS; i++) {
        contact_t *ct = GET_CONTACT(i);

        if (VALID_CONTACT(ct))
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
    //d868uv_write_csv,
};
