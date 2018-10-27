/*
 * Interface to virtual serial USB port.
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
#include <errno.h>
#include "util.h"

#if defined(__WIN32__) || defined(WIN32)
    #include <windows.h>
    #include <malloc.h>
    static void *fd = INVALID_HANDLE_VALUE;
    static DCB saved_mode;
#else
    #include <termios.h>
    static int fd = -1;
    static struct termios saved_mode;
#endif

#ifdef __linux__
#   include <libudev.h>
#endif

#ifdef __APPLE__
#   include <CoreFoundation/CoreFoundation.h>
#   include <IOKit/usb/IOUSBLib.h>
#   include <IOKit/serial/IOSerialKeys.h>
#endif

static char *dev_path;

static const unsigned char CMD_PRG[]   = "PROGRAM";
static const unsigned char CMD_PRG2[]  = "\2";
static const unsigned char CMD_QX[]    = "QX\6";
static const unsigned char CMD_ACK[]   = "\6";
static const unsigned char CMD_READ[]  = "Raaaan";
static const unsigned char CMD_WRITE[] = "Waaaan...";
static const unsigned char CMD_END[]   = "END";

#if defined(__WIN32__) || defined(WIN32)
    // No need for this function.
#else
//
// Encode the speed in bits per second into bit value
// accepted by cfsetspeed() function.
// Return -1 when speed is not supported.
//
static int baud_encode(int bps)
{
    // Linux: only a limited set of values permitted.
    switch (bps) {
#ifdef B75
    case 75: return B75;
#endif
#ifdef B110
    case 110: return B110;
#endif
#ifdef B134
    case 134: return B134;
#endif
#ifdef B150
    case 150: return B150;
#endif
#ifdef B200
    case 200: return B200;
#endif
#ifdef B300
    case 300: return B300;
#endif
#ifdef B600
    case 600: return B600;
#endif
#ifdef B1200
    case 1200: return B1200;
#endif
#ifdef B1800
    case 1800: return B1800;
#endif
#ifdef B2400
    case 2400: return B2400;
#endif
#ifdef B4800
    case 4800: return B4800;
#endif
#ifdef B9600
    case 9600: return B9600;
#endif
#ifdef B19200
    case 19200: return B19200;
#endif
#ifdef B38400
    case 38400: return B38400;
#endif
#ifdef B57600
    case 57600: return B57600;
#endif
#ifdef B115200
    case 115200: return B115200;
#endif
#ifdef B230400
    case 230400: return B230400;
#endif
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B500000
    case 500000: return B500000;
#endif
#ifdef B576000
    case 576000: return B576000;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
#ifdef B1000000
    case 1000000: return B1000000;
#endif
#ifdef B1152000
    case 1152000: return B1152000;
#endif
#ifdef B1500000
    case 1500000: return B1500000;
#endif
#ifdef B2000000
    case 2000000: return B2000000;
#endif
#ifdef B2500000
    case 2500000: return B2500000;
#endif
#ifdef B3000000
    case 3000000: return B3000000;
#endif
#ifdef B3500000
    case 3500000: return B3500000;
#endif
#ifdef B4000000
    case 4000000: return B4000000;
#endif
    }
    return -1;
}
#endif /* WIN32 */

//
// Send data to device.
// Return number of bytes, or -1 on error.
//
int serial_write(const unsigned char *data, int len)
{
#if defined(__WIN32__) || defined(WIN32)
    DWORD written;

    if (! WriteFile(fd, data, len, &written, 0))
        return -1;
    return written;
#else
    return write(fd, data, len);
#endif
}

//
// Receive data from device.
// Return number of bytes, or -1 on error.
//
int serial_read(unsigned char *data, int len, int timeout_msec)
{
#if defined(__WIN32__) || defined(WIN32)
    DWORD got;
    COMMTIMEOUTS ctmo;

    // Reset the Windows RX timeout to the current timeout_msec
    // value, as it may have changed since the last read.
    //
    memset(&ctmo, 0, sizeof(ctmo));
    ctmo.ReadIntervalTimeout = 0;
    ctmo.ReadTotalTimeoutMultiplier = 0;
    ctmo.ReadTotalTimeoutConstant = timeout_msec;
    if (! SetCommTimeouts(fd, &ctmo)) {
        fprintf(stderr, "Cannot set timeouts in serial_read()\n");
        return -1;
    }

    if (! ReadFile(fd, data, len, &got, 0)) {
        fprintf(stderr, "serial_read: read error\n");
        exit(-1);
    }
#else
    struct timeval timeout, to2;
    long got;
    fd_set rfds;

    timeout.tv_sec = timeout_msec / 1000;
    timeout.tv_usec = timeout_msec % 1000 * 1000;
again:
    to2 = timeout;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    got = select(fd + 1, &rfds, 0, 0, &to2);
    if (got < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            if (trace_flag > 0)
                printf("serial_read: retry on select\n");
            goto again;
        }
        fprintf(stderr, "serial_read: select error: %s\n", strerror(errno));
        exit(-1);
    }
#endif
    if (got == 0) {
        if (trace_flag > 0)
            printf("serial_read: no characters to read\n");
        return 0;
    }

#if ! defined(__WIN32__) && ! defined(WIN32)
    got = read(fd, data, (len > 1024) ? 1024 : len);
    if (got < 0) {
        fprintf(stderr, "serial_read: read error\n");
        exit(-1);
    }
#endif
    return got;
}

//
// Open the serial port.
// Return -1 on error.
//
int serial_open(const char *devname, int baud_rate)
{
#if defined(__WIN32__) || defined(WIN32)
    DCB new_mode;
#else
    struct termios new_mode;
#endif

#if defined(__WIN32__) || defined(WIN32)
    // Check for the Windows device syntax and bend a DOS device
    // into that syntax to allow higher COM numbers than 9
    //
    if (devname[0] != '\\') {
        // Prepend device prefix: COM23 -> \\.\COM23
        char *buf = alloca(5 + strlen(devname));
        if (! buf) {
            fprintf(stderr, "%s: Out of memory\n", devname);
            return -1;
        }
        strcpy(buf, "\\\\.\\");
        strcat(buf, devname);
        devname = buf;
    }

    // Open port
    fd = CreateFile(devname, GENERIC_READ | GENERIC_WRITE,
        0, 0, OPEN_EXISTING, 0, 0);
    if (fd == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "%s: Cannot open\n", devname);
        return -1;
    }

    // Set serial attributes
    memset(&saved_mode, 0, sizeof(saved_mode));
    if (! GetCommState(fd, &saved_mode)) {
        fprintf(stderr, "%s: Cannot get state\n", devname);
        return -1;
    }

    new_mode = saved_mode;

    new_mode.fDtrControl = DTR_CONTROL_ENABLE;
    new_mode.BaudRate = baud_rate;
    new_mode.ByteSize = 8;
    new_mode.StopBits = ONESTOPBIT;
    new_mode.Parity = 0;
    new_mode.fParity = FALSE;
    new_mode.fOutX = FALSE;
    new_mode.fInX = FALSE;
    new_mode.fOutxCtsFlow = FALSE;
    new_mode.fOutxDsrFlow = FALSE;
    new_mode.fRtsControl = RTS_CONTROL_ENABLE;
    new_mode.fNull = FALSE;
    new_mode.fAbortOnError = FALSE;
    new_mode.fBinary = TRUE;
    if (! SetCommState(fd, &new_mode)) {
        fprintf(stderr, "%s: Cannot set state\n", devname);
        return -1;
    }
#else
    // Encode baud rate.
    int baud_code = baud_encode(baud_rate);
    if (baud_code < 0) {
        fprintf(stderr, "%s: Bad baud rate %d\n", devname, baud_rate);
        return -1;
    }

    // Open port
    fd = open(devname, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror(devname);
        return -1;
    }

    // Set serial attributes
    memset(&saved_mode, 0, sizeof(saved_mode));
    tcgetattr(fd, &saved_mode);

    // 8n1, ignore parity
    memset(&new_mode, 0, sizeof(new_mode));
    new_mode.c_cflag = CS8 | CLOCAL | CREAD;
    new_mode.c_iflag = IGNBRK;
    new_mode.c_oflag = 0;
    new_mode.c_lflag = 0;
    new_mode.c_cc[VTIME] = 0;
    new_mode.c_cc[VMIN]  = 1;
    cfsetispeed(&new_mode, baud_code);
    cfsetospeed(&new_mode, baud_code);
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &new_mode);

    // Clear O_NONBLOCK flag.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
#endif
    return 0;
}

//
// Find a device path by vid/pid.
//
static char *find_path(int vid, int pid)
{
    char *result = 0;

#if defined(__linux__)
    // Create the udev object.
    struct udev *udev = udev_new();
    if (! udev) {
        printf("Can't create udev\n");
        return 0;
    }

    // Create a list of the devices in the 'tty' subsystem.
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);

    // For each item enumerated, figure out its information.
    struct udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
        // Get the filename of the /sys entry for the device
        // and create a udev_device object (dev) representing it.
        const char *syspath = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *comport = udev_device_new_from_syspath(udev, syspath);
        //printf("syspath = %s\n", syspath);

        // Get the parent device with the subsystem/devtype pair
        // of "usb"/"usb_device".
        struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(comport,
               "usb", "usb_device");
        if (! parent) {
            //printf("Unable to find parent usb device.\n");
            continue;
        }

        // Get the path to the device node in /dev.
        const char *devpath = udev_device_get_devnode(comport);
        //printf("devpath = %s\n", devpath);
        //printf("parent = %s\n", udev_device_get_devnode(parent));

        const char *idVendor  = udev_device_get_sysattr_value(parent, "idVendor");
        const char *idProduct = udev_device_get_sysattr_value(parent, "idProduct");
        if (! idVendor || ! idProduct) {
            // No vendor and product ID.
            continue;
        }
        //printf("vid = %s\n", idVendor);
        //printf("pid = %s\n", idProduct);

        unsigned vendor_id = strtoul(idVendor, 0, 16);
        unsigned product_id = strtoul(idProduct, 0, 16);
        if (vendor_id != vid || product_id != pid) {
            // Wrong ID.
            continue;
        }

        // Print names of vendor and product.
        //const char *vendor  = udev_device_get_sysattr_value(parent, "manufacturer");
        //const char *product = udev_device_get_sysattr_value(parent, "product");
        //printf("vendor = %s\n", vendor);
        //printf("product = %s\n", product);

        // Return result.
        udev_device_unref(parent);
        result = strdup(devpath);
        break;
    }

    // Free the enumerator object
    udev_enumerate_unref(enumerate);
    udev_unref(udev);

#elif defined(__APPLE__)
    // Create a list of the devices in the 'IOSerialBSDClient' class.
    CFMutableDictionaryRef dict = IOServiceMatching(kIOSerialBSDServiceValue);
    if (! dict) {
        printf("Cannot create IO Service dictionary.\n");
        return 0;
    }

    io_iterator_t devices = IO_OBJECT_NULL;
    kern_return_t ret = IOServiceGetMatchingServices(kIOMasterPortDefault,
        dict, &devices);
    if (ret != KERN_SUCCESS) {
        printf("Cannot find matching IO services.\n");
        return 0;
    }

    // For each matching device, print out its information.
    io_object_t device;
    while ((device = IOIteratorNext(devices)) != MACH_PORT_NULL) {
        // Get device path.
        char devname[1024];
        CFStringRef ref = (CFStringRef) IORegistryEntrySearchCFProperty(device,
            kIOServicePlane, CFSTR(kIOCalloutDeviceKey),
            kCFAllocatorDefault, kIORegistryIterateRecursively);
        if (! ref || ! CFStringGetCString(ref, devname, sizeof(devname), kCFStringEncodingUTF8)) {
            // Cannot get device path.
            continue;
        }
        //printf("%s\n", devname);

        // Get device parent.
        io_registry_entry_t parent = 0;
        if (KERN_SUCCESS != IORegistryEntryGetParentEntry(device, kIOServicePlane, &parent)) {
            //printf("Cannot get device parent.\n");
            continue;
        }

        // Get device grandparent.
        io_registry_entry_t grandparent = 0;
        if (KERN_SUCCESS != IORegistryEntryGetParentEntry(parent, kIOServicePlane, &grandparent)) {
            //printf("Cannot get device grandparent.\n");
            continue;
        }

        // Get vendor ID.
        int vendor_id;
        ref = IORegistryEntryCreateCFProperty(grandparent,
            CFSTR(kUSBVendorID), kCFAllocatorDefault, 0);
        if (! ref || ! CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &vendor_id)) {
            //printf("Cannot get vendor ID.\n");
            continue;
        }

        // Get product ID.
        int product_id;
        ref = IORegistryEntryCreateCFProperty(grandparent,
            CFSTR(kUSBProductID), kCFAllocatorDefault, 0);
        if (! ref || ! CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &product_id)) {
            //printf("Cannot get product ID.\n");
            continue;
        }

        if (vendor_id != vid || product_id != pid) {
            // Wrong ID.
            continue;
        }

        result = strdup(devname);
        break;
    }

    // Free the iterator object
    IOObjectRelease(devices);

#else
    printf("Don't know how to get the list of CD devices on this system.\n");
#endif

    return result;
}

//
// Connect to the specified device.
// Initiate the programming session.
//
int serial_init(int vid, int pid)
{
    dev_path = find_path(vid, pid);
    if (!dev_path) {
        if (trace_flag) {
            fprintf(stderr, "Cannot find USB device %04x:%04x\n",
                vid, pid);
        }
        return -1;
    }

    // Succeeded.
    printf("Serial port: %s\n", dev_path);
    return 0;
}

//
// Send the command sequence and get back a response.
//
static int send_recv(const unsigned char *cmd, int cmdlen,
    unsigned char *response, int reply_len)
{
    unsigned char *p;
    int len, i, got;

    //
    // Send command.
    //
    if (trace_flag > 0) {
        fprintf(stderr, "----Send [%d] %x", cmdlen, cmd[0]);
        for (i=1; i<cmdlen; ++i)
            fprintf(stderr, "-%x", cmd[i]);
        fprintf(stderr, "\n");
    }

    if (serial_write(cmd, cmdlen) < 0) {
        fprintf(stderr, "%s: write error\n", dev_path);
        exit(-1);
    }

    //
    // Get response.
    //
    p = response;
    len = 0;
    while (len < reply_len) {
        got = serial_read(p, reply_len - len, 1000);
        if (! got)
            return 0;

        p += got;
        len += got;
    }

    if (trace_flag > 0) {
        fprintf(stderr, "----Recv [%d] %x", reply_len, response[0]);
        for (i=1; i<reply_len; ++i)
            fprintf(stderr, "-%x", response[i]);
        fprintf(stderr, "\n");
    }
    return 1;
}

//
// Close the serial port.
//
void serial_close()
{
#if defined(__WIN32__) || defined(WIN32)
    if (fd != INVALID_HANDLE_VALUE) {
        SetCommState(fd, &saved_mode);
        CloseHandle(fd);
        fd = INVALID_HANDLE_VALUE;
    }
#else
    if (fd >= 0) {
        unsigned char ack[1];

        send_recv(CMD_END, 3, ack, 1);

        tcsetattr(fd, TCSANOW, &saved_mode);
        close(fd);
        fd = -1;
    }
#endif
}

//
// Query and return the device identification string.
// On error, return NULL.
//
const char *serial_identify()
{
    static unsigned char reply[16];
    unsigned char ack[3];
    int retry = 0;

    if (serial_open(dev_path, 115200) < 0) {
        return 0;
    }

again:
#if defined(__WIN32__) || defined(WIN32)
    //TODO: flush pending input and output buffers.
#else
    tcflush(fd, TCIOFLUSH);
#endif
    send_recv(CMD_PRG, 7, ack, 3);
    if (memcmp(ack, CMD_QX, 3) != 0) {
        if (++retry >= 10) {
            fprintf(stderr, "%s: Wrong PRG acknowledge %02x-%02x-%02x, expected %02x-%02x-%02x\n",
                __func__, ack[0], ack[1], ack[2], CMD_QX[0], CMD_QX[1], CMD_QX[2]);
            return 0;
        }
        usleep(500000);
        goto again;
    }

    // Reply:
    // 49 44 38 36 38 55 56 45 00 56 31 30 32 00 00 06
    //  I  D  8  6  8  U  V  E     V  1  0  2
    send_recv(CMD_PRG2, 1, reply, 16);
    if (reply[0] != 'I' || reply[15] != CMD_ACK[0]) {
        if (++retry >= 10) {
            fprintf(stderr, "%s: Wrong PRG2 reply %02x-...-%02x, expected %02x-...-%02x\n",
                __func__, reply[0], reply[15], 'I', CMD_ACK[0]);
            return 0;
        }
        usleep(500000);
        goto again;
    }

    // Terminate the string.
    reply[8] = 0;
    return (char*)&reply[1];
}

void serial_read_region(int addr, unsigned char *data, int nbytes)
{
    static const int DATASZ = 64;
    unsigned char cmd[6], reply[8 + DATASZ];
    int n, i, retry = 0;

    for (n=0; n<nbytes; n+=DATASZ) {
        // Read command: 52 aa aa aa aa 10
        cmd[0] = CMD_READ[0];
        cmd[1] = (addr + n) >> 24;
        cmd[2] = (addr + n) >> 16;
        cmd[3] = (addr + n) >> 8;
        cmd[4] = addr + n;
        cmd[5] = DATASZ;
again:
        send_recv(cmd, 6, reply, sizeof(reply));
        if (reply[0] != CMD_WRITE[0] || reply[7+DATASZ] != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong read reply %02x-...-%02x, expected %02x-...-%02x\n",
                __func__, reply[0], reply[7+DATASZ], CMD_WRITE[0], CMD_ACK[0]);
            exit(-1);
        }

        // Compute checksum.
        unsigned char sum = reply[1];
        for (i=2; i<6+DATASZ; i++)
            sum += reply[i];
        if (reply[6+DATASZ] != sum) {
            fprintf(stderr, "%s: Wrong read checksum %02x, expected %02x\n",
                __func__, sum, reply[6+DATASZ]);
            if (retry++ < 3)
                goto again;
            exit(-1);
        }

        memcpy(data + n, reply + 6, DATASZ);
    }
}

void serial_write_region(int addr, unsigned char *data, int nbytes)
{
    //static const int DATASZ = 64;
    static const int DATASZ = 16;
    unsigned char ack, cmd[8 + DATASZ];
    int n, i;

    for (n=0; n<nbytes; n+=DATASZ) {
        // Write command: 57 aa aa aa aa 10 .. .. ss nn
        cmd[0] = CMD_WRITE[0];
        cmd[1] = (addr + n) >> 24;
        cmd[2] = (addr + n) >> 16;
        cmd[3] = (addr + n) >> 8;
        cmd[4] = addr + n;
        cmd[5] = DATASZ;
        memcpy(cmd + 6, data + n, DATASZ);

        // Compute checksum.
        unsigned char sum = cmd[1];
        for (i=2; i<6+DATASZ; i++)
            sum += cmd[i];

        cmd[6 + DATASZ] = sum;
        cmd[7 + DATASZ] = CMD_ACK[0];

        send_recv(cmd, 8 + DATASZ, &ack, 1);
        if (ack != CMD_ACK[0]) {
            fprintf(stderr, "%s: Wrong acknowledge %#x, expected %#x\n",
                __func__, ack, CMD_ACK[0]);
            exit(-1);
        }
    }
}
