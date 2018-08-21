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
#define NPMS            50
#define MEMSZ           0x6fc8

#define OFFSET_VFO      0x0048
#define OFFSET_HOME     0x01c8
#define OFFSET_CHANNELS 0x0248
#define OFFSET_PMS      0x40c8
#define OFFSET_NAMES    0x4708
#define OFFSET_ZONES    0x69c8
#define OFFSET_SCAN     0x6ec8

static const char CHARSET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ !`o$%&'()*+,-./|;/=>?@[~]^__";
#define NCHARS  65
#define SPACE   36
#define OPENBOX 64

static const char *POWER_NAME[] = { "High", "Med", "Low", "??" };

static const char *SCAN_NAME[] = { "+", "-", "Only", "??" };

enum {
    STEP_5 = 0,
    STEP_10,
    STEP_12_5,
    STEP_15,
    STEP_20,
    STEP_25,
    STEP_50,
    STEP_100,
};

//
// Data structure for a memory channel.
//
typedef struct {
    uint8_t     duplex    : 4,  // Repeater mode
#define D_SIMPLEX       0
#define D_NEG_OFFSET    2
#define D_POS_OFFSET    3
#define D_CROSS_BAND    4
                isam      : 1,  // Amplitude modulation
                isnarrow  : 1,  // Narrow FM modulation
                _u1       : 1,
                used      : 1;  // Channel is used
    uint8_t     rxfreq [3];     // Receive frequency
    uint8_t     tmode     : 3,  // CTCSS/DCS mode
#define T_OFF           0
#define T_TONE          1
#define T_TSQL          2
#define T_TSQL_REV      3
#define T_DTCS          4
#define T_D             5
#define T_T_DCS         6
#define T_D_TSQL        7
                step      : 3,  // Frequency step
                _u2       : 2;
    uint8_t     txfreq [3];     // Transmit frequency when cross-band
    uint8_t     tone      : 6,  // CTCSS tone select
#define TONE_DEFAULT    12

                power     : 2;  // Transmit power level
    uint8_t     dtcs      : 7,  // DCS code select
                _u3       : 1;
    uint8_t     _u4 [2];
    uint8_t     offset;         // TX offset, in 50kHz steps
    uint8_t     _u5 [3];
} memory_channel_t;

//
// Data structure for a channel name.
//
typedef struct {
    uint8_t     name[6];
    uint8_t     _u1       : 7,
                used      : 1;
    uint8_t     _u2       : 7,
                valid     : 1;
} memory_name_t;

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

//
// Convert a 3-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
static int freq_to_hz(uint8_t *bcd)
{
    int hz;

    hz = (bcd[0]       & 15) * 100000000 +
        ((bcd[1] >> 4) & 15) * 10000000 +
         (bcd[1]       & 15) * 1000000 +
        ((bcd[2] >> 4) & 15) * 100000 +
         (bcd[2]       & 15) * 10000;
    hz += (bcd[0] >> 6) * 2500;
    return hz;
}

//
// Convert an integet frequency value (in Hertz)
// to a 3-byte binary coded decimal format.
//
static void hz_to_freq(int hz, uint8_t *bcd)
{
    bcd[0] = (hz / 2500      % 4)  << 6 |
             (hz / 100000000 % 10);
    bcd[1] = (hz / 10000000  % 10) << 4 |
             (hz / 1000000   % 10);
    bcd[2] = (hz / 100000    % 10) << 4 |
             (hz / 10000     % 10);
}

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
// Extract channel name.
//
static void decode_name(int i, char *name)
{
    memory_name_t *nm = i + (memory_name_t*) &radio_mem[OFFSET_NAMES];

    if (nm->valid && nm->used) {
        int n, c;
        for (n=0; n<6; n++) {
            c = nm->name[n];
            name[n] = (c < NCHARS) ? CHARSET[c] : ' ';

            // Replace spaces by underscore.
            if (name[n] == ' ')
                name[n] = '_';
        }
        // Strip trailing spaces.
        for (n=5; n>=0 && name[n]=='_'; n--)
            name[n] = 0;
        name[6] = 0;
    }
}

//
// Encode a character from ASCII to internal index.
// Replace underscores by spaces.
// Make all letters uppercase.
//
static int encode_char(int c)
{
    int i;

    // Replace underscore by space.
    if (c == '_')
        c = ' ';
    if (c >= 'a' && c <= 'z')
        c += 'A' - 'a';
    for (i=0; i<NCHARS; i++)
        if (c == CHARSET[i])
            return i;
    return OPENBOX;
}

//
// Set a name for the channel.
//
static void encode_name(int i, char *name)
{
    memory_name_t *nm = i + (memory_name_t*) &radio_mem[OFFSET_NAMES];
    int n;

    if (name && *name && *name != '-') {
        // Setup channel name.
        nm->valid = 1;
        nm->used = 1;
        for (n=0; n<6 && name[n]; n++) {
            nm->name[n] = encode_char(name[n]);
        }
        for (; n<6; n++)
            nm->name[n] = SPACE;
    } else {
        // Clear name.
        nm->valid = 0;
        nm->used = 0;
        for (n=0; n<6; n++)
            nm->name[n] = 0xff;
    }
}

//
// Get all parameters for a given channel.
// Seek selects the type of channel:
//  OFFSET_VFO      - VFO channel, 0..4
//  OFFSET_HOME     - home channel, 0..4
//  OFFSET_CHANNELS - memory channel, 0..999
//  OFFSET_PMS      - programmable memory scan, i=0..99
//
static void decode_channel(int i, int seek, char *name,
    int *rx_hz, int *tx_hz, int *power, int *wide,
    int *scan, int *isam, int *step)
{
    memory_channel_t *ch = i + (memory_channel_t*) &radio_mem[seek];
    int scan_data = radio_mem[OFFSET_SCAN + i/4];

    *rx_hz = *tx_hz = 0;
    *power = *wide = *scan = *isam = *step = 0;
    if (name)
        *name = 0;
    if (! ch->used && (seek == OFFSET_CHANNELS || seek == OFFSET_PMS))
        return;

    // Extract channel name.
    if (name && seek == OFFSET_CHANNELS)
        decode_name(i, name);

    // Decode channel frequencies.
    *rx_hz = freq_to_hz(ch->rxfreq);

    *tx_hz = *rx_hz;
    switch (ch->duplex) {
    case D_NEG_OFFSET:
        *tx_hz -= ch->offset * 50000;
        break;
    case D_POS_OFFSET:
        *tx_hz += ch->offset * 50000;
        break;
    case D_CROSS_BAND:
        *tx_hz = freq_to_hz(ch->txfreq);
        break;
    }

    // Other parameters.
    *power = ch->power;
    *wide = ! ch->isnarrow;
    *scan = (scan_data << ((i & 3) * 2) >> 6) & 3;
    *isam = ch->isam;
    *step = ch->step;
}

//
// Set the parameters for a given memory channel.
//
static void setup_channel(int i, char *name, double rx_mhz, double tx_mhz,
    int tmode, int power, int wide, int scan, int isam)
{
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
    fprintf(out, "Channel Name    Receive  Transmit Power Modulation Scan\n");
    for (i=0; i<NCHAN; i++) {
        int rx_hz, tx_hz, power, wide, scan, isam, step;
        char name[17];

        decode_channel(i, OFFSET_CHANNELS, name, &rx_hz, &tx_hz, &power, &wide, &scan, &isam, &step);
        if (rx_hz == 0) {
            // Channel is disabled
            continue;
        }

        fprintf(out, "%5d   %-7s %8.4f ", i+1, name[0] ? name : "-", rx_hz / 1000000.0);
        print_offset(out, rx_hz, tx_hz);

        fprintf(out, "   %-4s  %-10s %s\n", POWER_NAME[power],
            isam ? "AM" : wide ? "Wide" : "Narrow", SCAN_NAME[scan]);
    }

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
            setup_channel(i, 0, 0, 0, 0, TONE_DEFAULT, 1, 0, 0);
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
