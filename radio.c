/*
 * Configuration Utility for DMR radios.
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
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include "radio.h"
#include "util.h"

static struct {
    char *ident;
    radio_device_t *device;
} radio_tab[] = {
    { "DR780",      &radio_md380 },     // TYT MD-380, Retevis RT3, RT8
    { "MD-UV380",   &radio_uv380 },     // TYT MD-UV380
    { "MD-UV390",   &radio_uv390 },     // TYT MD-UV390, Retevis RT3S
    { "2017",       &radio_md2017 },    // TYT MD-2017, Retevis RT82
    { "MD9600",     &radio_md9600 },    // TYT MD-9600
    { "BF-5R",      &radio_rd5r },      // Baofeng RD-5R, TD-5R
    { "MD-760P",    &radio_gd77 },      // Radioddity GD-77, version 3.1.1 and later
    { "D868UVE",    &radio_d868uv },    // Anytone AT-D868UV
    { "D6X2UV",     &radio_dmr6x2 },    // BTECH DMR-6x2
    { "ZD3688",     &radio_d900 },      // Zastone D900
    { "TP660",      &radio_dp880 },     // Zastone DP880
    { "ZN><:",      &radio_rt27d },     // Radtel RT-27D
    { 0, 0 }
};

unsigned char radio_mem [1024*1024*2];  // Radio memory contents, up to 2 Mbytes
int radio_progress;                     // Read/write progress counter

static radio_device_t *device;          // Device-dependent interface

//
// Close the serial port.
//
void radio_disconnect()
{
    fprintf(stderr, "Close device.\n");

    // Restore the normal radio mode.
    dfu_reboot();
    dfu_close();
    hid_close();
    serial_close();
}

//
// Print a generic information about the device.
//
void radio_print_version(FILE *out)
{
    device->print_version(device, out);
}

//
// Connect to the radio and identify the type of device.
//
void radio_connect()
{
    const char *ident;
    int i;

    // Try TYT MD family.
    ident = dfu_init(0x0483, 0xdf11);
    if (! ident) {
        // Try RD-5R and GD-77.
        if (hid_init(0x15a2, 0x0073) >= 0)
            ident = hid_identify();
    }
    if (! ident) {
        // Try AT-D868UV.
        if (serial_init(0x28e9, 0x018a) >= 0)
            ident = serial_identify();
    }
    if (! ident) {
        fprintf(stderr, "No radio detected.\n");
        fprintf(stderr, "Check your USB cable!\n");
        exit(-1);
    }

    for (i=0; radio_tab[i].ident; i++) {
        if (strcasecmp(ident, radio_tab[i].ident) == 0) {
            device = radio_tab[i].device;
            break;
        }
    }
    if (! device) {
        fprintf(stderr, "Unrecognized radio '%s'.\n", ident);
        exit(-1);
    }
    fprintf(stderr, "Connect to %s.\n", device->name);
}

//
// List all supported radios.
//
void radio_list()
{
    int i;

    printf("Supported radios:\n");
    for (i=0; radio_tab[i].ident; i++) {
        printf("    %s\n", radio_tab[i].device->name);
    }
}

//
// Read firmware image from the device.
//
void radio_download()
{
    radio_progress = 0;
    if (! trace_flag) {
        fprintf(stderr, "Read device: ");
        fflush(stderr);
    }

    device->download(device);

    if (! trace_flag)
        fprintf(stderr, " done.\n");
}

//
// Write firmware image to the device.
//
void radio_upload(int cont_flag)
{
    // Check for compatibility.
    if (! device->is_compatible(device)) {
        fprintf(stderr, "Incompatible image - cannot upload.\n");
        exit(-1);
    }
    radio_progress = 0;
    if (! trace_flag) {
        fprintf(stderr, "Write device: ");
        fflush(stderr);
    }
    device->upload(device, cont_flag);

    if (! trace_flag)
        fprintf(stderr, " done.\n");
}

//
// Read firmware image from the binary file.
//
void radio_read_image(const char *filename)
{
    FILE *img;
    struct stat st;
    char ident[8];

    fprintf(stderr, "Read codeplug from file '%s'.\n", filename);
    img = fopen(filename, "rb");
    if (! img) {
        perror(filename);
        exit(-1);
    }

    // Guess device type by file size.
    if (stat(filename, &st) < 0) {
        perror(filename);
        exit(-1);
    }
    switch (st.st_size) {
    case 851968:
    case 852533:
        device = &radio_uv380;
        break;
    case 262144:
    case 262709:
        device = &radio_md380;
        break;
    case 1606528:
        if (fread(ident, 1, 8, img) != 8) {
            fprintf(stderr, "%s: Cannot read header.\n", filename);
            exit(-1);
        }
        fseek(img, 0, SEEK_SET);
        if (memcmp(ident, "D868UVE", 7) == 0) {
            device = &radio_d868uv;
        } else if (memcmp(ident, "D6X2UV", 6) == 0) {
            device = &radio_dmr6x2;
        } else {
            fprintf(stderr, "%s: Unrecognized header '%.6s'\n",
                filename, ident);
            exit(-1);
        }
        break;
    case 131072:
        if (fread(ident, 1, 8, img) != 8) {
            fprintf(stderr, "%s: Cannot read header.\n", filename);
            exit(-1);
        }
        fseek(img, 0, SEEK_SET);
        if (memcmp(ident, "BF-5R", 5) == 0) {
            device = &radio_rd5r;
        } else if (memcmp(ident, "MD-760P", 7) == 0) {
            device = &radio_gd77;
        } else if (memcmp(ident, "MD-760", 6) == 0) {
            fprintf(stderr, "Old Radioddity GD-77 v2.6 image not supported!\n");
            exit(-1);
        } else {
            fprintf(stderr, "%s: Unrecognized header '%.6s'\n",
                filename, ident);
            exit(-1);
        }
        break;
    default:
        fprintf(stderr, "%s: Unrecognized file size %u bytes.\n",
            filename, (int) st.st_size);
        exit(-1);
    }

    device->read_image(device, img);
    fclose(img);
}

//
// Save firmware image to the binary file.
//
void radio_save_image(const char *filename)
{
    FILE *img;

    fprintf(stderr, "Write codeplug to file '%s'.\n", filename);
    img = fopen(filename, "wb");
    if (! img) {
        perror(filename);
        exit(-1);
    }
    device->save_image(device, img);
    fclose(img);
}

//
// Read the configuration from text file, and modify the firmware.
//
void radio_parse_config(const char *filename)
{
    FILE *conf;
    char line [256], *p, *v;
    int table_id = 0, table_dirty = 0;

    fprintf(stderr, "Read configuration from file '%s'.\n", filename);
    conf = fopen(filename, "r");
    if (! conf) {
        perror(filename);
        exit(-1);
    }

    device->channel_count = 0;
    while (fgets(line, sizeof(line), conf)) {
        line[sizeof(line)-1] = 0;

        // Strip comments.
        v = strchr(line, '#');
        if (v)
            *v = 0;

        // Strip trailing spaces and newline.
        v = line + strlen(line) - 1;
        while (v >= line && (*v=='\n' || *v=='\r' || *v==' ' || *v=='\t'))
            *v-- = 0;

        // Ignore comments and empty lines.
        p = line;
        if (*p == 0)
            continue;

        if (*p != ' ') {
            // Table finished.
            table_id = 0;

            // Find the value.
            v = strchr(p, ':');
            if (! v) {
                // Table header: get table type.
                table_id = device->parse_header(device, p);
                if (! table_id) {
badline:            fprintf(stderr, "Invalid line: '%s'\n", line);
                    exit(-1);
                }
                table_dirty = 0;
                continue;
            }

            // Parameter.
            *v++ = 0;

            // Skip spaces.
            while (*v == ' ' || *v == '\t')
                v++;

            device->parse_parameter(device, p, v);

        } else {
            // Table row or comment.
            // Skip spaces.
            // Ignore comments and empty lines.
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '#' || *p == 0)
                continue;
            if (! table_id) {
                goto badline;
            }

            if (! device->parse_row(device, table_id, ! table_dirty, p)) {
                goto badline;
            }
            table_dirty = 1;
        }
    }
    fclose(conf);
    device->update_timestamp(device);
}

//
// Print full information about the device configuration.
//
void radio_print_config(FILE *out, int verbose)
{
    if (verbose) {
        char buf [40];
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);
        if (! tmp || ! strftime(buf, sizeof(buf), "%Y/%m/%d ", tmp))
            buf[0] = 0;
        fprintf(out, "#\n");
        fprintf(out, "# This configuration was generated %sby dmrconfig,\n", buf);
        fprintf(out, "# Version %s, %s\n", version, copyright);
        fprintf(out, "#\n");
    }
    device->print_config(device, out, verbose);
}

//
// Check the configuration is correct.
//
void radio_verify_config()
{
    if (!device->verify_config(device)) {
        // Message should be already printed.
        exit(-1);
    }
}

//
// Update contacts database on the device.
//
void radio_write_csv(const char *filename)
{
    FILE *csv;

    if (!device->write_csv) {
        fprintf(stderr, "%s does not support CSV database.\n", device->name);
        return;
    }

    csv = fopen(filename, "r");
    if (! csv) {
        perror(filename);
        return;
    }
    fprintf(stderr, "Read file '%s'.\n", filename);

    device->write_csv(device, csv);
    fclose(csv);
}

//
// Check for compatible radio model.
//
int radio_is_compatible(const char *name)
{
    int i;

    for (i=0; radio_tab[i].ident; i++) {
        // Radio is compatible when it has the same parse routine.
        if (device->parse_parameter == radio_tab[i].device->parse_parameter &&
            strcasecmp(name, radio_tab[i].device->name) == 0) {
            return 1;
        }
    }
    return 0;
}
