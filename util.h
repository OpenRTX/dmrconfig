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
int trace_flag;

//
// Print data in hex format.
//
void print_hex(const unsigned char *data, int len);

//
// DFU functions.
//
const char *dfu_init(unsigned vid, unsigned pid);
void dfu_close(void);
void dfu_erase(unsigned start, unsigned finish);
void dfu_read_block(int bno, unsigned char *data, int nbytes);
void dfu_write_block(int bno, unsigned char *data, int nbytes);
void dfu_reboot(void);

//
// HID functions.
//
int hid_init(int vid, int pid);
const char *hid_identify(void);
void hid_close(void);
void hid_send_recv(const unsigned char *data, unsigned nbytes, unsigned char *rdata, unsigned rlength);
void hid_read_block(int bno, unsigned char *data, int nbytes);
void hid_read_finish(void);
void hid_write_block(int bno, unsigned char *data, int nbytes);
void hid_write_finish(void);

//
// Serial functions.
//
int serial_init(int vid, int pid);
const char *serial_identify(void);
void serial_close(void);
void serial_read_region(int addr, unsigned char *data, int nbytes);
void serial_write_region(int addr, unsigned char *data, int nbytes);

//
// Delay in milliseconds.
//
void mdelay(unsigned msec);

//
// Check for a regular file.
//
int is_file(char *filename);

//
// Convert frequency in MHz from floating point to
// a binary coded decimal format (8 digits).
//
unsigned mhz_to_bcd(double mhz);

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
void print_ascii(FILE *out, const unsigned char *text, unsigned nchars, int fill_flag);

//
// Fetch Unicode symbol from UTF-8 string.
// Advance string pointer.
//
int utf8_to_unicode(const char **p);

//
// Decode UTF-8 string into UCS-2 string, at most nsym characters.
//
void utf8_decode(unsigned short *dst, const char *src, unsigned nsym);

//
// Copy ASCII string, at most nsym characters.
// Replace underscore by space.
//
void ascii_decode(unsigned char *dst, const char *src, unsigned nsym);

//
// Get local time in format: YYYYMMDDhhmmss
//
void get_timestamp(char p[16]);

//
// Convert tone string to BCD format.
// Return -1 on error.
// Four possible formats:
// nnn.n - CTCSS frequency
// DnnnN - DCS normal
// DnnnI - DCS inverted
// '-'   - Disabled
//
int encode_tone(char *str);

//
// Print frequency (BCD value).
//
void print_freq(FILE *out, unsigned data);

//
// Convert a 4-byte frequency value from binary coded decimal
// to integer format (in Hertz).
//
int freq_to_hz(unsigned bcd);

//
// Print frequency as MHz.
//
void print_mhz(FILE *out, unsigned hz);

//
// Print the transmit offset or frequency.
//
void print_offset(FILE *out, unsigned rx_bcd, unsigned tx_bcd);

//
// Compare channel index for qsort().
//
int compare_index(const void *pa, const void *pb);

//
// Print CTSS or DCS tone.
//
void print_tone(FILE *out, unsigned data);
