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
void radio_connect(void);

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
void radio_read_image(const char *filename);

//
// Save firmware image to the binary file.
//
void radio_save_image(const char *filename);

//
// Read the configuration from text file, and modify the firmware.
//
void radio_parse_config(const char *filename);

//
// Attempt to read the configuration file, see if it can be parsed successfully for any radio.
//
void radio_validate_config(const char *filename);

//
// Check the configuration.
//
void radio_verify_config(void);

//
// Update CSV contacts database.
//
void radio_write_csv(const char *filename);

//
// List all supported radios.
//
void radio_list(void);

//
// Check for compatible radio model.
//
int radio_is_compatible(const char *ident);

//
// Device-dependent interface to the radio.
//
typedef struct _radio_device_t radio_device_t;
struct _radio_device_t {
    const char *name;
    void (*download)(radio_device_t *radio);
    void (*upload)(radio_device_t *radio, int cont_flag);
    int (*is_compatible)(radio_device_t *radio);
    void (*read_image)(radio_device_t *radio, FILE *img);
    void (*save_image)(radio_device_t *radio, FILE *img);
    void (*print_version)(radio_device_t *radio, FILE *out);
    void (*print_config)(radio_device_t *radio, FILE *out, int verbose);
    int (*verify_config)(radio_device_t *radio);
    void (*parse_parameter)(radio_device_t *radio, char *param, char *value);
    int (*parse_header)(radio_device_t *radio, char *line);
    int (*parse_row)(radio_device_t *radio, int table_id, int first_row, char *line);
    void (*update_timestamp)(radio_device_t *radio);
    void (*write_csv)(radio_device_t *radio, FILE *csv);
    int channel_count;
};

extern radio_device_t radio_md380;      // TYT MD-380
extern radio_device_t radio_md390;      // TYT MD-390
extern radio_device_t radio_md2017;     // TYT MD-2017
extern radio_device_t radio_uv380;      // TYT MD-UV380
extern radio_device_t radio_uv390;      // TYT MD-UV390
extern radio_device_t radio_md9600;     // TYT MD-9600
extern radio_device_t radio_d900;       // Zastone D900
extern radio_device_t radio_dp880;      // Zastone DP880
extern radio_device_t radio_rt27d;      // Radtel RT-27D
extern radio_device_t radio_rd5r;       // Baofeng RD-5R
extern radio_device_t radio_gd77;       // Radioddity GD-77, version 3.1.1 and later
extern radio_device_t radio_dm1801;     // Baofeng DM-1801
extern radio_device_t radio_d868uv;     // Anytone AT-D868UV
extern radio_device_t radio_d878uv;     // Anytone AT-D878UV
extern radio_device_t radio_dmr6x2;     // BTECH DMR-6x2
extern radio_device_t radio_rt84;       // Baofeng DM-1701, Retevis RT84

//
// Radio: memory contents.
//
extern unsigned char radio_mem[];

//
// File descriptor of serial port with programming cable attached.
//
extern int radio_port;

//
// Read/write progress counter.
//
extern int radio_progress;
