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
    unsigned file_offset = 0;
    unsigned last_printed = 0;

    for (f=region_map; f->length; f++) {
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

//
// Print full information about the device configuration.
//
static void d868uv_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    fprintf(out, "Radio: %s\n", radio->name);
    if (verbose)
        d868uv_print_version(radio, out);

    //TODO
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
    //TODO
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
