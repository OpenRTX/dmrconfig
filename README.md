
DMRconfig is a utility for programming digital radios via USB programming cable.
Supported radios:

 * TYT MD-380, Retevis RT3, RT8
 * TYT MD-2017, Retevis RT82 (untested)
 * TYT MD-UV380
 * TYT MD-UV390, Retevis RT3S (untested)
 * TYT MD-9600 (untested)
 * Baofeng RD-5R
 * Radioddity GD-77
 * Zastone D900 (untested)
 * Zastone DP880 (untested)
 * Radtel RT-27D (untested)

## Usage

Read codeplug from the radio and save it to file 'device.img',
and also save text configuration to 'device.conf':

    dmrconfig -r [-t]

Write codeplug to the radio:

    dmrconfig -w [-t] file.img

Configure the radio from text file.
Previous codeplug is saved to 'backup.img':

    dmrconfig -c [-t] file.conf

Show configuration from the codeplug file:

    dmrconfig file.img

Apply configuration from text file to the codeplug file:

    dmrconfig -c file.img file.conf

Update database of contacts from CSV file:

    dmrconfig -u [-t] file.csv

Option -t enables tracing of USB protocol.

## Permissions

On Linux, a permission to access USB device is required.
It's possible to run dmrconfig as root, like "sudo dmrconfig",
but it's safer to enable access for users.
Create a file /etc/udev/rules.d/99-dmr.rules with the following contents:

    # TYT MD-UV380
    SUBSYSTEM=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="df11", MODE="666"

    # Baofeng RD-5R
    SUBSYSTEM=="usb", ATTRS{idVendor}=="15a2", ATTRS{idProduct}=="0073", MODE="666"

To activate it, run:

    sudo udevadm control --reload-rules

Then re-attach the USB cable to the radio.

## Sources

Sources are distributed freely under the terms of Apache 2.0 license.
You can download sources via GIT:

    git clone https://github.com/sergev/dmrconfig


To build on Linux or Mac OS X, run:

    make
    make install


Regards,
Serge Vakulenko
KK6ABQ
