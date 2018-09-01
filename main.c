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
    fprintf(stderr, _("    dmrconfig -r [-t]\n"));
    fprintf(stderr, _("                                 Read codeplug from the radio and save it to a binary file 'device.img'.\n"));
    fprintf(stderr, _("                                 Save configuration to a text file 'device.conf'.\n"));
    fprintf(stderr, _("    dmrconfig -w [-t] file.img\n"));
    fprintf(stderr, _("                                 Write codeplug to the radio.\n"));
    fprintf(stderr, _("    dmrconfig -c [-t] file.conf\n"));
    fprintf(stderr, _("                                 Apply configuration script to the radio.\n"));
    fprintf(stderr, _("    dmrconfig -c file.img file.conf\n"));
    fprintf(stderr, _("                                 Apply configuration script to the codeplug image.\n"));
    fprintf(stderr, _("                                 Store modified copy to a file 'device.img'.\n"));
    fprintf(stderr, _("    dmrconfig file.img\n"));
    fprintf(stderr, _("                                 Display configuration from the codeplug image.\n"));
    fprintf(stderr, _("Options:\n"));
    fprintf(stderr, _("    -r           Read codeplug from the radio.\n"));
    fprintf(stderr, _("    -w           Write codeplug to the radio.\n"));
    fprintf(stderr, _("    -c           Configure the radio from a text script.\n"));
    fprintf(stderr, _("    -t           Trace USB protocol.\n"));
    exit(-1);
}

int main(int argc, char **argv)
{
    int read_flag = 0, write_flag = 0, config_flag = 0;

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
    trace_flag = 0;
    for (;;) {
        switch (getopt(argc, argv, "tcwr")) {
        case 't': ++trace_flag; continue;
        case 'r': ++read_flag;      continue;
        case 'w': ++write_flag;     continue;
        case 'c': ++config_flag;    continue;
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
        if (argc != 1)
            usage();

        radio_connect();
        radio_read_image(argv[0]);
        radio_print_version(stdout);
        radio_upload(0);
        radio_disconnect();

    } else if (config_flag) {
        if (argc != 1 && argc != 2)
            usage();

        if (argc == 2) {
            // Apply text config to image file.
            radio_read_image(argv[0]);
            radio_print_version(stdout);
            radio_parse_config(argv[1]);
            radio_verify_config();
            radio_save_image("device.img");

        } else {
            // Update device from text config file.
            radio_connect();
            radio_download();
            radio_print_version(stdout);
            radio_save_image("backup.img");
            radio_parse_config(argv[0]);
            radio_verify_config();
            radio_upload(1);
            radio_disconnect();
        }

    } else if (read_flag) {
        if (argc != 0)
            usage();

        // Dump device to image file.
        radio_connect();
        radio_download();
        radio_print_version(stdout);
        radio_disconnect();
        radio_save_image("device.img");

        // Print configuration to file.
        const char *filename = "device.conf";
        printf("Print configuration to file '%s'.\n", filename);
        FILE *conf = fopen(filename, "w");
        if (!conf) {
            perror(filename);
            exit(-1);
        }
        radio_print_config(conf, 1);
        fclose(conf);
    } else {
        if (argc != 1)
            usage();

        // Print configuration from image file.
        radio_read_image(argv[0]);
        radio_print_config(stdout, !isatty(1));
    }
    return 0;
}
