/*
 * Interface to Baofeng DM-32UV (experimental).
 *
 * The read implementation follows the protocol and memory layout documented in
 * dm32_reference/read_connection.md and dm32_reference/codeplug_format.md. It
 * performs the OEM CPS handshake, downloads the mapped regions, and presents a
 * consolidated view of the strings and channel slots we currently understand.
 *
 * Write support is intentionally omitted until the erase/program semantics are
 * verified on hardware.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include "radio.h"
#include "util.h"

#define DM32_BAUD               115200

#define MEMSZ                   0x200000u
#define DM32_PAGE_SIZE          0x1000u
#define DM32_PAGE_MASK          (~(DM32_PAGE_SIZE - 1u))

#define OFFSET_FW_VERSION       0x00000030u
#define LEN_FW_VERSION          32u

#define OFFSET_BOOT_LINE1       0x00005001u
#define OFFSET_BOOT_LINE2       0x0000500Fu
#define LEN_BOOT_LINE           16u

/*
 * Block metadata types. The DM-32UV stores a 1-byte type marker at
 * (block_base + 0xFFF) for every 4 KiB block in the main config region.
 * Discovery reads that byte per block to locate data dynamically.
 * Values per infamy/DM32-Protocol-Spec (MIT).
 */
#define DM32_META_EMPTY         0x00u
#define DM32_META_INVALID       0xFFu
#define DM32_META_SETTINGS      0x04u   /* Radio Settings (language, etc.) */
#define DM32_META_MSG           0x0Au   /* Quick text messages            */
#define DM32_META_RXGROUP       0x0Fu   /* RX group list (109-byte entries)*/
#define DM32_META_SCANLIST      0x11u   /* Scan lists (57-byte entries)   */
#define DM32_META_CHAN_FIRST    0x12u   /* Channel block 0 (has header)   */
#define DM32_META_CHAN_LAST     0x41u   /* Channel block 47               */
#define DM32_META_TXCONTACT_LO  0x42u   /* TX contact map, channels 1-2048*/
#define DM32_META_TXCONTACT_HI  0x43u   /* TX contact map, channels 2049+ */
#define DM32_META_TG_FIRST      0x44u   /* Talkgroup block 0              */
#define DM32_META_TG_LAST       0x48u   /* Talkgroup block 4              */
#define DM32_META_ZONE          0x5Cu   /* Zones (145-byte entries)       */
#define DM32_META_RADIOID       0x67u   /* Radio's own DMR ID list        */

/* Main config block bounds (fallback if V-frame 0x0A is unavailable). */
#define DM32_MAIN_START         0x001000u
#define DM32_MAIN_END           0x0C8FFFu
#define DM32_META_OFFSET        0x0FFFu  /* type byte within a 4 KiB block */
#define DM32_MAX_CHAN_BLOCKS    48u

/* Channel record (48 bytes, little-endian; spec 05-DATA-STRUCTURES). */
#define DM32_CHANNEL_SIZE       0x30u
#define DM32_CHANNEL_HDR        0x10u    /* header size in first block      */
#define LEN_CHANNEL_NAME        16u
#define MAX_CHANNELS            4000u

/* Zone record (145 bytes, metadata 0x5C). */
#define DM32_ZONE_SIZE          145u
#define DM32_ZONE_HDR           16u
#define LEN_ZONE_NAME           11u

/* Scan list record (57 bytes, metadata 0x11). */
#define DM32_SCANLIST_SIZE      57u
#define LEN_SCANLIST_NAME       11u

/* RX group list record (109 bytes, metadata 0x0F). */
/* Block layout (NeonPlug parsers validated on live hw 2026-07-14):       */
/*   0x00-0x03 bitmask u32 LE (bit i=1 → group i+1 active)               */
/*   0x04-0x0F reserved; 0x10 flag (0x01)                                 */
/*   entries at 0x11, each 109 bytes:                                     */
/*     +0x00 name[11]; +0x0B 32 × 3-byte LE DMR IDs (0x00..=sentinel)    */
#define DM32_RXGROUP_ENTRY_SIZE  109u
#define DM32_RXGROUP_HDR         0x11u
#define DM32_RXGROUP_NAME_LEN    11u
#define DM32_RXGROUP_CONTACT_OFF 0x0Bu
#define DM32_RXGROUP_MAX_CONTACTS 32u
#define DM32_RXGROUP_MAX_GROUPS   32u

/* Radio ID record (16 bytes, metadata 0x67). */
#define DM32_RADIOID_SIZE       16u
#define LEN_RADIOID_NAME        12u

typedef struct __attribute__((packed)) {
    uint8_t  name[16];       /* 0x00 ASCII, null-term, 0xFF pad           */
    uint8_t  rx_bcd[4];      /* 0x10 RX freq, BCD little-endian           */
    uint8_t  tx_bcd[4];      /* 0x14 TX freq, BCD little-endian           */
    uint8_t  mode_flags;     /* 0x18 mode / forbid-TX / power / lone      */
    uint8_t  scan_bw;        /* 0x19 bandwidth / scan-add / scanlist id   */
    uint8_t  talkaround;     /* 0x1A talkaround / aprs / reverse          */
    uint8_t  emergency;      /* 0x1B emergency                            */
    uint8_t  squelch_aprs;   /* 0x1C squelch (7-4) / aprs mode            */
    uint8_t  analog;         /* 0x1D vox / scramble / compander / talkback*/
    uint8_t  reserved_1e;    /* 0x1E                                      */
    uint8_t  ptt_id;         /* 0x1F ptt id display / value               */
    uint8_t  color_code;     /* 0x20 DMR color code 0-15                  */
    uint8_t  rx_tone[2];     /* 0x21 RX CTCSS/DCS                         */
    uint8_t  tx_tone[2];     /* 0x23 TX CTCSS/DCS                         */
    uint8_t  extra[6];       /* 0x25..0x2A misc flags                     */
    uint8_t  radio_id_index; /* 0x2B index into radio-ID list, 0xFF none  */
    uint8_t  reserved_2c[4]; /* 0x2C padding                              */
} channel_raw_t;

/* Channel mode_flags (byte 0x18). */
#define DM32_MODE_MASK          0xF0u
#define DM32_MODE_SHIFT         4
#define DM32_MODE_ANALOG        0u
#define DM32_MODE_DIGITAL       1u
#define DM32_FORBID_TX_BIT      0x08u
#define DM32_POWER_MASK         0x06u
#define DM32_POWER_SHIFT        1
#define DM32_LONE_BIT           0x01u
/* Channel scan_bw (byte 0x19). */
#define DM32_BW_WIDE_BIT        0x80u
#define DM32_SCAN_ADD_BIT       0x40u
#define DM32_SCANLIST_MASK      0x3Cu
#define DM32_SCANLIST_SHIFT     2
/* Channel byte 0x1D on a digital channel: bits 3-0 color code, bit 4 time slot. */
#define DM32_DIG_CC_MASK        0x0Fu
#define DM32_DIG_TS_BIT         0x10u

typedef struct {
    uint8_t  id;
    uint32_t start;      /* first byte of segment (little-endian u32) */
    uint32_t end;        /* last byte of segment  (little-endian u32) */
} dm32_vseg_t;

static char dm32_board_id[32];
static char dm32_fw_version_cached[32];
static char dm32_build_date[32];
static dm32_vseg_t dm32_vsegs[16];
static unsigned dm32_nvsegs;

/* Discovered main-config bounds and per-type block addresses. */
static uint32_t dm32_main_start = DM32_MAIN_START;
static uint32_t dm32_main_end   = DM32_MAIN_END;
static uint32_t dm32_chan_blocks[DM32_MAX_CHAN_BLOCKS];
static unsigned dm32_chan_nblocks;
static uint32_t dm32_zone_block;      /* 0 = not found */
static uint32_t dm32_scan_block;
static uint32_t dm32_rxgroup_block;
static uint32_t dm32_radioid_block;
static uint32_t dm32_msg_block;
static uint32_t dm32_tg_block;        /* talkgroups (metadata 0x44)        */
static uint32_t dm32_txc_lo_block;    /* TX contact, channels 1-2048 (0x42)*/
static uint32_t dm32_txc_hi_block;    /* TX contact, channels 2049+ (0x43) */
static uint32_t dm32_settings_block;  /* 0x04 - Radio Settings block (language, etc.) */

#define DM32_MAX_TG 256
typedef struct {
    char     name[17];
    uint32_t number;
    uint8_t  call_type;
} dm32_tg_t;
static dm32_tg_t dm32_tgs[DM32_MAX_TG];
static unsigned  dm32_ntgs;
static int       dm32_tgs_parsed;

static uint32_t dm32_read_addrs[256];
static unsigned dm32_read_addrs_n;
static unsigned dm32_written_max;

static inline uint32_t dm32_page_base(uint32_t addr)
{
    return addr & DM32_PAGE_MASK;
}

static int dm32_is_ascii(unsigned char c)
{
    return c >= 32 && c <= 126;
}

static void dm32_mark_read(uint32_t addr)
{
    for (unsigned i = 0; i < dm32_read_addrs_n; ++i) {
        if (dm32_read_addrs[i] == addr) {
            return;
        }
    }
    if (dm32_read_addrs_n < (sizeof(dm32_read_addrs) / sizeof(dm32_read_addrs[0]))) {
        dm32_read_addrs[dm32_read_addrs_n++] = addr;
    }
}

static void dm32_trim_right(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static size_t dm32_copy_ascii(char *dst, size_t dstlen, const unsigned char *src, size_t maxlen)
{
    size_t out = 0;
    if (!dst || dstlen == 0) {
        return 0;
    }
    while (out + 1 < dstlen && out < maxlen) {
        unsigned char c = src[out];
        if (c == 0x00 || c == 0xFF) {
            break;
        }
        if (!dm32_is_ascii(c)) {
            break;
        }
        dst[out++] = (char)c;
    }
    dst[out] = '\0';
    dm32_trim_right(dst);
    return out;
}

static double dm32_bcd_mhz(const uint8_t *p)
{
    /*
     * Frequencies are 4-byte BCD stored little-endian. Reverse the bytes
     * to big-endian, then read the eight BCD nibbles most-significant
     * first. e.g. stored 00 50 53 14 -> 14 53 50 00 -> 14535000 -> 145.35.
     */
    unsigned long long digits = 0;
    for (int i = 3; i >= 0; --i) {
        digits = digits * 10 + ((p[i] >> 4) & 0xF);
        digits = digits * 10 + (p[i] & 0xF);
    }
    return digits / 100000.0;
}

static const unsigned char *dm32_mem_ptr(uint32_t offset, size_t len)
{
    if (offset + len > MEMSZ) {
        return NULL;
    }
    return &radio_mem[offset];
}

static void dm32_send_ascii(const char *s)
{
    if (trace_flag) {
        fprintf(stderr, "DM32: send '%s'\n", s);
    }
    serial_write((const unsigned char *)s, (int)strlen(s));
}

static int dm32_read_exact(unsigned char *buf, int n, int timeout_msec)
{
    int got = 0;
    while (got < n) {
        int r = serial_read(buf + got, n - got, timeout_msec);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    return got;
}

static int dm32_collect_reads(unsigned char *out, int maxlen, int msec)
{
    int total = 0;
    int iters = msec / 50;
    if (iters < 1) {
        iters = 1;
    }
    while (iters-- > 0) {
        unsigned char buf[256];
        int n = serial_read(buf, sizeof(buf), 50);
        if (n > 0) {
            if (out && total < maxlen) {
                int cp = n;
                if (total + cp > maxlen) {
                    cp = maxlen - total;
                }
                memcpy(out + total, buf, cp);
            }
            total += n;
        }
    }
    return total;
}

static void dm32_extract_board_id(const unsigned char *buf, int n)
{
    int best_start = -1;
    int best_len = 0;
    for (int i = 0; i < n;) {
        if (!dm32_is_ascii(buf[i])) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && dm32_is_ascii(buf[j])) {
            ++j;
        }
        int len = j - i;
        if (len >= 5 && len > best_len) {
            best_start = i;
            best_len = len;
        }
        i = j;
    }
    if (best_start >= 0) {
        int copy = best_len;
        if (copy > (int)sizeof(dm32_board_id) - 1) {
            copy = (int)sizeof(dm32_board_id) - 1;
        }
        memcpy(dm32_board_id, buf + best_start, copy);
        dm32_board_id[copy] = '\0';
    }
}

static int dm32_read_header_sync(unsigned char hdr[6], int timeout_msec)
{
    unsigned char b;
    int waited = 0;
    while (waited < timeout_msec) {
        int r = dm32_read_exact(&b, 1, 150);
        if (r <= 0) {
            waited += 200;
            continue;
        }
        if (b != 0x57) {
            continue;
        }
        hdr[0] = b;
        if (dm32_read_exact(hdr + 1, 5, 2000) != 5) {
            return -1;
        }
        return 0;
    }
    return -1;
}

static int dm32_read_block(uint32_t addr24, uint16_t len)
{
    unsigned char cmd[6];
    unsigned char hdr[6];

    int priming = (addr24 >= 0xFF0000u);
    if (!priming && (uint64_t)addr24 + len > MEMSZ) {
        return -1;
    }

    cmd[0] = 0x52;
    cmd[1] = addr24 & 0xFF;          /* address is 24-bit little-endian */
    cmd[2] = (addr24 >> 8) & 0xFF;
    cmd[3] = (addr24 >> 16) & 0xFF;
    cmd[4] = len & 0xFF;
    cmd[5] = (len >> 8) & 0xFF;

    if (trace_flag) {
        fprintf(stderr, "DM32: R %02X %02X %02X %02X %02X\n",
                cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
    }
    if (serial_write(cmd, 6) < 0) {
        return -1;
    }

    if (dm32_read_header_sync(hdr, 4000) != 0) {
        return -1;
    }
    if (hdr[0] != 0x57 || hdr[1] != cmd[1] || hdr[2] != cmd[2] ||
        hdr[3] != cmd[3] || hdr[4] != cmd[4] || hdr[5] != cmd[5]) {
        if (trace_flag) {
            fprintf(stderr, "DM32: unexpected header\n");
            print_hex(hdr, 6);
        }
        return -1;
    }

    unsigned toread = len;
    unsigned offset = 0;
    while (toread > 0) {
        unsigned char buf[512];
        int chunk = (toread > sizeof(buf)) ? (int)sizeof(buf) : (int)toread;
        int r = dm32_read_exact(buf, chunk, 2000);
        if (r <= 0) {
            return -1;
        }
        if (!priming) {
            memcpy(radio_mem + addr24 + offset, buf, r);
            offset += r;
            toread -= r;
            if (addr24 + offset > dm32_written_max) {
                dm32_written_max = addr24 + offset;
            }
        } else {
            offset += r;
            toread -= r;
        }
    }

    if (!priming) {
        dm32_mark_read(dm32_page_base(addr24));
        double pct = (dm32_written_max / (double)MEMSZ) * 100.0;
        if (pct > 100.0) {
            pct = 100.0;
        }
        radio_progress = (int)pct;
    }
    return 0;
}

static int dm32_read_block_retry(uint32_t addr24, uint16_t len, int attempts)
{
    for (int i = 0; i < attempts; ++i) {
        if (dm32_read_block(addr24, len) == 0) {
            return 0;
        }
        usleep(50000);
    }
    return -1;
}

/*
 * Drain any pending input so a probe reply cannot be confused with a stale or
 * lagging reply left over from a previous request.
 */
static void dm32_drain_input(void)
{
    unsigned char sink[256];
    while (serial_read(sink, (int)sizeof(sink), 0) > 0) {
        /* discard */
    }
}

/*
 * Read a single byte at addr24 without storing it into radio_mem. Used by the
 * write path to probe the radio's current block layout while leaving the image
 * that is staged in radio_mem untouched. Returns the byte, or -1 on error.
 *
 * The reply header (0x57 + addr(3 LE) + len(2 LE)) is validated to echo the
 * exact address and length we requested. A stale or mismatched reply is
 * resynchronised by consuming its payload, so its data bytes cannot be misread
 * as the next reply. Without this, rapid sequential probing could attribute a
 * marker to the wrong address and relocate blocks on write.
 */
static int dm32_read_byte_raw(uint32_t addr24)
{
    unsigned char cmd[6], hdr[6], val;

    cmd[0] = 0x52;
    cmd[1] = addr24 & 0xFF;          /* little-endian address */
    cmd[2] = (addr24 >> 8) & 0xFF;
    cmd[3] = (addr24 >> 16) & 0xFF;
    cmd[4] = 0x01;
    cmd[5] = 0x00;

    for (int attempt = 0; attempt < 3; ++attempt) {
        dm32_drain_input();
        if (serial_write(cmd, 6) < 0) {
            return -1;
        }

        for (int scan = 0; scan < 8; ++scan) {
            if (dm32_read_header_sync(hdr, 1000) != 0) {
                break;                   /* nothing came back; resend */
            }
            unsigned rlen = (unsigned)hdr[4] | ((unsigned)hdr[5] << 8);
            if (hdr[1] == cmd[1] && hdr[2] == cmd[2] && hdr[3] == cmd[3] &&
                hdr[4] == cmd[4] && hdr[5] == cmd[5]) {
                if (dm32_read_exact(&val, 1, 2000) != 1) {
                    return -1;
                }
                return val;
            }
            /* Wrong reply: consume its echoed payload length to resynchronise. */
            while (rlen > 0) {
                unsigned char sink[512];
                int chunk = rlen > sizeof(sink) ? (int)sizeof(sink) : (int)rlen;
                int got = dm32_read_exact(sink, chunk, 2000);
                if (got <= 0) {
                    break;
                }
                rlen -= (unsigned)got;
            }
        }
    }
    return -1;
}

/*
 * Write a 4 KiB block: 0x57 + addr(3, little-endian) + len(0x1000, little-
 * endian) + 4096 data bytes. The last data byte (offset 0xFFF) is the block's
 * type marker. The radio replies with a single 0x06 ACK.
 */
static int dm32_write_block(uint32_t addr24, const unsigned char *data)
{
    unsigned char cmd[6 + DM32_PAGE_SIZE];
    unsigned char ack = 0;

    cmd[0] = 0x57;
    cmd[1] = addr24 & 0xFF;
    cmd[2] = (addr24 >> 8) & 0xFF;
    cmd[3] = (addr24 >> 16) & 0xFF;
    cmd[4] = 0x00;
    cmd[5] = 0x10;                   /* length 0x1000 (4096) */
    memcpy(cmd + 6, data, DM32_PAGE_SIZE);

    if (trace_flag) {
        fprintf(stderr, "DM32: W 0x%06X len 4096\n", addr24);
    }
    if (serial_write(cmd, (int)sizeof(cmd)) < 0) {
        return -1;
    }
    if (dm32_read_exact(&ack, 1, 5000) != 1 || ack != 0x06) {
        return -1;
    }
    return 0;
}

static int dm32_read_v_frame(uint8_t *type, uint8_t *length, unsigned char *payload, int maxlen, int timeout_msec)
{
    unsigned char b;
    int waited = 0;
    while (waited < timeout_msec) {
        int r = serial_read(&b, 1, 50);
        if (r <= 0) {
            waited += 50;
            continue;
        }
        if (b != 0x56) {
            continue;
        }
        unsigned char hdr[2];
        if (dm32_read_exact(hdr, 2, 200) != 2) {
            return -1;
        }
        uint8_t t = hdr[0];
        uint8_t len = hdr[1];
        if (len > maxlen) {
            unsigned char tmp[256];
            int remain = len;
            while (remain > 0) {
                int chunk = (remain > (int)sizeof(tmp)) ? (int)sizeof(tmp) : remain;
                int got = dm32_read_exact(tmp, chunk, 200);
                if (got <= 0) {
                    break;
                }
                remain -= got;
            }
            return -1;
        }
        if (dm32_read_exact(payload, len, 500) != len) {
            return -1;
        }
        if (type) {
            *type = t;
        }
        if (length) {
            *length = len;
        }
        return 0;
    }
    return -1;
}

static void dm32_parse_v_reply(uint8_t type, uint8_t len, const unsigned char *pl)
{
    if (type == 0x01) {
        size_t copy = len < sizeof(dm32_fw_version_cached) - 1 ? len : sizeof(dm32_fw_version_cached) - 1;
        memcpy(dm32_fw_version_cached, pl, copy);
        dm32_fw_version_cached[copy] = '\0';
    } else if (type == 0x03) {
        size_t copy = len < sizeof(dm32_build_date) - 1 ? len : sizeof(dm32_build_date) - 1;
        memcpy(dm32_build_date, pl, copy);
        dm32_build_date[copy] = '\0';
    } else if (type == 0x06 || type == 0x07 || type == 0x08 ||
               type == 0x09 || type == 0x0A || type == 0x0E || type == 0x0F) {
        if (len == 8) {
            /* 8-byte pointer = start(u32 LE) + end(u32 LE). */
            uint32_t start = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8) |
                             ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
            uint32_t end   = (uint32_t)pl[4] | ((uint32_t)pl[5] << 8) |
                             ((uint32_t)pl[6] << 16) | ((uint32_t)pl[7] << 24);
            if (dm32_nvsegs < (sizeof(dm32_vsegs) / sizeof(dm32_vsegs[0]))) {
                dm32_vsegs[dm32_nvsegs].id = type;
                dm32_vsegs[dm32_nvsegs].start = start;
                dm32_vsegs[dm32_nvsegs].end = end;
                ++dm32_nvsegs;
            }
            if (type == 0x0A && end > start) {
                dm32_main_start = start;
                dm32_main_end = end;
            }
        }
    }
}

static void dm32_collect_handshake_info(void)
{
    unsigned char buf[512];

    dm32_send_ascii("PSEARCH");
    int n = dm32_collect_reads(buf, sizeof(buf), 200);
    dm32_extract_board_id(buf, n);

    dm32_send_ascii("PASSSTA");
    n = dm32_collect_reads(buf, sizeof(buf), 200);
    if (!dm32_board_id[0]) {
        dm32_extract_board_id(buf, n);
    }

    dm32_send_ascii("SYSINFO");
    n = dm32_collect_reads(buf, sizeof(buf), 250);
    if (!dm32_board_id[0]) {
        dm32_extract_board_id(buf, n);
    }

    unsigned char request[5] = { 0x56, 0x00, 0x00, 0x40, 0x0D };
    serial_write(request, 5);
    uint8_t t, l;
    unsigned char payload[256];
    if (dm32_read_v_frame(&t, &l, payload, sizeof(payload), 800) == 0) {
        dm32_parse_v_reply(t, l, payload);
    }

    for (int i = 1; i <= 16; ++i) {
        if (i == 12) {
            continue;
        }
        unsigned char req[5] = { 0x56, 0x00, 0x00, 0x00, (unsigned char)i };
        serial_write(req, 5);
        if (dm32_read_v_frame(&t, &l, payload, sizeof(payload), 800) == 0) {
            dm32_parse_v_reply(t, l, payload);
        }
    }
}

static void dm32_enter_program_mode(void)
{
    static const unsigned char preamble[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0C,
                                              'P',  'R',  'O',  'G',  'R',  'A',  'M' };
    serial_write(preamble, sizeof(preamble));
    usleep(30000);

    static const unsigned char b02[] = { 0x02 };
    static const unsigned char b06[] = { 0x06 };
    serial_write(b02, sizeof(b02));
    dm32_collect_reads(NULL, 0, 80);
    serial_write(b06, sizeof(b06));
    dm32_collect_reads(NULL, 0, 120);
}

/* Read the 1-byte type marker at (block_addr + 0xFFF); returns -1 on error. */
static int dm32_read_meta(uint32_t block_addr)
{
    uint32_t a = block_addr + DM32_META_OFFSET;
    if (a + 1 > MEMSZ) {
        return -1;
    }
    if (dm32_read_block_retry(a, 1, 2) != 0) {
        return -1;
    }
    return radio_mem[a];
}

/*
 * Classify every 4 KiB block in the main config region by the type marker
 * stored at (block + 0xFFF) in radio_mem, building the dynamic layout map.
 * Channel blocks are ordered by their metadata value (0x12..0x41), since
 * metadata order is the logical channel order, independent of address order.
 * Works both after a live download and on an image already loaded into memory.
 */
static void dm32_classify(void)
{
    dm32_chan_nblocks = 0;
    dm32_zone_block = 0;
    dm32_scan_block = 0;
    dm32_rxgroup_block = 0;
    dm32_radioid_block = 0;
    dm32_msg_block = 0;
    dm32_tg_block = 0;
    dm32_txc_lo_block = 0;
    dm32_txc_hi_block = 0;
    dm32_settings_block = 0;
    dm32_ntgs = 0;
    dm32_tgs_parsed = 0;

    uint32_t start = dm32_page_base(dm32_main_start);
    uint32_t end = dm32_main_end;

    for (uint32_t addr = start;
         addr <= end && addr + DM32_PAGE_SIZE <= MEMSZ;
         addr += DM32_PAGE_SIZE) {
        uint8_t type = radio_mem[addr + DM32_META_OFFSET];
        if (type == DM32_META_EMPTY || type == DM32_META_INVALID) {
            continue;
        }
        if (trace_flag) {
            fprintf(stderr, "DM32: block 0x%06X type 0x%02X\n", addr, type);
        }

        if (type >= DM32_META_CHAN_FIRST && type <= DM32_META_CHAN_LAST) {
            if (dm32_chan_nblocks < DM32_MAX_CHAN_BLOCKS) {
                /* insertion sort by stored metadata byte */
                unsigned pos = dm32_chan_nblocks;
                while (pos > 0 &&
                       radio_mem[dm32_chan_blocks[pos - 1] + DM32_META_OFFSET] > type) {
                    dm32_chan_blocks[pos] = dm32_chan_blocks[pos - 1];
                    --pos;
                }
                dm32_chan_blocks[pos] = addr;
                ++dm32_chan_nblocks;
            }
        } else if (type == DM32_META_ZONE && !dm32_zone_block) {
            dm32_zone_block = addr;
        } else if (type == DM32_META_SCANLIST && !dm32_scan_block) {
            dm32_scan_block = addr;
        } else if (type == DM32_META_RXGROUP && !dm32_rxgroup_block) {
            dm32_rxgroup_block = addr;
        } else if (type == DM32_META_RADIOID && !dm32_radioid_block) {
            dm32_radioid_block = addr;
        } else if (type == DM32_META_MSG && !dm32_msg_block) {
            dm32_msg_block = addr;
        } else if (type == DM32_META_SETTINGS && !dm32_settings_block) {
            dm32_settings_block = addr;
        } else if (type == DM32_META_TG_FIRST && !dm32_tg_block) {
            dm32_tg_block = addr;
        } else if (type == DM32_META_TXCONTACT_LO && !dm32_txc_lo_block) {
            dm32_txc_lo_block = addr;
        } else if (type == DM32_META_TXCONTACT_HI && !dm32_txc_hi_block) {
            dm32_txc_hi_block = addr;
        }
    }
}

/*
 * Live discovery: probe the type marker of every block in the main config
 * region (bounds from V-frame 0x0A) over the serial link, then classify.
 */
static void dm32_discover(void)
{
    uint32_t start = dm32_page_base(dm32_main_start);
    uint32_t end = dm32_main_end;

    for (uint32_t addr = start;
         addr <= end && addr + DM32_PAGE_SIZE <= MEMSZ;
         addr += DM32_PAGE_SIZE) {
        dm32_read_meta(addr);
    }
    dm32_classify();
}

/* Fetch full 4 KiB payloads for every block located during discovery. */
static void dm32_read_discovered(void)
{
    for (unsigned i = 0; i < dm32_chan_nblocks; ++i) {
        dm32_read_block_retry(dm32_chan_blocks[i], DM32_PAGE_SIZE, 2);
    }
    if (dm32_zone_block) {
        dm32_read_block_retry(dm32_zone_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_scan_block) {
        dm32_read_block_retry(dm32_scan_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_rxgroup_block) {
        dm32_read_block_retry(dm32_rxgroup_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_radioid_block) {
        dm32_read_block_retry(dm32_radioid_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_msg_block) {
        dm32_read_block_retry(dm32_msg_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_tg_block) {
        dm32_read_block_retry(dm32_tg_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_txc_lo_block) {
        dm32_read_block_retry(dm32_txc_lo_block, DM32_PAGE_SIZE, 2);
    }
    if (dm32_txc_hi_block) {
        dm32_read_block_retry(dm32_txc_hi_block, DM32_PAGE_SIZE, 2);
    }
}

static int dm32_channel_blank(const channel_raw_t *ch)
{
    const unsigned char *raw = (const unsigned char *)ch;
    unsigned ff = 0;
    unsigned zz = 0;
    for (unsigned i = 0; i < sizeof(*ch); ++i) {
        if (raw[i] == 0xFF) {
            ++ff;
        }
        if (raw[i] == 0x00) {
            ++zz;
        }
    }
    if (ff == sizeof(*ch) || zz == sizeof(*ch)) {
        return 1;
    }
    return raw[0] == 0x00 || raw[0] == 0xFF;
}

/*
 * Parse the talkgroup list (metadata block 0x44). Entry 1 begins with a
 * 1-byte header (0x00); every entry is then flag(1) + name(16) + null(1) +
 * number(3 LE) + call_type(1) + pad(2) = 24 bytes.
 */
static void dm32_parse_talkgroups(void)
{
    dm32_ntgs = 0;
    dm32_tgs_parsed = 1;

    const unsigned char *b = dm32_tg_block
        ? dm32_mem_ptr(dm32_tg_block, DM32_PAGE_SIZE) : NULL;
    if (!b) {
        return;
    }

    unsigned off = 1;   /* skip entry-1 header byte */
    while (dm32_ntgs < DM32_MAX_TG) {
        unsigned name_off = off + 1;    /* skip flag byte */
        if (name_off + 23 > DM32_PAGE_SIZE) {
            break;
        }
        if (b[name_off] == 0x00 || b[name_off] == 0xFF) {
            break;      /* end sentinel */
        }
        dm32_tg_t *tg = &dm32_tgs[dm32_ntgs];
        dm32_copy_ascii(tg->name, sizeof(tg->name), b + name_off, 16);
        const unsigned char *c = b + name_off + 16 + 1;   /* after name+null */
        tg->number = c[0] | ((uint32_t)c[1] << 8) | ((uint32_t)c[2] << 16);
        tg->call_type = c[3];
        ++dm32_ntgs;
        off += 24;
    }
}

/*
 * Resolve a channel's TX talkgroup index (1-based, into the talkgroup list)
 * via the per-channel TX-contact maps in blocks 0x42/0x43. Returns 0 if none.
 */
static unsigned dm32_channel_tg_index(unsigned ch_index)
{
    const unsigned char *blk;
    unsigned off;

    if (ch_index >= 1 && ch_index <= 2048 && dm32_txc_lo_block) {
        blk = dm32_mem_ptr(dm32_txc_lo_block, DM32_PAGE_SIZE);
        off = (ch_index - 1) * 2;
    } else if (ch_index > 2048 && dm32_txc_hi_block) {
        blk = dm32_mem_ptr(dm32_txc_hi_block, DM32_PAGE_SIZE);
        off = (ch_index & 0x7FF) * 2;
    } else {
        return 0;
    }
    if (!blk || off + 1 >= DM32_PAGE_SIZE) {
        return 0;
    }
    /* 12-bit talkgroup index: (byte0>>4)<<8 | byte1; 0 = none. */
    return (((blk[off] >> 4) & 0x0F) << 8) | blk[off + 1];
}

/*
 * RX group list table.  Reads the metadata 0x0F block.
 * Block layout (NeonPlug, validated on live hw 2026-07-14):
 *   0x00-0x03  bitmask u32 LE  (bit i=1 → group i+1 active)
 *   0x04-0x0F  reserved; 0x10 flag byte (0x01)
 *   0x11+      entries, each DM32_RXGROUP_ENTRY_SIZE (109) bytes:
 *               +0x00  name[11] null-term
 *               +0x0B  up to 32 × 3-byte LE raw DMR IDs (sentinel = 0 or 0xFFFFFF)
 * Member IDs are matched against the loaded talkgroup table; matched
 * entries are printed as their 1-based contact index.  Unknown IDs are
 * omitted (they aren't in the Contacts table so the user can't reference
 * them by number anyway).
 */
static int dm32_have_rxgroups(void)
{
    if (!dm32_rxgroup_block) {
        return 0;
    }
    const unsigned char *blk = dm32_mem_ptr(dm32_rxgroup_block, 4);
    return blk && (blk[0] | blk[1] | blk[2] | blk[3]);
}

static void dm32_print_rxgroups(FILE *out, int verbose)
{
    if (!dm32_have_rxgroups()) {
        return;
    }
    const unsigned char *blk = dm32_mem_ptr(dm32_rxgroup_block, DM32_PAGE_SIZE);
    if (!blk) {
        return;
    }
    uint32_t bitmask = (uint32_t)blk[0] | ((uint32_t)blk[1] << 8) |
                       ((uint32_t)blk[2] << 16) | ((uint32_t)blk[3] << 24);
    if (!bitmask) {
        return;
    }

    if (!dm32_tgs_parsed) {
        dm32_parse_talkgroups();
    }

    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of receive group lists.\n");
        fprintf(out, "# 1) Grouplist number: 1-32\n");
        fprintf(out, "# 2) Name: up to 11 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Members: contact numbers from the Contact table, comma-separated\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Grouplist  Name        Members\n");

    for (unsigned i = 0; i < DM32_RXGROUP_MAX_GROUPS; ++i) {
        if (!(bitmask & (1u << i))) {
            continue;
        }
        uint32_t off = DM32_RXGROUP_HDR + i * DM32_RXGROUP_ENTRY_SIZE;
        if (off + DM32_RXGROUP_ENTRY_SIZE > DM32_PAGE_SIZE) {
            break;
        }
        const unsigned char *entry = blk + off;

        fprintf(out, "%5u     ", i + 1);
        print_ascii(out, entry, DM32_RXGROUP_NAME_LEN, 1);
        fprintf(out, "    ");

        int first = 1;
        for (unsigned slot = 0; slot < DM32_RXGROUP_MAX_CONTACTS; ++slot) {
            uint32_t id_off = DM32_RXGROUP_CONTACT_OFF + slot * 3u;
            if (id_off + 3u > DM32_RXGROUP_ENTRY_SIZE) {
                break;
            }
            uint32_t dmr_id = (uint32_t)entry[id_off] |
                              ((uint32_t)entry[id_off + 1] << 8) |
                              ((uint32_t)entry[id_off + 2] << 16);
            if (dmr_id == 0 || dmr_id == 0xFFFFFFu) {
                break;
            }
            /* Find 1-based index in talkgroup table by DMR ID. */
            unsigned tg_idx = 0;
            for (unsigned t = 0; t < dm32_ntgs; ++t) {
                if (dm32_tgs[t].number == dmr_id) {
                    tg_idx = t + 1;
                    break;
                }
            }
            if (!tg_idx) {
                /* ID not in Contact table; skip rather than emit invalid ref. */
                continue;
            }
            if (!first) {
                fprintf(out, ",");
            }
            fprintf(out, "%u", tg_idx);
            first = 0;
        }
        if (first) {
            fprintf(out, "-");
        }
        fprintf(out, "\n");
    }
}

/*
 * Table of contacts (talkgroups), canonical dmrconfig format.
 *   1) Contact number
 *   2) Name (use '_' for space)
 *   3) Call type: Group, Private, All
 *   4) Call ID
 *   5) Receive tone: -, +
 */
static void dm32_print_contacts(FILE *out, int verbose)
{
    if (!dm32_tgs_parsed) {
        dm32_parse_talkgroups();
    }
    if (dm32_ntgs == 0) {
        return;
    }
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of contacts.\n");
        fprintf(out, "# 1) Contact number: 1-%u\n", DM32_MAX_TG);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Call type: Group, Private, All\n");
        fprintf(out, "# 4) Call ID: 1...16777215\n");
        fprintf(out, "# 5) Call receive tone: -, +\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Contact Name             Type    ID       RxTone\n");
    for (unsigned i = 0; i < dm32_ntgs; ++i) {
        const char *ct = dm32_tgs[i].call_type == 0x03 ? "Private" :
                         dm32_tgs[i].call_type == 0x04 ? "Group" :
                         dm32_tgs[i].call_type == 0x05 ? "All" : "Group";
        fprintf(out, "%5u   ", i + 1);
        print_ascii(out, (const unsigned char *)dm32_tgs[i].name, 16, 1);
        fprintf(out, " %-7s %-8u %s\n", ct, dm32_tgs[i].number, "-");
    }
}

static void dm32_format_tone(const uint8_t *t, char *buf, size_t n)
{
    uint8_t lo = t[0], hi = t[1];
    if ((lo == 0xFF && hi == 0xFF) || (lo == 0x00 && hi == 0x00)) {
        snprintf(buf, n, "-");
    } else if (hi >= 0x80) {
        /* DCS: BCD digits. hundreds = hi low nibble; tens/ones = lo nibbles.
         * high byte 0x80-0xBF = normal (N), >=0xC0 = inverted (P). */
        unsigned code = (hi & 0x0F) * 100 + ((lo >> 4) & 0x0F) * 10 + (lo & 0x0F);
        snprintf(buf, n, "D%03u%c", code, (hi >= 0xC0) ? 'P' : 'N');
    } else {
        /* CTCSS: BCD. hundreds = hi[7-4], tens = hi[3-0],
         * ones = lo[7-4], tenths = lo[3-0]. */
        unsigned whole = ((hi >> 4) & 0x0F) * 100 + (hi & 0x0F) * 10 +
                         ((lo >> 4) & 0x0F);
        snprintf(buf, n, "%u.%u", whole, lo & 0x0F);
    }
}

/* Decoded view of one channel slot. */
typedef struct {
    unsigned            number;     /* 1-based channel number             */
    const channel_raw_t *raw;       /* pointer into radio_mem             */
    double              rx, tx;     /* frequencies in MHz                 */
    int                 digital;    /* 1 = digital, 0 = analog            */
    unsigned            power;      /* 0=Low 1=Mid 2=High                 */
    int                 rx_only;    /* forbid TX                          */
    unsigned            scanlist;   /* 0 = none, else scan list index     */
    unsigned            colorcode;  /* digital color code                 */
    unsigned            timeslot;   /* 1 or 2                             */
    unsigned            rxgl;       /* 0 = none, else RX group list index */
    unsigned            contact;    /* 0 = none, else talkgroup index     */
    unsigned            squelch;    /* analog squelch level               */
    int                 wide;       /* 1 = 25 kHz, 0 = 12.5 kHz           */
} dm32_chan_dec_t;

static const char *dm32_power_name[] = { "Low", "Mid", "High", "High" };

/*
 * Decode one channel slot. Returns 1 if the slot is populated (has a name),
 * 0 if blank. The channel number counts every slot, matching the index used
 * by the TX-contact maps.
 */
static int dm32_decode_channel(const channel_raw_t *ch, unsigned number,
                               dm32_chan_dec_t *d)
{
    if (!ch || dm32_channel_blank(ch)) {
        return 0;
    }
    char name[LEN_CHANNEL_NAME + 1];
    dm32_copy_ascii(name, sizeof(name), ch->name, sizeof(ch->name));
    if (!name[0]) {
        return 0;
    }

    memset(d, 0, sizeof(*d));
    d->number = number;
    d->raw = ch;
    d->rx = dm32_bcd_mhz(ch->rx_bcd);
    d->tx = dm32_bcd_mhz(ch->tx_bcd);
    /* Receive-only channels (e.g. aviation) store an all-0xFF TX frequency;
     * present TX = RX rather than a bogus decoded value. */
    if (ch->tx_bcd[0] == 0xFF && ch->tx_bcd[1] == 0xFF &&
        ch->tx_bcd[2] == 0xFF && ch->tx_bcd[3] == 0xFF) {
        d->tx = d->rx;
    }

    unsigned mode = (ch->mode_flags & DM32_MODE_MASK) >> DM32_MODE_SHIFT;
    d->digital = (mode & 1);    /* 0=Analog,1=Digital,2=FixAna,3=FixDig */
    d->power = (ch->mode_flags & DM32_POWER_MASK) >> DM32_POWER_SHIFT;
    d->rx_only = (ch->mode_flags & DM32_FORBID_TX_BIT) != 0;
    d->scanlist = (ch->scan_bw & DM32_SCANLIST_MASK) >> DM32_SCANLIST_SHIFT;
    d->wide = (ch->scan_bw & DM32_BW_WIDE_BIT) != 0;

    if (d->digital) {
        /* byte 0x1D: bit4 time slot, bits3-0 color code */
        d->timeslot = (ch->analog & DM32_DIG_TS_BIT) ? 2 : 1;
        d->colorcode = ch->analog & DM32_DIG_CC_MASK;
        d->rxgl = ch->ptt_id & 0x3F;    /* byte 0x1F bits5-0 = RX group list */
        d->contact = dm32_channel_tg_index(number);
    } else {
        d->squelch = (ch->squelch_aprs >> 4) & 0x0F;
    }
    return 1;
}

/* Return 1 if any populated channel matches the requested mode. */
static int dm32_have_channels(int want_digital)
{
    if (dm32_chan_nblocks == 0) {
        return 0;
    }
    unsigned number = 0;
    for (unsigned bi = 0; bi < dm32_chan_nblocks; ++bi) {
        uint32_t base = dm32_chan_blocks[bi];
        uint32_t off = (bi == 0) ? DM32_CHANNEL_HDR : 0;
        for (; off + DM32_CHANNEL_SIZE <= DM32_PAGE_SIZE; off += DM32_CHANNEL_SIZE) {
            const channel_raw_t *ch =
                (const channel_raw_t *)dm32_mem_ptr(base + off, sizeof(channel_raw_t));
            ++number;
            dm32_chan_dec_t d;
            if (dm32_decode_channel(ch, number, &d) && d.digital == want_digital) {
                return 1;
            }
        }
    }
    return 0;
}

/* Format "-" or an unsigned into a small buffer for a table cell. */
static void dm32_cell_index(char *buf, size_t n, unsigned value)
{
    if (value == 0) {
        snprintf(buf, n, "-");
    } else {
        snprintf(buf, n, "%u", value);
    }
}

static void dm32_print_digital_channels(FILE *out, int verbose)
{
    if (verbose) {
        fprintf(out, "# Table of digital channels.\n");
        fprintf(out, "# 1) Channel number: 1-%u\n", MAX_CHANNELS);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Mid, Low\n");
        fprintf(out, "# 6) Scan list: - or index in Scanlist table\n");
        fprintf(out, "# 7) Transmit timeout timer in seconds: (- if unknown)\n");
        fprintf(out, "# 8) Receive only: -, +\n");
        fprintf(out, "# 9) Admit criteria: -, Free, Color\n");
        fprintf(out, "# 10) Color code: 0, 1, 2, 3... 15\n");
        fprintf(out, "# 11) Time slot: 1 or 2\n");
        fprintf(out, "# 12) Receive group list: - or index in Grouplist table\n");
        fprintf(out, "# 13) Contact for transmit: - or index in Contacts table\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Digital Name             Receive   Transmit Power Scan TOT RO Admit  Color Slot RxGL TxContact\n");

    unsigned number = 0;
    for (unsigned bi = 0; bi < dm32_chan_nblocks; ++bi) {
        uint32_t base = dm32_chan_blocks[bi];
        uint32_t off = (bi == 0) ? DM32_CHANNEL_HDR : 0;
        for (; off + DM32_CHANNEL_SIZE <= DM32_PAGE_SIZE; off += DM32_CHANNEL_SIZE) {
            const channel_raw_t *ch =
                (const channel_raw_t *)dm32_mem_ptr(base + off, sizeof(channel_raw_t));
            ++number;
            dm32_chan_dec_t d;
            if (!dm32_decode_channel(ch, number, &d) || !d.digital) {
                continue;
            }
            char scanstr[6], rxglstr[6], ctstr[6];
            dm32_cell_index(scanstr, sizeof(scanstr), d.scanlist);
            dm32_cell_index(rxglstr, sizeof(rxglstr), d.rxgl);
            dm32_cell_index(ctstr, sizeof(ctstr), d.contact);

            fprintf(out, "%5u   ", d.number);
            print_ascii(out, ch->name, 16, 1);
            fprintf(out, " %9.5f %9.5f %-5s %-4s %-3s %c  %-6s %-5u %-4u %-4s %-4s",
                    d.rx, d.tx, dm32_power_name[d.power & 3], scanstr, "-",
                    d.rx_only ? '+' : '-', "-", d.colorcode, d.timeslot,
                    rxglstr, ctstr);
            if (d.contact >= 1 && d.contact <= dm32_ntgs) {
                fprintf(out, " # ");
                print_ascii(out, (const unsigned char *)dm32_tgs[d.contact - 1].name, 16, 0);
            }
            fprintf(out, "\n");
        }
    }
}

static void dm32_print_analog_channels(FILE *out, int verbose)
{
    if (verbose) {
        fprintf(out, "# Table of analog channels.\n");
        fprintf(out, "# 1) Channel number: 1-%u\n", MAX_CHANNELS);
        fprintf(out, "# 2) Name: up to 16 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Receive frequency in MHz\n");
        fprintf(out, "# 4) Transmit frequency or +/- offset in MHz\n");
        fprintf(out, "# 5) Transmit power: High, Mid, Low\n");
        fprintf(out, "# 6) Scan list: - or index\n");
        fprintf(out, "# 7) Transmit timeout timer in seconds: (- if unknown)\n");
        fprintf(out, "# 8) Receive only: -, +\n");
        fprintf(out, "# 9) Admit criteria: -, Free, Tone\n");
        fprintf(out, "# 10) Squelch level: 0, 1, 2, 3... 9\n");
        fprintf(out, "# 11) Guard tone for receive, or '-' to disable\n");
        fprintf(out, "# 12) Guard tone for transmit, or '-' to disable\n");
        fprintf(out, "# 13) Bandwidth in kHz: 12.5, 25\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Analog  Name             Receive   Transmit Power Scan TOT RO Admit  Squelch RxTone TxTone Width\n");

    unsigned number = 0;
    for (unsigned bi = 0; bi < dm32_chan_nblocks; ++bi) {
        uint32_t base = dm32_chan_blocks[bi];
        uint32_t off = (bi == 0) ? DM32_CHANNEL_HDR : 0;
        for (; off + DM32_CHANNEL_SIZE <= DM32_PAGE_SIZE; off += DM32_CHANNEL_SIZE) {
            const channel_raw_t *ch =
                (const channel_raw_t *)dm32_mem_ptr(base + off, sizeof(channel_raw_t));
            ++number;
            dm32_chan_dec_t d;
            if (!dm32_decode_channel(ch, number, &d) || d.digital) {
                continue;
            }
            char scanstr[6], rxtone[8], txtone[8];
            dm32_cell_index(scanstr, sizeof(scanstr), d.scanlist);
            dm32_format_tone(ch->rx_tone, rxtone, sizeof(rxtone));
            dm32_format_tone(ch->tx_tone, txtone, sizeof(txtone));

            fprintf(out, "%5u   ", d.number);
            print_ascii(out, ch->name, 16, 1);
            fprintf(out, " %9.5f %9.5f %-5s %-4s %-3s %c  %-6s %-7u %-6s %-6s %s\n",
                    d.rx, d.tx, dm32_power_name[d.power & 3], scanstr, "-",
                    d.rx_only ? '+' : '-', "-", d.squelch, rxtone, txtone,
                    d.wide ? "25" : "12.5");
        }
    }
}

/* Return 1 if the zone block holds at least one populated zone. */
static int dm32_have_zones(void)
{
    if (!dm32_zone_block) {
        return 0;
    }
    const unsigned char *z = dm32_mem_ptr(dm32_zone_block + DM32_ZONE_HDR, DM32_ZONE_SIZE);
    return z && z[0] != 0xFF && z[0] != 0x00;
}

static void dm32_print_zones(FILE *out, int verbose)
{
    if (!dm32_have_zones()) {
        return;
    }
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of channel zones.\n");
        fprintf(out, "# 1) Zone number\n");
        fprintf(out, "# 2) Name: up to 11 characters, use '_' instead of space\n");
        fprintf(out, "# 3) List of channels: numbers separated by comma\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Zone    Name        Channels\n");

    for (unsigned n = 1; ; ++n) {
        uint32_t off = DM32_ZONE_HDR + (n - 1) * DM32_ZONE_SIZE;
        if (off + DM32_ZONE_SIZE > DM32_PAGE_SIZE) {
            break;
        }
        const unsigned char *z = dm32_mem_ptr(dm32_zone_block + off, DM32_ZONE_SIZE);
        if (!z || z[0] == 0xFF || z[0] == 0x00) {
            break;      /* first empty slot terminates the list */
        }

        unsigned cnt = z[0x10];
        if (cnt > 64) {
            cnt = 64;
        }
        fprintf(out, "%4u    ", n);
        print_ascii(out, z, LEN_ZONE_NAME, 1);
        fprintf(out, " ");
        int any = 0;
        for (unsigned i = 0; i < cnt; ++i) {
            unsigned ch = z[0x11 + i * 2] | ((unsigned)z[0x12 + i * 2] << 8);
            if (ch == 0) {
                break;
            }
            fprintf(out, "%s%u", any ? "," : "", ch);
            any = 1;
        }
        if (!any) {
            fprintf(out, "-");
        }
        fprintf(out, "\n");
    }
}

/* Return 1 if the scan-list block holds at least one populated entry. */
static int dm32_have_scanlists(void)
{
    return dm32_scan_block && radio_mem[dm32_scan_block] > 0;
}

static void dm32_print_scanlists(FILE *out, int verbose)
{
    if (!dm32_have_scanlists()) {
        return;
    }
    fprintf(out, "\n");
    if (verbose) {
        fprintf(out, "# Table of scan lists.\n");
        fprintf(out, "# 1) Scan list number\n");
        fprintf(out, "# 2) Name: up to 11 characters, use '_' instead of space\n");
        fprintf(out, "# 3) Priority channel 1: -, Cur or index\n");
        fprintf(out, "# 4) Priority channel 2: -, Cur or index\n");
        fprintf(out, "# 5) Designated transmit channel: Last, Cur or index\n");
        fprintf(out, "# 6) List of channels: numbers separated by comma\n");
        fprintf(out, "#\n");
    }
    fprintf(out, "Scanlist Name        PCh1 PCh2 TxCh Channels\n");

    unsigned count = radio_mem[dm32_scan_block];   /* count byte at +0x00 */
    for (unsigned n = 1; n <= count && n <= 71; ++n) {
        uint32_t off = (DM32_SCANLIST_SIZE * n) - 56;   /* entry N: (57*N)-56 */
        if (off + DM32_SCANLIST_SIZE > DM32_PAGE_SIZE) {
            break;
        }
        const unsigned char *s = dm32_mem_ptr(dm32_scan_block + off, DM32_SCANLIST_SIZE);
        if (!s) {
            break;
        }
        char name[LEN_SCANLIST_NAME + 1];
        dm32_copy_ascii(name, sizeof(name), s, LEN_SCANLIST_NAME);
        if (!name[0]) {
            continue;
        }

        /*
         * 57-byte entry layout (little-endian):
         *   0x0B channel count; 0x0E priority types (bits3-0 pri1, bits7-4 pri2);
         *   0x0F-10 priority channel 1 (stored directly);
         *   0x13-14 priority channel 2 (stored = actual-2);
         *   0x1A.. up to 15 member channels (u16 LE, 0/0xFFFF terminates).
         */
        unsigned pri_types = s[0x0E];
        unsigned pri1_type = pri_types & 0x0F;
        unsigned pri2_type = (pri_types >> 4) & 0x0F;
        char pch1[6], pch2[6];
        if (pri1_type == 2) {
            unsigned c = s[0x0F] | ((unsigned)s[0x10] << 8);
            snprintf(pch1, sizeof(pch1), "%u", c);
        } else {
            snprintf(pch1, sizeof(pch1), "%s", pri1_type == 1 ? "Cur" : "-");
        }
        if (pri2_type == 2) {
            unsigned c = (s[0x13] | ((unsigned)s[0x14] << 8)) + 2;
            snprintf(pch2, sizeof(pch2), "%u", c);
        } else {
            snprintf(pch2, sizeof(pch2), "%s", pri2_type == 1 ? "Cur" : "-");
        }

        unsigned cnt = s[0x0B];
        if (cnt > 15) {
            cnt = 15;
        }
        fprintf(out, "%5u    ", n);
        print_ascii(out, s, LEN_SCANLIST_NAME, 1);
        fprintf(out, " %-4s %-4s %-4s ", pch1, pch2, "Last");
        int any = 0;
        for (unsigned i = 0; i < cnt; ++i) {
            unsigned ch = s[0x1A + i * 2] | ((unsigned)s[0x1B + i * 2] << 8);
            if (ch == 0 || ch == 0xFFFF) {
                break;
            }
            fprintf(out, "%s%u", any ? "," : "", ch);
            any = 1;
        }
        if (!any) {
            fprintf(out, "-");
        }
        fprintf(out, "\n");
    }
}

/*
 * Emit the radio's DMR ID and name as canonical scalar parameters. The DM-32
 * keeps a list of IDs (block 0x67); the first entry is the primary ID. Any
 * additional IDs are listed as comments for reference.
 */
static void dm32_print_id(FILE *out, int verbose)
{
    uint32_t id = 0;
    char name[LEN_RADIOID_NAME + 1] = { 0 };
    unsigned count = 0;

    if (dm32_radioid_block) {
        count = radio_mem[dm32_radioid_block] |
                ((uint32_t)radio_mem[dm32_radioid_block + 1] << 8) |
                ((uint32_t)radio_mem[dm32_radioid_block + 2] << 16) |
                ((uint32_t)radio_mem[dm32_radioid_block + 3] << 24);
        if (count == 0 || count > 256) {
            count = 256;
        }
        const unsigned char *r = dm32_mem_ptr(dm32_radioid_block + DM32_RADIOID_SIZE,
                                              DM32_RADIOID_SIZE);
        if (r) {
            id = r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16);
            dm32_copy_ascii(name, sizeof(name), r + 3, LEN_RADIOID_NAME);
        }
    }

    if (verbose) {
        fprintf(out, "\n# Unique DMR ID and name of this radio.");
    }
    fprintf(out, "\nID: %u\nName: %s\n", id, name[0] ? name : "-");

    /* List any additional radio IDs as comments. */
    for (unsigned n = 1; n < count; ++n) {
        uint32_t off = (n + 1) * DM32_RADIOID_SIZE;
        if (off + DM32_RADIOID_SIZE > DM32_PAGE_SIZE) {
            break;
        }
        const unsigned char *r = dm32_mem_ptr(dm32_radioid_block + off, DM32_RADIOID_SIZE);
        if (!r) {
            break;
        }
        char nm[LEN_RADIOID_NAME + 1];
        dm32_copy_ascii(nm, sizeof(nm), r + 3, LEN_RADIOID_NAME);
        uint32_t rid = r[0] | ((uint32_t)r[1] << 8) | ((uint32_t)r[2] << 16);
        if (rid == 0 && !nm[0]) {
            continue;
        }
        fprintf(out, "# Radio ID %u: %u %s\n", n + 1, rid, nm[0] ? nm : "-");
    }
}

static void dm32_print_boot_message(FILE *out)
{
    char line1[LEN_BOOT_LINE + 1];
    char line2[LEN_BOOT_LINE + 1];
    const unsigned char *l1 = dm32_mem_ptr(OFFSET_BOOT_LINE1, LEN_BOOT_LINE);
    const unsigned char *l2 = dm32_mem_ptr(OFFSET_BOOT_LINE2, LEN_BOOT_LINE);
    if (l1) {
        dm32_copy_ascii(line1, sizeof(line1), l1, LEN_BOOT_LINE);
    } else {
        line1[0] = '\0';
    }
    if (l2) {
        dm32_copy_ascii(line2, sizeof(line2), l2, LEN_BOOT_LINE);
    } else {
        line2[0] = '\0';
    }

    if (line1[0] || line2[0]) {
        fprintf(out, "# Boot Message: %s / %s\n",
                line1[0] ? line1 : "-",
                line2[0] ? line2 : "-");
    }
}

static void dm32_print_version(radio_device_t *radio, FILE *out)
{
    (void)radio;
    fprintf(out, "# Baofeng DM-32UV (experimental)\n");
    const unsigned char *fw_ptr = dm32_mem_ptr(OFFSET_FW_VERSION, LEN_FW_VERSION);
    char fw_buf[LEN_FW_VERSION + 1] = {0};
    if (fw_ptr) {
        dm32_copy_ascii(fw_buf, sizeof(fw_buf), fw_ptr, LEN_FW_VERSION);
    }

    if (dm32_board_id[0]) {
        fprintf(out, "# Board: %s\n", dm32_board_id);
    }
    if (dm32_fw_version_cached[0]) {
        fprintf(out, "# Firmware (via V-frame): %s\n", dm32_fw_version_cached);
    } else if (fw_buf[0]) {
        fprintf(out, "# Firmware: %s\n", fw_buf);
    }
    if (dm32_build_date[0]) {
        fprintf(out, "# Build Date: %s\n", dm32_build_date);
    }
    dm32_print_boot_message(out);
}

static void dm32_print_config(radio_device_t *radio, FILE *out, int verbose)
{
    fprintf(out, "Radio: %s\n", radio->name);
    if (verbose) {
        dm32_print_version(radio, out);
    }

    /* When displaying a loaded image, classify the layout from memory first. */
    if (dm32_chan_nblocks == 0 && dm32_zone_block == 0) {
        dm32_classify();
    }
    if (!dm32_tgs_parsed) {
        dm32_parse_talkgroups();
    }

    //
    // Channels.
    //
    if (dm32_have_channels(1)) {
        fprintf(out, "\n");
        dm32_print_digital_channels(out, verbose);
    }
    if (dm32_have_channels(0)) {
        fprintf(out, "\n");
        dm32_print_analog_channels(out, verbose);
    }

    //
    // Zones, scan lists, contacts.
    //
    dm32_print_zones(out, verbose);
    dm32_print_scanlists(out, verbose);
    dm32_print_rxgroups(out, verbose);
    dm32_print_contacts(out, verbose);

    //
    // Radio ID and name.
    //
    dm32_print_id(out, verbose);
}

static void dm32_download(radio_device_t *radio)
{
    (void)radio;
    if (serial_open_found(DM32_BAUD) < 0) {
        fprintf(stderr, "DM32: unable to open serial port at %d\n", DM32_BAUD);
        return;
    }

    dm32_board_id[0] = '\0';
    dm32_fw_version_cached[0] = '\0';
    dm32_build_date[0] = '\0';
    dm32_nvsegs = 0;
    dm32_read_addrs_n = 0;
    dm32_written_max = 0;
    radio_progress = 0;

    (void)serial_pulse_rts_dtr();
    usleep(150000);

    fprintf(stderr, "DM32: performing handshake...\n");
    dm32_collect_handshake_info();

    fprintf(stderr, "DM32: requesting resource banner...\n");
    static const unsigned char resource_req[] = { 0x47, 0x00, 0x00, 0x00, 0x00, 0x01 };
    serial_write(resource_req, sizeof(resource_req));
    dm32_collect_reads(NULL, 0, 200);

    fprintf(stderr, "DM32: entering PROGRAM mode...\n");
    dm32_enter_program_mode();

    fprintf(stderr, "DM32: discovering memory layout...\n");
    dm32_discover();

    fprintf(stderr, "DM32: reading data blocks...\n");
    dm32_read_discovered();
    radio_progress = 100;
}

static void dm32_upload(radio_device_t *radio, int cont_flag)
{
    (void)radio;
    (void)cont_flag;

    fprintf(stderr,
        "\nDM32: write support is EXPERIMENTAL and has NOT been verified on\n"
        "      hardware. Make sure you have a backup image (dmrconfig -r)\n"
        "      before continuing.\n");

    if (serial_open_found(DM32_BAUD) < 0) {
        fprintf(stderr, "DM32: unable to open serial port at %d\n", DM32_BAUD);
        return;
    }

    dm32_board_id[0] = '\0';
    dm32_nvsegs = 0;
    dm32_main_start = DM32_MAIN_START;
    dm32_main_end = DM32_MAIN_END;
    radio_progress = 0;

    (void)serial_pulse_rts_dtr();
    usleep(150000);

    fprintf(stderr, "DM32: handshake...\n");
    dm32_collect_handshake_info();

    fprintf(stderr, "DM32: entering PROGRAM mode...\n");
    dm32_enter_program_mode();

    /*
     * Build a type -> address map for the radio's CURRENT layout by probing the
     * type marker at block+0xFFF (as the OEM CPS does before writing), without
     * disturbing the image staged in radio_mem.
     */
    uint32_t radio_addr[256];
    memset(radio_addr, 0, sizeof(radio_addr));
    uint32_t start = dm32_page_base(dm32_main_start);
    uint32_t end = dm32_main_end;
    for (uint32_t addr = start;
         addr <= end && addr + DM32_PAGE_SIZE <= MEMSZ;
         addr += DM32_PAGE_SIZE) {
        int t = dm32_read_byte_raw(addr + DM32_META_OFFSET);
        if (t <= 0 || t == 0xFF) {
            continue;
        }
        if (radio_addr[t] == 0) {
            radio_addr[t] = addr;
        }
    }

    /*
     * Write every populated block back to its OWN address. The staged image
     * (from an -r download or a full image file) already places each 4 KiB
     * block at its true radio address, so writing in place cannot relocate a
     * block. This is what NeonPlug does, and it is essential: relocating the
     * type 0x04 radio-settings block (which holds the display language) is what
     * previously reset the radio to Chinese after a write. The radio_addr probe
     * above is kept only to prime the radio and as an optional sanity check;
     * its results are deliberately NOT used as write targets, because the
     * marker probe can desync and report wrong addresses.
     */
    unsigned total = 0;
    for (uint32_t addr = start;
         addr <= end && addr + DM32_PAGE_SIZE <= MEMSZ;
         addr += DM32_PAGE_SIZE) {
        unsigned char t = radio_mem[addr + DM32_META_OFFSET];
        if (t == DM32_META_EMPTY || t == DM32_META_INVALID || t == DM32_META_SETTINGS) {
            continue;
        }
        ++total;
    }
    if (total == 0) {
        fprintf(stderr, "DM32: no writable blocks in image; aborting.\n");
        return;
    }

    fprintf(stderr, "DM32: writing %u blocks...\n", total);
    unsigned done = 0;
    for (uint32_t addr = start;
         addr <= end && addr + DM32_PAGE_SIZE <= MEMSZ;
         addr += DM32_PAGE_SIZE) {
        unsigned char t = radio_mem[addr + DM32_META_OFFSET];
        if (t == DM32_META_EMPTY || t == DM32_META_INVALID || t == DM32_META_SETTINGS) {
            continue;
        }
        if (trace_flag && radio_addr[t] && radio_addr[t] != addr) {
            fprintf(stderr, "DM32: note type 0x%02X image@0x%06X vs radio@0x%06X\n",
                    t, addr, radio_addr[t]);
        }
        const unsigned char *data = dm32_mem_ptr(addr, DM32_PAGE_SIZE);
        if (!data) {
            continue;
        }
        if (dm32_write_block(addr, data) != 0) {
            fprintf(stderr, "DM32: write failed at 0x%06X (type 0x%02X)\n",
                    addr, t);
            return;
        }
        ++done;
        radio_progress = (int)(done * 100 / total);
        usleep(20000);      /* 20 ms between block writes */
    }

    /* Leave programming mode. */
    static const unsigned char end_cmd[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x0C, 'E', 'N', 'D', 0x00, 0x00, 0x00, 0x00
    };
    serial_write(end_cmd, sizeof(end_cmd));
    dm32_collect_reads(NULL, 0, 200);
    radio_progress = 100;
}

static int dm32_is_compatible(radio_device_t *radio)
{
    (void)radio;
    /* Some images carry the firmware string at offset 0x30. */
    const unsigned char *fw_ptr = dm32_mem_ptr(OFFSET_FW_VERSION, 4);
    if (fw_ptr && fw_ptr[0] == 'D' && fw_ptr[1] == 'M' &&
        fw_ptr[2] == '3' && fw_ptr[3] == '2') {
        return 1;
    }
    /* Otherwise accept the image if it contains DM-32 channel blocks. */
    dm32_classify();
    return dm32_chan_nblocks > 0;
}

static void dm32_read_image(radio_device_t *radio, FILE *img)
{
    (void)radio;
    size_t total = fread(radio_mem, 1, MEMSZ, img);
    if (total < MEMSZ) {
        memset(radio_mem + total, 0xFF, MEMSZ - total);
    } else if (total > MEMSZ) {
        fprintf(stderr, "DM32 image larger than expected (%zu bytes).\n", total);
        exit(-1);
    }
}

static void dm32_save_image(radio_device_t *radio, FILE *img)
{
    (void)radio;
    fwrite(radio_mem, 1, MEMSZ, img);
}

static int dm32_verify_config(radio_device_t *radio)
{
    (void)radio;
    return 1;
}

/*
 * ------------------------------------------------------------------------
 * Configuration parsing (text .conf -> radio_mem).
 *
 * The image in radio_mem is populated first (download or read_image), so its
 * dynamic block layout is already known. Parsing edits the mutable fields of
 * existing records in place; it does not allocate, relocate, or renumber
 * blocks. Bits that are not represented in the text format are preserved.
 * ------------------------------------------------------------------------
 */

/* Ensure the block layout is classified before editing records. */
static void dm32_ensure_ready(void)
{
    if (dm32_chan_nblocks == 0 && dm32_zone_block == 0) {
        dm32_classify();
    }
}

static int dm32_freq_eq(double a, double b)
{
    double d = a - b;
    return (d < 0 ? -d : d) < 1e-6;
}

/* Encode MHz into 4-byte little-endian BCD (inverse of dm32_bcd_mhz). */
static void dm32_mhz_to_bcd(double mhz, uint8_t p[4])
{
    unsigned long long digits = (unsigned long long)(mhz * 100000.0 + 0.5);
    uint8_t d[8];
    for (int i = 7; i >= 0; --i) {
        d[i] = digits % 10;
        digits /= 10;
    }
    p[3] = (uint8_t)((d[0] << 4) | d[1]);
    p[2] = (uint8_t)((d[2] << 4) | d[3]);
    p[1] = (uint8_t)((d[4] << 4) | d[5]);
    p[0] = (uint8_t)((d[6] << 4) | d[7]);
}

/* Encode a tone string into DM-32 2-byte format (inverse of dm32_format_tone). */
static void dm32_encode_tone(const char *s, uint8_t t[2])
{
    if (!s || s[0] == '-' || s[0] == 0) {
        t[0] = 0xFF;
        t[1] = 0xFF;
        return;
    }
    if (s[0] == 'D' || s[0] == 'd') {
        /* DCS: Dnnn followed by N (normal) or P/I (inverted). */
        unsigned code = 0;
        int i = 1;
        while (s[i] >= '0' && s[i] <= '9') {
            code = code * 10 + (unsigned)(s[i] - '0');
            ++i;
        }
        char pol = s[i];
        int inverted = (pol == 'P' || pol == 'p' || pol == 'I' || pol == 'i');
        t[0] = (uint8_t)((((code / 10) % 10) << 4) | (code % 10));
        t[1] = (uint8_t)((inverted ? 0xC0 : 0x80) | ((code / 100) & 0x0F));
        return;
    }
    /* CTCSS: nnn.n */
    unsigned freq10 = (unsigned)(atof(s) * 10.0 + 0.5);
    unsigned whole = freq10 / 10, tenth = freq10 % 10;
    t[1] = (uint8_t)((((whole / 100) % 10) << 4) | ((whole / 10) % 10));
    t[0] = (uint8_t)(((whole % 10) << 4) | tenth);
}

/*
 * Write an ASCII name into a fixed-width field. Underscores become spaces.
 * A 0x00 terminator is placed after the last character; bytes past the
 * terminator are left untouched, which preserves the original padding on a
 * byte-for-byte round trip.
 */
static void dm32_write_name(unsigned char *dst, const char *src, unsigned width)
{
    unsigned i = 0;
    for (; i < width && src[i]; ++i) {
        char c = src[i];
        dst[i] = (unsigned char)(c == '_' ? ' ' : c);
    }
    if (i < width) {
        dst[i] = 0x00;
    }
}

static unsigned dm32_parse_power(const char *s)
{
    if (strcasecmp(s, "Low") == 0) return 0;
    if (strcasecmp(s, "Mid") == 0) return 1;
    return 2;   /* High (default) */
}

/* Locate a mutable pointer to channel slot `number` (1-based); NULL if none. */
static unsigned char *dm32_channel_wptr(unsigned number)
{
    if (dm32_chan_nblocks == 0 || number == 0) {
        return NULL;
    }
    unsigned n = 0;
    for (unsigned bi = 0; bi < dm32_chan_nblocks; ++bi) {
        uint32_t base = dm32_chan_blocks[bi];
        uint32_t off = (bi == 0) ? DM32_CHANNEL_HDR : 0;
        for (; off + DM32_CHANNEL_SIZE <= DM32_PAGE_SIZE; off += DM32_CHANNEL_SIZE) {
            if (++n == number) {
                if (base + off + DM32_CHANNEL_SIZE > MEMSZ) {
                    return NULL;
                }
                return &radio_mem[base + off];
            }
        }
    }
    return NULL;
}

/* Write the per-channel TX-contact map entry (blocks 0x42/0x43). */
static void dm32_write_tx_contact(unsigned ch_index, unsigned tg, int digital)
{
    uint32_t block;
    unsigned off;
    if (ch_index >= 1 && ch_index <= 2048 && dm32_txc_lo_block) {
        block = dm32_txc_lo_block;
        off = (ch_index - 1) * 2;
    } else if (ch_index > 2048 && dm32_txc_hi_block) {
        block = dm32_txc_hi_block;
        off = (ch_index & 0x7FF) * 2;
    } else {
        return;
    }
    if (block + off + 1 >= MEMSZ) {
        return;
    }
    radio_mem[block + off]     = (uint8_t)((((tg >> 8) & 0x0F) << 4) | (digital ? 1 : 0));
    radio_mem[block + off + 1] = (uint8_t)(tg & 0xFF);
}

static unsigned char *dm32_zone_wptr(unsigned number)
{
    if (!dm32_zone_block || number == 0) {
        return NULL;
    }
    uint32_t off = DM32_ZONE_HDR + (number - 1) * DM32_ZONE_SIZE;
    if (off + DM32_ZONE_SIZE > DM32_PAGE_SIZE) {
        return NULL;
    }
    return &radio_mem[dm32_zone_block + off];
}

static unsigned char *dm32_scanlist_wptr(unsigned number)
{
    if (!dm32_scan_block || number == 0) {
        return NULL;
    }
    uint32_t off = (DM32_SCANLIST_SIZE * number) - 56;
    if (off + DM32_SCANLIST_SIZE > DM32_PAGE_SIZE) {
        return NULL;
    }
    return &radio_mem[dm32_scan_block + off];
}

/* Writable pointer to RX group entry `number` (1-based). */
static unsigned char *dm32_rxgroup_wptr(unsigned number)
{
    if (!dm32_rxgroup_block || number == 0 || number > DM32_RXGROUP_MAX_GROUPS) {
        return NULL;
    }
    uint32_t off = DM32_RXGROUP_HDR + (number - 1) * DM32_RXGROUP_ENTRY_SIZE;
    if (off + DM32_RXGROUP_ENTRY_SIZE > DM32_PAGE_SIZE) {
        return NULL;
    }
    return &radio_mem[dm32_rxgroup_block + off];
}

/* Pointer to the 16-byte name field of talkgroup `number` (1-based). */
static unsigned char *dm32_talkgroup_wptr(unsigned number)
{
    if (!dm32_tg_block || number == 0) {
        return NULL;
    }
    uint32_t name_off = 2 + (number - 1) * 24;   /* skip header + flag byte */
    if (name_off + 24 > DM32_PAGE_SIZE) {
        return NULL;
    }
    return &radio_mem[dm32_tg_block + name_off];
}

/* Parse a comma-separated channel list into `out`; returns the count. */
static unsigned dm32_parse_chanlist(const char *s, uint16_t *out, unsigned max)
{
    unsigned count = 0;
    if (!s || s[0] == '-' || s[0] == 0) {
        return 0;
    }
    while (*s && count < max) {
        while (*s == ',' || *s == ' ') {
            ++s;
        }
        if (*s < '0' || *s > '9') {
            break;
        }
        out[count++] = (uint16_t)strtoul(s, (char **)&s, 10);
    }
    return count;
}

static int dm32_parse_digital_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_s[64], name_s[64], rx_s[64], tx_s[64], pwr_s[64], scan_s[64];
    char tot_s[64], ro_s[64], adm_s[64], cc_s[64], ts_s[64], rxgl_s[64], ct_s[64];
    (void)first_row;

    if (sscanf(line, "%63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s",
               num_s, name_s, rx_s, tx_s, pwr_s, scan_s, tot_s, ro_s, adm_s,
               cc_s, ts_s, rxgl_s, ct_s) != 13) {
        return 0;
    }
    (void)tot_s;
    (void)adm_s;

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *rec = dm32_channel_wptr(num);
    if (!rec) {
        fprintf(stderr, "dm32: digital channel %u: no such slot.\n", num);
        return 0;
    }

    double rx = atof(rx_s);
    double tx = atof(tx_s);
    if (tx_s[0] == '+' || tx_s[0] == '-') {
        tx += rx;
    }
    unsigned power = dm32_parse_power(pwr_s);
    int rx_only = (ro_s[0] == '+');
    unsigned scan = (scan_s[0] == '-') ? 0 : (unsigned)atoi(scan_s);
    unsigned cc = (unsigned)atoi(cc_s) & 0x0F;
    unsigned ts = (unsigned)atoi(ts_s);
    unsigned rxgl = (rxgl_s[0] == '-') ? 0 : (unsigned)atoi(rxgl_s);
    unsigned ct = (ct_s[0] == '-') ? 0 : (unsigned)atoi(ct_s);

    dm32_write_name(rec, name_s, LEN_CHANNEL_NAME);
    dm32_mhz_to_bcd(rx, rec + 0x10);
    if (!(rx_only && dm32_freq_eq(tx, rx))) {
        dm32_mhz_to_bcd(tx, rec + 0x14);
    }
    /* mode nibble bit 0x10 = digital; preserve fixed-mode/reserved bits + lone. */
    rec[0x18] = (uint8_t)((rec[0x18] & (0x20 | 0x40 | 0x80 | 0x01)) | 0x10 |
                          ((power & 3) << 1) | (rx_only ? 0x08 : 0));
    rec[0x19] = (uint8_t)((rec[0x19] & ~DM32_SCANLIST_MASK) |
                          ((scan & 0x0F) << DM32_SCANLIST_SHIFT));
    rec[0x1D] = (uint8_t)((rec[0x1D] & 0xE0) | (ts == 2 ? DM32_DIG_TS_BIT : 0) |
                          (cc & DM32_DIG_CC_MASK));
    rec[0x1F] = (uint8_t)((rec[0x1F] & 0xC0) | (rxgl & 0x3F));
    dm32_write_tx_contact(num, ct, 1);

    if (radio) {
        radio->channel_count++;
    }
    return 1;
}

/* True if a 2-byte tone field encodes "no tone" (either 0x0000 or 0xFFFF). */
static int dm32_tone_field_none(const uint8_t *t)
{
    return (t[0] == 0xFF && t[1] == 0xFF) || (t[0] == 0x00 && t[1] == 0x00);
}

static int dm32_parse_analog_channel(radio_device_t *radio, int first_row, char *line)
{
    char num_s[64], name_s[64], rx_s[64], tx_s[64], pwr_s[64], scan_s[64];
    char tot_s[64], ro_s[64], adm_s[64], sq_s[64], rxt_s[64], txt_s[64], bw_s[64];
    (void)first_row;

    if (sscanf(line, "%63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s %63s",
               num_s, name_s, rx_s, tx_s, pwr_s, scan_s, tot_s, ro_s, adm_s,
               sq_s, rxt_s, txt_s, bw_s) != 13) {
        return 0;
    }
    (void)tot_s;
    (void)adm_s;

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *rec = dm32_channel_wptr(num);
    if (!rec) {
        fprintf(stderr, "dm32: analog channel %u: no such slot.\n", num);
        return 0;
    }

    double rx = atof(rx_s);
    double tx = atof(tx_s);
    if (tx_s[0] == '+' || tx_s[0] == '-') {
        tx += rx;
    }
    unsigned power = dm32_parse_power(pwr_s);
    int rx_only = (ro_s[0] == '+');
    unsigned scan = (scan_s[0] == '-') ? 0 : (unsigned)atoi(scan_s);
    unsigned squelch = (unsigned)atoi(sq_s) & 0x0F;
    int wide = (strcasecmp(bw_s, "25") == 0);

    dm32_write_name(rec, name_s, LEN_CHANNEL_NAME);
    dm32_mhz_to_bcd(rx, rec + 0x10);
    if (!(rx_only && dm32_freq_eq(tx, rx))) {
        dm32_mhz_to_bcd(tx, rec + 0x14);
    }
    /* Clear digital bit (analog); preserve fixed-mode/reserved bits + lone. */
    rec[0x18] = (uint8_t)((rec[0x18] & (0x20 | 0x40 | 0x80 | 0x01)) |
                          ((power & 3) << 1) | (rx_only ? 0x08 : 0));
    rec[0x19] = (uint8_t)((rec[0x19] & ~(DM32_BW_WIDE_BIT | DM32_SCANLIST_MASK)) |
                          (wide ? DM32_BW_WIDE_BIT : 0) |
                          ((scan & 0x0F) << DM32_SCANLIST_SHIFT));
    rec[0x1C] = (uint8_t)((rec[0x1C] & 0x0F) | (squelch << 4));
    /* Preserve the existing "none" marker (0x0000 vs 0xFFFF) when unchanged. */
    if (!(rxt_s[0] == '-' && dm32_tone_field_none(rec + 0x21))) {
        dm32_encode_tone(rxt_s, rec + 0x21);
    }
    if (!(txt_s[0] == '-' && dm32_tone_field_none(rec + 0x23))) {
        dm32_encode_tone(txt_s, rec + 0x23);
    }

    if (radio) {
        radio->channel_count++;
    }
    return 1;
}

static int dm32_parse_zone(int first_row, char *line)
{
    char num_s[64], name_s[64], chan_s[256];
    (void)first_row;

    int got = sscanf(line, "%63s %63s %255s", num_s, name_s, chan_s);
    if (got < 2) {
        return 0;
    }
    if (got < 3) {
        chan_s[0] = '-';
        chan_s[1] = 0;
    }

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *z = dm32_zone_wptr(num);
    if (!z) {
        fprintf(stderr, "dm32: zone %u: no such slot.\n", num);
        return 0;
    }

    dm32_write_name(z, name_s, LEN_ZONE_NAME);

    uint16_t members[64];
    unsigned cnt = dm32_parse_chanlist(chan_s, members, 64);
    z[0x10] = (uint8_t)cnt;
    for (unsigned i = 0; i < cnt; ++i) {
        z[0x11 + i * 2] = (uint8_t)(members[i] & 0xFF);
        z[0x12 + i * 2] = (uint8_t)((members[i] >> 8) & 0xFF);
    }
    return 1;
}

static int dm32_parse_scanlist(int first_row, char *line)
{
    char num_s[64], name_s[64], pch1_s[64], pch2_s[64], txch_s[64], chan_s[256];
    (void)first_row;

    int got = sscanf(line, "%63s %63s %63s %63s %63s %255s",
                     num_s, name_s, pch1_s, pch2_s, txch_s, chan_s);
    if (got < 5) {
        return 0;
    }
    if (got < 6) {
        chan_s[0] = '-';
        chan_s[1] = 0;
    }
    (void)txch_s;

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *s = dm32_scanlist_wptr(num);
    if (!s) {
        fprintf(stderr, "dm32: scan list %u: no such slot.\n", num);
        return 0;
    }

    dm32_write_name(s, name_s, LEN_SCANLIST_NAME);

    unsigned pri1_type, pri2_type;
    if (strcasecmp(pch1_s, "-") == 0) {
        pri1_type = 0;
    } else if (strcasecmp(pch1_s, "Cur") == 0) {
        pri1_type = 1;
    } else {
        unsigned c = (unsigned)atoi(pch1_s);
        pri1_type = 2;
        s[0x0F] = (uint8_t)(c & 0xFF);
        s[0x10] = (uint8_t)((c >> 8) & 0xFF);
    }
    if (strcasecmp(pch2_s, "-") == 0) {
        pri2_type = 0;
    } else if (strcasecmp(pch2_s, "Cur") == 0) {
        pri2_type = 1;
    } else {
        unsigned c = (unsigned)atoi(pch2_s);
        pri2_type = 2;
        if (c >= 2) {
            c -= 2;   /* stored value = actual - 2 */
        }
        s[0x13] = (uint8_t)(c & 0xFF);
        s[0x14] = (uint8_t)((c >> 8) & 0xFF);
    }
    s[0x0E] = (uint8_t)((pri1_type & 0x0F) | ((pri2_type & 0x0F) << 4));

    uint16_t members[15];
    unsigned cnt = dm32_parse_chanlist(chan_s, members, 15);
    s[0x0B] = (uint8_t)cnt;
    for (unsigned i = 0; i < cnt; ++i) {
        s[0x1A + i * 2] = (uint8_t)(members[i] & 0xFF);
        s[0x1B + i * 2] = (uint8_t)((members[i] >> 8) & 0xFF);
    }
    return 1;
}

static int dm32_parse_contact(int first_row, char *line)
{
    char num_s[64], name_s[64], type_s[64], id_s[64], rxt_s[64];
    (void)first_row;

    int got = sscanf(line, "%63s %63s %63s %63s %63s",
                     num_s, name_s, type_s, id_s, rxt_s);
    if (got < 4) {
        return 0;
    }
    (void)rxt_s;

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *name_ptr = dm32_talkgroup_wptr(num);
    if (!name_ptr) {
        fprintf(stderr, "dm32: contact %u: no such slot.\n", num);
        return 0;
    }

    dm32_write_name(name_ptr, name_s, 16);
    uint32_t id = strtoul(id_s, NULL, 0);
    unsigned char *fields = name_ptr + 16 + 1;   /* after name + null separator */
    fields[0] = (uint8_t)(id & 0xFF);
    fields[1] = (uint8_t)((id >> 8) & 0xFF);
    fields[2] = (uint8_t)((id >> 16) & 0xFF);
    fields[3] = (uint8_t)(strcasecmp(type_s, "Private") == 0 ? 0x03 :
                          strcasecmp(type_s, "All") == 0 ? 0x05 : 0x04);
    return 1;
}

static int dm32_parse_grouplist(int first_row, char *line)
{
    char num_s[64], name_s[64], mem_s[256];
    (void)first_row;

    int got = sscanf(line, "%63s %63s %255s", num_s, name_s, mem_s);
    if (got < 2) {
        return 0;
    }
    if (got < 3) {
        mem_s[0] = '-';
        mem_s[1] = 0;
    }

    unsigned num = (unsigned)atoi(num_s);
    unsigned char *entry = dm32_rxgroup_wptr(num);
    if (!entry) {
        fprintf(stderr, "dm32: grouplist %u: no such slot.\n", num);
        return 0;
    }

    /* Write name into entry. */
    dm32_write_name(entry, name_s, DM32_RXGROUP_NAME_LEN);

    /* Clear all contact ID slots (sentinel = 0x00). */
    memset(entry + DM32_RXGROUP_CONTACT_OFF, 0,
           DM32_RXGROUP_MAX_CONTACTS * 3u);

    /* Parse comma-separated contact indices; look up DMR IDs. */
    uint16_t indices[DM32_RXGROUP_MAX_CONTACTS];
    unsigned cnt = dm32_parse_chanlist(mem_s, indices, DM32_RXGROUP_MAX_CONTACTS);

    if (!dm32_tgs_parsed) {
        dm32_parse_talkgroups();
    }

    unsigned written = 0;
    for (unsigned i = 0; i < cnt; ++i) {
        unsigned idx = indices[i];
        if (idx == 0 || idx > dm32_ntgs) {
            fprintf(stderr, "dm32: grouplist %u: contact index %u out of range.\n",
                    num, idx);
            continue;
        }
        uint32_t dmr_id = dm32_tgs[idx - 1].number;
        uint32_t id_off = DM32_RXGROUP_CONTACT_OFF + written * 3u;
        entry[id_off]     = (uint8_t)(dmr_id & 0xFF);
        entry[id_off + 1] = (uint8_t)((dmr_id >> 8) & 0xFF);
        entry[id_off + 2] = (uint8_t)((dmr_id >> 16) & 0xFF);
        ++written;
    }

    /* Set bit (num-1) in the bitmask u32 LE at block base. */
    unsigned char *blk = &radio_mem[dm32_rxgroup_block];
    uint32_t bitmask = (uint32_t)blk[0] | ((uint32_t)blk[1] << 8) |
                       ((uint32_t)blk[2] << 16) | ((uint32_t)blk[3] << 24);
    bitmask |= (1u << (num - 1));
    blk[0] = (unsigned char)(bitmask & 0xFF);
    blk[1] = (unsigned char)((bitmask >> 8) & 0xFF);
    blk[2] = (unsigned char)((bitmask >> 16) & 0xFF);
    blk[3] = (unsigned char)((bitmask >> 24) & 0xFF);

    return 1;
}

static void dm32_parse_parameter(radio_device_t *radio, char *param, char *value)
{
    (void)radio;
    dm32_ensure_ready();

    if (strcasecmp("Radio", param) == 0) {
        if (!radio_is_compatible(value)) {
            fprintf(stderr, "Incompatible model: %s\n", value);
            exit(-1);
        }
        return;
    }
    if (!dm32_radioid_block) {
        return;
    }
    unsigned char *entry = &radio_mem[dm32_radioid_block + DM32_RADIOID_SIZE];
    if (dm32_radioid_block + 2 * DM32_RADIOID_SIZE > MEMSZ) {
        return;
    }
    if (strcasecmp("ID", param) == 0) {
        uint32_t id = strtoul(value, NULL, 0);
        entry[0] = (uint8_t)(id & 0xFF);
        entry[1] = (uint8_t)((id >> 8) & 0xFF);
        entry[2] = (uint8_t)((id >> 16) & 0xFF);
        /* Ensure the ID count is at least one. */
        if (radio_mem[dm32_radioid_block] == 0 &&
            radio_mem[dm32_radioid_block + 1] == 0 &&
            radio_mem[dm32_radioid_block + 2] == 0 &&
            radio_mem[dm32_radioid_block + 3] == 0) {
            radio_mem[dm32_radioid_block] = 1;
        }
        return;
    }
    if (strcasecmp("Name", param) == 0) {
        dm32_write_name(entry + 3, value, LEN_RADIOID_NAME);
        return;
    }
    /* Unknown scalar parameters are ignored (e.g. informational comments). */
}

static int dm32_parse_header(radio_device_t *radio, char *line)
{
    (void)radio;
    dm32_ensure_ready();

    if (strncasecmp(line, "Digital", 7) == 0)   return 'D';
    if (strncasecmp(line, "Analog", 6) == 0)    return 'A';
    if (strncasecmp(line, "Zone", 4) == 0)      return 'Z';
    if (strncasecmp(line, "Scanlist", 8) == 0)  return 'S';
    if (strncasecmp(line, "Grouplist", 9) == 0) return 'G';
    if (strncasecmp(line, "Contact", 7) == 0)   return 'C';
    return 0;
}

static int dm32_parse_row(radio_device_t *radio, int table_id, int first_row, char *line)
{
    switch (table_id) {
    case 'D': return dm32_parse_digital_channel(radio, first_row, line);
    case 'A': return dm32_parse_analog_channel(radio, first_row, line);
    case 'Z': return dm32_parse_zone(first_row, line);
    case 'S': return dm32_parse_scanlist(first_row, line);
    case 'C': return dm32_parse_contact(first_row, line);
    case 'G': return dm32_parse_grouplist(first_row, line);
    }
    return 0;
}

static void dm32_update_timestamp(radio_device_t *radio)
{
    (void)radio;
}

radio_device_t radio_dm32 = {
    "Baofeng DM-32UV",
    dm32_download,
    dm32_upload,
    dm32_is_compatible,
    dm32_read_image,
    dm32_save_image,
    dm32_print_version,
    dm32_print_config,
    dm32_verify_config,
    dm32_parse_parameter,
    dm32_parse_header,
    dm32_parse_row,
    dm32_update_timestamp,
    NULL,
};
