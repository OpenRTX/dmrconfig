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
#include <stdint.h>
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

int trace_flag = 0;

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
#define NDCS    (104+1)

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
     17, // For RD-5R
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

void print_hex_addr_data(unsigned addr, const unsigned char *data, int len)
{
    for (; len >= 16; len -= 16) {
        printf("%08x: ", addr);
        print_hex(data, 16);
        printf("\n");
        addr += 16;
        data += 16;
    }
    if (len > 0) {
        printf("%08x: ", addr);
        print_hex(data, len);
        printf("\n");
    }
}

//
// Strip trailing spaces and newline.
// Shorten the string in place to a specified limit.
//
char *trim_spaces(char *line, int limit)
{
    // Strip leading spaces.
    while (*line==' ' || *line=='\t')
        line++;

    // Shorten to the limit.
    unsigned len = strlen(line);
    if (len > limit)
        line[limit] = 0;

    // Strip trailing spaces and newlines.
    char *e = line + strlen(line) - 1;
    while (e >= line && (*e=='\n' || *e=='\r' || *e==' ' || *e=='\t'))
        *e-- = 0;

    return line;
}

//
// Strip optional quotes around the string.
//
char *trim_quotes(char *line)
{
    if (*line == '"') {
        int last = strlen(line) - 1;

        if (line[last] == '"') {
            line[last] = 0;
            return line+1;
        }
    }
    return line;
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

    if ((*text == 0xff || *text == 0) && fill_flag) {
        // When text is empty, still print something.
        unsigned short underscore[2] = { '_', 0 };
        text = underscore;
    }
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
// Print ASCII text until 0xff.
// For short texts, replace space with underscore.
//
void print_ascii(FILE *out, const unsigned char *text, unsigned nchars, int fill_flag)
{
    unsigned i, ch;

    if ((*text == 0xff || *text == 0) && fill_flag) {
        // When text is empty, still print something.
        text = (const unsigned char*) "_";
    }
    for (i=0; i<nchars && *text != 0xff && *text != 0; i++) {
        ch = *text++;
        if (ch == '\t')
            ch = ' ';
        if (fill_flag && ch == ' ')
            ch = '_';
        putc(ch, out);
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
// Copy ASCII string, at most nsym characters.
// Replace underscore by space.
// Fill the rest with 0xff.
//
void ascii_decode(unsigned char *dst, const char *src, unsigned nsym, unsigned fill)
{
    if (src[0] == '-' && src[1] == 0)
        src = "";

    for (; nsym > 0; nsym--) {
        int ch = *src++;

        if (ch == 0) {
            // Clear the remaining bytes.
            while (nsym-- > 0)
                *dst++ = fill;
            break;
        }
        if (ch == '_')
            ch = ' ';

        *dst++ = ch;
    }
}

//
// Copy ASCII string, at most nsym characters.
// Replace underscore by space.
// Fill the rest with 0xff.
//
void ascii_decode_uppercase(unsigned char *dst, const char *src, unsigned nsym, unsigned fill)
{
    if (src[0] == '-' && src[1] == 0)
        src = "";

    for (; nsym > 0; nsym--) {
        int ch = *src++;

        if (ch == 0) {
            // Clear the remaining bytes.
            while (nsym-- > 0)
                *dst++ = fill;
            break;
        }
        if (ch == '_')
            ch = ' ';
        else if (ch >= 'a' && ch <= 'z')
            ch += 'A' - 'a';

        *dst++ = ch;
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
        if (i >= NDCS) {
            return -1;
        }

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
// Format: abcdefgh
//
unsigned mhz_to_abcdefgh(double mhz)
{
    unsigned hz = iround(mhz * 1000000.0);
    unsigned a  = (hz / 100000000) % 10;
    unsigned b  = (hz / 10000000)  % 10;
    unsigned c  = (hz / 1000000)   % 10;
    unsigned d  = (hz / 100000)    % 10;
    unsigned e  = (hz / 10000)     % 10;
    unsigned f  = (hz / 1000)      % 10;
    unsigned g  = (hz / 100)       % 10;
    unsigned h  = (hz / 10)        % 10;

    return a << 28 | b << 24 | c << 20 | d << 16 | e << 12 | f << 8 | g << 4 | h;
}

//
// Convert frequency in MHz from floating point to
// a binary coded decimal format (8 digits).
// Format: ghefcdab
//
unsigned mhz_to_ghefcdab(double mhz)
{
    unsigned hz = iround(mhz * 1000000.0);
    unsigned a  = (hz / 100000000) % 10;
    unsigned b  = (hz / 10000000)  % 10;
    unsigned c  = (hz / 1000000)   % 10;
    unsigned d  = (hz / 100000)    % 10;
    unsigned e  = (hz / 10000)     % 10;
    unsigned f  = (hz / 1000)      % 10;
    unsigned g  = (hz / 100)       % 10;
    unsigned h  = (hz / 10)        % 10;

    return g << 28 | h << 24 | e << 20 | f << 16 | c << 12 | d << 8 | a << 4 | b;
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
// Treat 0 as empty element.
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
// Compare channel index for qsort().
// Treat 0xffff as empty element.
//
int compare_index_ffff(const void *pa, const void *pb)
{
    unsigned short a = *(unsigned short*) pa;
    unsigned short b = *(unsigned short*) pb;

    if (a == 0xffff)
        return (b != 0xffff);
    if (b == 0xffff)
        return -1;
    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

//
// Compare channel index for qsort().
// Treat 0xffffffff as empty element.
//
int compare_index_ffffffff(const void *pa, const void *pb)
{
    uint32_t a = *(uint32_t*) pa;
    uint32_t b = *(uint32_t*) pb;

    if (a == 0xffffffff)
        return (b != 0xffffffff);
    if (b == 0xffffffff)
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

//
// Initialize CSV parser.
// Check header for correctness.
// Return negative on error.
//
static int csv_skip_field1;
static int csv_join_fields34;

int csv_init(FILE *csv)
{
    char line[256];

    if (!fgets(line, sizeof(line), csv))
        return -1;

    char *field1 = line;
    char *field2 = strchr(field1,      ','); if (! field2) return -1; *field2++ = 0;
    char *field3 = strchr(field2,      ','); if (! field3) return -1; *field3++ = 0;
    char *field4 = strchr(field3,      ','); if (! field4) return -1; *field4++ = 0;

    field1 = trim_quotes(field1);
    field2 = trim_quotes(field2);
    field3 = trim_quotes(field3);
    //printf("Line: %s,%s,%s\n", field1, field2, field3);

    if (strcasecmp(field1, "Radio ID") == 0 &&
        strcasecmp(field2, "Callsign") == 0) {
        // Correct format:
        // Radio ID,Callsign,Name,City,State,Country,Remarks
        csv_skip_field1 = 0;
        csv_join_fields34 = 0;
        return 0;
    }
    if (strcasecmp(field1, "RADIO_ID") == 0 &&
        strcasecmp(field2, "CALLSIGN") == 0 &&
        strcasecmp(field3, "FIRST_NAME") == 0) {
        // Correct format:
        // RADIO_ID,CALLSIGN,FIRST_NAME,LAST_NAME,CITY,STATE,COUNTRY,REMARKS
        csv_skip_field1 = 0;
        csv_join_fields34 = 1;
        return 0;
    }
    if (strcasecmp(field2, "Radio ID") == 0 &&
        strcasecmp(field3, "Callsign") == 0) {
        // Correct format:
        // "No.","Radio ID","Callsign","Name","City","State","Country","Remarks"
        csv_skip_field1 = 1;
        csv_join_fields34 = 0;
        return 0;
    }

    fprintf(stderr, "Unexpected CSV file format!\n");
    return -1;
}

//
// Parse one line of CSV file.
// Return 1 on success, 0 on EOF.
//
int csv_read(FILE *csv, char **radioid, char **callsign, char **name,
    char **city, char **state, char **country, char **remarks)
{
    static char line[256];

again:
    if (!fgets(line, sizeof(line), csv))
        return 0;
    //printf("Line: '%s'\n", line);

    // Replace non-ASCII characters with '?'.
    char *p;
    for (p=line; *p; p++) {
        if ((uint8_t)*p > '~')
            *p = '?';
    }

    if (csv_skip_field1) {
        *radioid = strchr(line, ',');
        if (! *radioid)
            return 0;
        *(*radioid)++ = 0;
    } else
        *radioid = line;

    *callsign = strchr(*radioid,  ','); if (! *callsign) return 0; *(*callsign)++ = 0;
    *name     = strchr(*callsign, ','); if (! *name)     return 0; *(*name)++     = 0;
    *city     = strchr(*name,     ','); if (! *city)     return 0; *(*city)++     = 0;
    *state    = strchr(*city,     ','); if (! *state)    return 0; *(*state)++    = 0;
    *country  = strchr(*state,    ','); if (! *country)  return 0; *(*country)++  = 0;
    *remarks  = strchr(*country,  ','); if (! *remarks)  return 0; *(*remarks)++  = 0;
    if ((p = strchr(*remarks, ',')) != 0)
        *p++ = 0;

    if (csv_join_fields34) {
        char *name2 = *city;
        *city     = *state;
        *state    = *country;
        *country  = *remarks;
        *remarks  = p;

        if ((p = strchr(*remarks, ',')) != 0)
            *p = 0;

        if (*name2) {
            static char fullname[256];
            strcpy(fullname, *name);
            strcat(fullname, " ");
            strcat(fullname, name2);
            *name = fullname;
        }
    }
    *radioid  = trim_spaces(trim_quotes(*radioid),  100);
    *callsign = trim_spaces(trim_quotes(*callsign), 100);
    *name     = trim_spaces(trim_quotes(*name),     100);
    *city     = trim_spaces(trim_quotes(*city),     100);
    *state    = trim_spaces(trim_quotes(*state),    100);
    *country  = trim_spaces(trim_quotes(*country),  100);
    *remarks  = trim_spaces(trim_quotes(*remarks),  100);
    //printf("%s,%s,%s,%s,%s,%s,%s\n", *radioid, *callsign, *name, *city, *state, *country, *remarks);

    if (**radioid < '1' || **radioid > '9')
        goto again;
    return 1;
}
