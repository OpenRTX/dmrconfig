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
#ifdef MINGW32
#   include <windows.h>
#else
#   include <sys/stat.h>
#endif
#include "util.h"

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
// Open the serial port.
//
int serial_open()
{
    //TODO
    return 0;
}

//
// Close the serial port.
//
void serial_close(int fd)
{
    // Restore the port mode.
    //TODO
}

//
// Read data from serial port.
// Return 0 when no data available.
// Use 200-msec timeout.
//
int serial_read(int fd, unsigned char *data, int len)
{
    //TODO
    return 0;
}

//
// Write data to serial port.
//
void serial_write(int fd, const void *data, int len)
{
    //TODO
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
// Convert 32-bit value from binary coded decimal
// to integer format (8 digits).
//
int bcd_to_int(int bcd)
{
    return ((bcd >> 28) & 15) * 10000000 +
           ((bcd >> 24) & 15) * 1000000 +
           ((bcd >> 20) & 15) * 100000 +
           ((bcd >> 16) & 15) * 10000 +
           ((bcd >> 12) & 15) * 1000 +
           ((bcd >> 8)  & 15) * 100 +
           ((bcd >> 4)  & 15) * 10 +
           (bcd         & 15);
}

//
// Convert 32-bit value from integer
// binary coded decimal format (8 digits).
//
int int_to_bcd(int val)
{
    return ((val / 10000000) % 10) << 28 |
           ((val / 1000000)  % 10) << 24 |
           ((val / 100000)   % 10) << 20 |
           ((val / 10000)    % 10) << 16 |
           ((val / 1000)     % 10) << 12 |
           ((val / 100)      % 10) << 8 |
           ((val / 10)       % 10) << 4 |
           (val              % 10);
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
