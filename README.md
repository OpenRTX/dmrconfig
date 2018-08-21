
DMRconfig is a utility for programming digital radios via USB programming cable.
Supported radios:

 * TYT MD-380
 * TYT MD-UV380


## Usage

Read codeplug from the radio and save it to file 'device.img',
and text configuration to 'device.conf':

    dmrconfig -r [-v]

Write codeplug to the radio:

    dmrconfig -w [-v] file.img

Configure the radio from text file.
Previous codeplug is saved to 'backup.img':

    dmrconfig -c [-v] file.conf

Show configuration from the codeplug file:

    dmrconfig file.img

Option -v enables tracing of a serial protocol to the radio.


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
