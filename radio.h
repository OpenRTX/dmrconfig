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

//
// Connect to the radio via the serial port.
// Identify the type of device.
//
void radio_connect(const char *port_name, const char *type);

//
// Close the serial port.
//
void radio_disconnect(void);

//
// Read firmware image from the device.
//
void radio_download(void);

//
// Write firmware image to the device.
//
void radio_upload(int cont_flag);

//
// Print a generic information about the device.
//
void radio_print_version(FILE *out);

//
// Print full information about the device configuration.
//
void radio_print_config(FILE *out, int verbose);

//
// Read firmware image from the binary file.
//
void radio_read_image(char *filename);

//
// Save firmware image to the binary file.
//
void radio_save_image(char *filename);

//
// Read the configuration from text file, and modify the firmware.
//
void radio_parse_config(char *filename);

//
// Device-dependent interface to the radio.
//
typedef struct {
    const char *name;
    void (*download)(void);
    void (*upload)(int cont_flag);
    int (*is_compatible)(void);
    void (*read_image)(FILE *img);
    void (*save_image)(FILE *img);
    void (*print_version)(FILE *out);
    void (*print_config)(FILE *out, int verbose);
    void (*parse_parameter)(char *param, char *value);
    int (*parse_header)(char *line);
    int (*parse_row)(int table_id, int first_row, char *line);
} radio_device_t;

extern radio_device_t radio_md380;   // TYT MD-380
extern radio_device_t radio_uv380;   // TYT MD-UV380

//
// Radio: memory contents.
//
extern unsigned char radio_mem[];

//
// File descriptor of serial port with programming cable attached.
//
int radio_port;

//
// Read/write progress counter.
//
int radio_progress;
