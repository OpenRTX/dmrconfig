/*
 * Auxiliary functions.
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
#include <fcntl.h>
#include <time.h>
#ifdef MINGW32
#   include <windows.h>
#else
#   include <sys/stat.h>
#endif
#include "util.h"

//
// CTCSS tones, Hz*10.
//
#define NCTCSS  50

static const int CTCSS_TONES [NCTCSS] = {
     670,  693,  719,  744,  770,  797,  825,  854,  885,  915,
     948,  974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
};

//
// DCS codes.
//
#define NDCS    104

static const int DCS_CODES[NDCS] = {
     23,  25,  26,  31,  32,  36,  43,  47,  51,  53,
     54,  65,  71,  72,  73,  74, 114, 115, 116, 122,
    125, 131, 132, 134, 143, 145, 152, 155, 156, 162,
    165, 172, 174, 205, 212, 223, 225, 226, 243, 244,
    245, 246, 251, 252, 255, 261, 263, 265, 266, 271,
    274, 306, 311, 315, 325, 331, 332, 343, 346, 351,
    356, 364, 365, 371, 411, 412, 413, 423, 431, 432,
    445, 446, 452, 454, 455, 462, 464, 465, 466, 503,
    506, 516, 523, 526, 532, 546, 565, 606, 612, 624,
    627, 631, 632, 654, 662, 664, 703, 712, 723, 731,
    732, 734, 743, 754,
};

//
// Check for a regular file.
//
int is_file(char *filename)
{
#ifdef MINGW32
    // Treat COM* as a device.
    return strncasecmp(filename, "com", 3) != 0;
#else
    struct stat st;

    if (stat(filename, &st) < 0) {
        // File not exist: treat it as a regular file.
        return 1;
    }
    return (st.st_mode & S_IFMT) == S_IFREG;
#endif
}

//
// Print data in hex format.
//
void print_hex(const unsigned char *data, int len)
{
    int i;

    printf("%02x", (unsigned char) data[0]);
    for (i=1; i<len; i++)
        printf("-%02x", (unsigned char) data[i]);
}

//
// Delay in milliseconds.
//
void mdelay(unsigned msec)
{
#ifdef MINGW32
    Sleep(msec);
#else
    usleep(msec * 1000);
#endif
}

//
// Round double value to integer.
//
static int iround(double x)
{
    if (x >= 0)
        return (int)(x + 0.5);

    return -(int)(-x + 0.5);
}

//
// Get a binary value of the parameter: On/Off,
// Ignore case.
// For invlid value, print a message and halt.
//
int on_off(char *param, char *value)
{
    if (strcasecmp("On", value) == 0)
        return 1;
    if (strcasecmp("Off", value) == 0)
        return 0;
    fprintf(stderr, "Bad value for %s: %s\n", param, value);
    exit(-1);
}

//
// Get integer value, or "Off" as 0,
// Ignore case.
//
int atoi_off(const char *value)
{
    if (strcasecmp("Off", value) == 0)
        return 0;
    return atoi(value);
}

//
// Copy a text string to memory image.
// Clear unused part with spaces.
//
void copy_str(unsigned char *dest, const char *src, int nbytes)
{
    int i;

    for (i=0; i<nbytes; i++) {
        *dest++ = (*src ? *src++ : ' ');
    }
}

//
// Find a string in a table of size nelem, ignoring case.
// Return -1 when not found.
//
int string_in_table(const char *value, const char *tab[], int nelem)
{
    int i;

    for (i=0; i<nelem; i++) {
        if (strcasecmp(tab[i], value) == 0) {
            return i;
        }
    }
    return -1;
}

//
// Print description of the parameter.
//
void print_options(FILE *out, const char **tab, int num, const char *info)
{
    int i;

    fprintf(out, "\n");
    if (info)
        fprintf(out, "# %s\n", info);
    fprintf(out, "# Options:");
    for (i=0; i<num; i++) {
        if (i > 0)
            fprintf(out, ",");
        fprintf(out, " %s", tab[i]);
    }
    fprintf(out, "\n");
}

//
// Write Unicode symbol to file.
// Convert to UTF-8 encoding:
// 00000000.0xxxxxxx -> 0xxxxxxx
// 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
// xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
//
void putc_utf8(unsigned short ch, FILE *out)
{
    if (ch < 0x80) {
        putc (ch, out);

    } else if (ch < 0x800) {
        putc (ch >> 6 | 0xc0, out);
        putc ((ch & 0x3f) | 0x80, out);

    } else {
        putc (ch >> 12 | 0xe0, out);
        putc (((ch >> 6) & 0x3f) | 0x80, out);
        putc ((ch & 0x3f) | 0x80, out);
    }
}

//
// Print utf16 text as utf8.
// For short texts, replace space with underscore.
//
void print_unicode(FILE *out, const unsigned short *text, unsigned nchars, int fill_flag)
{
    unsigned i, ch;

    for (i=0; i<nchars && *text; i++) {
        ch = *text++;
        if (ch == '\t')
            ch = ' ';
        if (nchars <= 16 && ch == ' ')
            ch = '_';
        putc_utf8(ch, out);
    }
    if (fill_flag) {
        for (; i<nchars; i++) {
            putc(' ', out);
        }
    }
}

//
// Get local time in format: YYYYMMDDhhmmss
//
void get_timestamp(char p[16])
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);

    if (! local) {
        perror("localtime");
        exit(-1);
    }
    if (!strftime(p, 16, "%Y%m%d%H%M%S", local)) {
        perror("strftime");
        exit(-1);
    }
}

//
// Fetch Unicode symbol from UTF-8 string.
// Advance string pointer.
//
int utf8_to_unicode(const char **p)
{
    int c1, c2, c3;

    c1 = (unsigned char) *(*p)++;
    if (! (c1 & 0x80))
        return c1;
    c2 = (unsigned char) *(*p)++;
    if (! (c1 & 0x20))
        return (c1 & 0x1f) << 6 | (c2 & 0x3f);
    c3 = (unsigned char) *(*p)++;
    return (c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 | (c3 & 0x3f);
}

//
// Decode UTF-8 string into UCS-2 string, at most nsym characters.
// Replace underscore by space.
//
void utf8_decode(unsigned short *dst, const char *src, unsigned nsym)
{
    if (src[0] == '-' && src[1] == 0)
        src = "";

    for (; nsym > 0; nsym--) {
        int ch = utf8_to_unicode(&src);

        if (ch == '_')
            ch = ' ';
        *dst++ = ch;

        if (ch == 0) {
            // Clear the remaining bytes.
            while (--nsym > 0)
                *dst++ = 0;
            break;
        }
    }
}

//
// Convert tone string to BCD format.
// Four possible formats:
// nnn.n - CTCSS frequency
// DnnnN - DCS normal
// DnnnI - DCS inverted
// '-'   - Disabled
//
int encode_tone(char *str)
{
    unsigned val, tag, a, b, c, d;

    if (*str == '-') {
        // Disabled
        return 0xffff;

    } else if (*str == 'D' || *str == 'd') {
        //
        // DCS tone
        //
        char *e;
        val = strtoul(++str, &e, 10);

        // Find a valid index in DCS table.
        int i;
        for (i=0; i<NDCS; i++)
            if (DCS_CODES[i] == val)
                break;
        if (i >= NDCS)
            return -1;

        a = 0;
        b = val / 100;
        c = val / 10 % 10;
        d = val % 10;

        if (*e == 'N' || *e == 'n') {
            tag = 2;
        } else if (*e == 'I' || *e == 'i') {
            tag = 3;
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

        // Find a valid index in CTCSS table.
        int i;
        for (i=0; i<NCTCSS; i++)
            if (CTCSS_TONES[i] == val)
                break;
        if (i >= NCTCSS)
            return -1;

        a = val / 1000;
        b = val / 100 % 10;
        c = val / 10 % 10;
        d = val % 10;
        tag = 0;
    } else {
        return -1;
    }

    return (a << 12) | (b << 8) | (c << 4) | d | (tag << 14);
}

//
// Print frequency (BCD value).
//
void print_freq(FILE *out, unsigned data)
{
    fprintf(out, "%d%d%d.%d%d%d", (data >> 28) & 15, (data >> 24) & 15,
        (data >> 20) & 15, (data >> 16) & 15,
        (data >> 12) & 15, (data >> 8) & 15);

    if ((data & 0xff) == 0) {
        fputs("  ", out);
    } else {
        fprintf(out, "%d", (data >> 4) & 15);
        if ((data & 0x0f) == 0) {
            fputs(" ", out);
        } else {
            fprintf(out, "%d", data & 15);
        }
    }
}

//
// Convert frequency in MHz from floating point to
// a binary coded decimal format (8 digits).
//
unsigned mhz_to_bcd(double mhz)
{
    unsigned hz = iround(mhz * 1000000.0);

    return ((hz / 100000000) % 10) << 28 |
           ((hz / 10000000)  % 10) << 24 |
           ((hz / 1000000)   % 10) << 20 |
           ((hz / 100000)    % 10) << 16 |
           ((hz / 10000)     % 10) << 12 |
           ((hz / 1000)      % 10) << 8 |
           ((hz / 100)       % 10) << 4 |
           ((hz / 10)        % 10);
}

//
// Convert a 4-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
int freq_to_hz(unsigned bcd)
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
// Print frequency as MHz.
//
void print_mhz(FILE *out, unsigned hz)
{
    if (hz % 1000000 == 0)
        fprintf(out, "%-8u", hz / 1000000);
    else if (hz % 100000 == 0)
        fprintf(out, "%-8.1f", hz / 1000000.0);
    else if (hz % 10000 == 0)
        fprintf(out, "%-8.2f", hz / 1000000.0);
    else if (hz % 1000 == 0)
        fprintf(out, "%-8.3f", hz / 1000000.0);
    else if (hz % 100 == 0)
        fprintf(out, "%-8.4f", hz / 1000000.0);
    else
        fprintf(out, "%-8.5f", hz / 1000000.0);
}

//
// Print the transmit offset or frequency.
//
void print_offset(FILE *out, unsigned rx_bcd, unsigned tx_bcd)
{
    int rx_hz = freq_to_hz(rx_bcd);
    int tx_hz = freq_to_hz(tx_bcd);
    int delta = tx_hz - rx_hz;

    if (delta == 0) {
        fprintf(out, "+0       ");
    } else if (delta > 0 && delta/50000 <= 255) {
        fprintf(out, "+");
        print_mhz(out, delta);
    } else if (delta < 0 && -delta/50000 <= 255) {
        fprintf(out, "-");
        print_mhz(out, -delta);
    } else {
        fprintf(out, " ");
        print_mhz(out, tx_hz);
    }
}

//
// Compare channel index for qsort().
//
int compare_index(const void *pa, const void *pb)
{
    unsigned short a = *(unsigned short*) pa;
    unsigned short b = *(unsigned short*) pb;

    if (a == 0)
        return (b != 0);
    if (b == 0)
        return -1;
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

//
// Print CTSS or DCS tone.
//
void print_tone(FILE *out, unsigned data)
{
    if (data == 0xffff) {
        fprintf(out, "-    ");
        return;
    }

    unsigned tag = data >> 14;
    unsigned a = (data >> 12) & 3;
    unsigned b = (data >> 8) & 15;
    unsigned c = (data >> 4) & 15;
    unsigned d = data & 15;

    switch (tag) {
    default:
        // CTCSS
        if (a == 0)
            fprintf(out, "%d%d.%d ", b, c, d);
        else
            fprintf(out, "%d%d%d.%d", a, b, c, d);
        break;
    case 2:
        // DCS-N
        fprintf(out, "D%d%d%dN", b, c, d);
        break;
    case 3:
        // DCS-I
        fprintf(out, "D%d%d%dI", b, c, d);
        break;
    }
}
