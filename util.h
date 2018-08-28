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

//
// Localization.
//
#if 0
#include <libintl.h>
#define _(str)              gettext(str)
#else
#define _(str)              str
#define setlocale(x,y)      /* empty */
#define bindtextdomain(x,y) /* empty */
#define textdomain(x)       /* empty */
#endif

//
// Program version.
//
extern const char version[];
extern const char *copyright;

//
// Trace data i/o via the serial port.
//
int serial_verbose;

//
// Print data in hex format.
//
void print_hex(const unsigned char *data, int len);

//
// DFU functions.
//
const char *dfu_init(unsigned vid, unsigned pid);
void dfu_close(void);
void dfu_erase(int nbytes);
void dfu_read_block(int bno, unsigned char *data, int nbytes);
void dfu_write_block(int bno, unsigned char *data, int nbytes);

//
// Delay in milliseconds.
//
void mdelay(unsigned msec);

//
// Check for a regular file.
//
int is_file(char *filename);

//
// Convert 32-bit value from binary coded decimal
// to integer format (8 digits).
//
int bcd_to_int(int bcd);

//
// Convert 32-bit value from integer
// binary coded decimal format (8 digits).
//
int int_to_bcd(int val);

//
// Get a binary value of the parameter: On/Off,
// Ignore case.
//
int on_off(char *param, char *value);

//
// Get integer value, or "Off" as 0,
// Ignore case.
//
int atoi_off(const char *value);

//
// Copy a text string to memory image.
// Clear unused space to zero.
//
void copy_str(unsigned char *dest, const char *src, int nbytes);

//
// Find a string in a table of size nelem, ignoring case.
// Return -1 when not found.
//
int string_in_table(const char *value, const char *tab[], int nelem);

//
// Print description of the parameter.
//
void print_options(FILE *out, const char **tab, int num, const char *info);

//
// Print list of all squelch tones.
//
void print_squelch_tones(FILE *out, int normal_only);

//
// Write Unicode symbol to a file in UTF-8 encoding.
//
void putc_utf8(unsigned short ch, FILE *out);

//
// Print utf16 text as utf8.
//
void print_unicode(FILE *out, const unsigned short *text, unsigned nchars, int fill_flag);
