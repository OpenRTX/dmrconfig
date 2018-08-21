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
#include <stdlib.h>
#include <unistd.h>
#include "radio.h"
#include "util.h"

const char version[] = VERSION;
const char *copyright;

extern char *optarg;
extern int optind;

void usage()
{
    fprintf(stderr, _("DMR Config, Version %s, %s\n"), version, copyright);
    fprintf(stderr, _("Usage:\n"));
    fprintf(stderr, _("    dmrconfig [-v] -r\n"));
    fprintf(stderr, _("                                 Save device binary image to file 'device.img',\n"));
    fprintf(stderr, _("                                 and text configuration to 'device.conf'.\n"));
    fprintf(stderr, _("    dmrconfig -w [-v] file.img\n"));
    fprintf(stderr, _("                                 Write image to device.\n"));
    fprintf(stderr, _("    dmrconfig -c [-v] file.conf\n"));
    fprintf(stderr, _("                                 Configure device from text file.\n"));
    fprintf(stderr, _("    dmrconfig -c [-v] file.img file.conf\n"));
    fprintf(stderr, _("                                 Apply text configuration to the image.\n"));
    fprintf(stderr, _("    dmrconfig file.img\n"));
    fprintf(stderr, _("                                 Display configuration from image file.\n"));
    fprintf(stderr, _("Options:\n"));
    fprintf(stderr, _("    -w           Write image to device.\n"));
    fprintf(stderr, _("    -c           Configure device from text file.\n"));
    fprintf(stderr, _("    -v           Trace serial protocol.\n"));
    exit(-1);
}

int main(int argc, char **argv)
{
    int write_flag = 0, config_flag = 0;
    const char *type = 0;

    // Set locale and message catalogs.
    setlocale(LC_ALL, "");
#ifdef MINGW32
    // Files with localized messages should be placed in
    // in c:/Program Files/dmrconfig/ directory.
    bindtextdomain("dmrconfig", "c:/Program Files/dmrconfig");
#else
    bindtextdomain("dmrconfig", "/usr/local/share/locale");
#endif
    textdomain("dmrconfig");

    copyright = _("Copyright (C) 2018 Serge Vakulenko KK6ABQ");
    serial_verbose = 0;
    for (;;) {
        switch (getopt(argc, argv, "vcwt:")) {
        case 'v': ++serial_verbose; continue;
        case 'w': ++write_flag;     continue;
        case 'c': ++config_flag;    continue;
        case 't': type = optarg;    continue;
        default:
            usage();
        case EOF:
            break;
        }
        break;
    }
    argc -= optind;
    argv += optind;
    if (write_flag + config_flag > 1) {
        fprintf(stderr, "Only one of -w or -c options is allowed.\n");
        usage();
    }
    setvbuf(stdout, 0, _IOLBF, 0);
    setvbuf(stderr, 0, _IOLBF, 0);

    if (write_flag) {
        // Restore image file to device.
        if (argc != 2 || !type)
            usage();

        radio_connect(argv[0], type);
        radio_read_image(argv[1]);
        radio_print_version(stdout);
        radio_upload(0);
        radio_disconnect();

    } else if (config_flag) {
        if (argc != 2)
            usage();

        if (is_file(argv[0])) {
            // Apply text config to image file.
            radio_read_image(argv[0]);
            radio_print_version(stdout);
            radio_parse_config(argv[1]);
            radio_save_image("device.img");

        } else {
            if (!type)
                usage();

            // Update device from text config file.
            radio_connect(argv[0], type);
            radio_download();
            radio_print_version(stdout);
            radio_save_image("backup.img");
            radio_parse_config(argv[1]);
            radio_upload(1);
            radio_disconnect();
        }

    } else {
        if (argc != 1)
            usage();

        if (is_file(argv[0])) {
            // Print configuration from image file.
            // Load image from file.
            radio_read_image(argv[0]);
            radio_print_version(stdout);
            radio_print_config(stdout, ! isatty(1));

        } else {
            if (!type)
                usage();

            // Dump device to image file.
            radio_connect(argv[0], type);
            radio_download();
            radio_print_version(stdout);
            radio_disconnect();
            radio_save_image("device.img");

            // Print configuration to file.
            const char *filename = "device.conf";
            printf("Print configuration to file '%s'.\n", filename);
            FILE *conf = fopen(filename, "w");
            if (! conf) {
                perror(filename);
                exit(-1);
            }
            radio_print_version(conf);
            radio_print_config(conf, 1);
            fclose(conf);
        }
    }
    return 0;
}
