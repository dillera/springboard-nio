#include "mailbox.h"
#include "bus.h"
#include "console.h"
#include "sbnio.h"
#include "hardware/gpio.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "tusb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIO_ITF 0
#define REPLY_TIMEOUT_MS 30000
#define SLIP_END 0xC0
#define SLIP_ESC 0xDB
#define SLIP_ESC_END 0xDC
#define SLIP_ESC_ESC 0xDD

static uint8_t req[SBN_REQ_MAX];
static uint32_t req_len;
static uint8_t tx[2 * SBN_REQ_MAX + 2];
static uint32_t tx_len, tx_pos;
static uint8_t rx[SBN_RESP_MAX + 1];
static uint32_t rx_len;
static bool rx_esc, rx_overflow;
static bool awaiting, loopback, trace, host;
static uint8_t seq_now, dev_now, cmd_now;
static absolute_time_t deadline;
static uint64_t sent_at;
static uint32_t token_tail, token_peak;
static uint32_t tokens, overflowed, unknown, frames_dropped;
static char palm_log[96];
static unsigned palm_log_used;

/* The mailbox lives in the CS0 table above the ROM, at its own byte offsets. */
static void reg(uint16_t offset, uint16_t value) { bus_rom_words[offset / 2] = value; }

static void publish_status(void) {
    uint16_t st = 0;
    if (host) st |= SBN_ST_HOST;
    if (awaiting) st |= SBN_ST_BUSY;
    if (loopback) st |= SBN_ST_LOOPBACK;
    reg(SBN_R_STATUS, st);
}

static const char *result_name(uint16_t r) {
    static const char *const names[] = {"ok", "no-host", "timeout", "bad-length",
                                        "?", "too-big", "aborted"};
    return r < sizeof names / sizeof names[0] ? names[r] : "?";
}

/* Data first, then length and result, then the sequence the Palm polls. */
static void publish_reply(uint8_t seq, uint16_t result, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)(data[i] << 8 | (i + 1 < len ? data[i + 1] : 0));
        bus_rom_words[SBN_R_RESP / 2 + i / 2] = w;
    }
    reg(SBN_R_RESP_LEN, (uint16_t)len);
    reg(SBN_R_RESULT, result);
    __dmb();
    reg(SBN_R_RESP_SEQ, seq);
    awaiting = false;
    publish_status();
    console_printf("#%u reply %s, %lu bytes, %lu ms\n", seq, result_name(result),
                   (unsigned long)len, (unsigned long)((time_us_64() - sent_at) / 1000));
    if (len) console_hex("  <", data, len < 64 ? len : 64);
}

static void slip_queue(const uint8_t *data, uint32_t len) {
    tx_len = tx_pos = 0;
    tx[tx_len++] = SLIP_END;
    for (uint32_t i = 0; i < len; ++i) {
        if (data[i] == SLIP_END) { tx[tx_len++] = SLIP_ESC; tx[tx_len++] = SLIP_ESC_END; }
        else if (data[i] == SLIP_ESC) { tx[tx_len++] = SLIP_ESC; tx[tx_len++] = SLIP_ESC_ESC; }
        else tx[tx_len++] = data[i];
    }
    tx[tx_len++] = SLIP_END;
}

static void doorbell(uint8_t seq) {
    reg(SBN_R_ACK_SEQ, seq);
    sent_at = time_us_64();
    uint32_t len = req_len;
    if (awaiting) console_printf("#%u abandons #%u\n", seq, seq_now);
    awaiting = false;
    console_printf("#%u request %lu bytes\n", seq, (unsigned long)len);
    if (len) console_hex("  >", req, len < 64 ? len : 64);
    if (!len || overflowed) {
        overflowed = 0;
        return publish_reply(seq, SBN_RES_BAD_LEN, NULL, 0);
    }
    if (loopback) return publish_reply(seq, SBN_RES_OK, req, len);
    if (!host) return publish_reply(seq, SBN_RES_NO_HOST, NULL, 0);
    slip_queue(req, len);
    seq_now = seq;
    dev_now = req[0];
    cmd_now = len > 1 ? req[1] : 0;
    awaiting = true;
    deadline = make_timeout_time_ms(REPLY_TIMEOUT_MS);
    rx_len = 0;
    rx_esc = rx_overflow = false;
    publish_status();
}

static void palm_log_char(uint8_t c) {
    if (c == '\n' || palm_log_used == sizeof palm_log - 1) {
        palm_log[palm_log_used] = 0;
        console_printf("palm: %s\n", palm_log);
        palm_log_used = 0;
    }
    if (c != '\n') palm_log[palm_log_used++] = (char)c;
}

static void control(uint8_t value) {
    switch (value) {
    case SBN_CTL_ABORT:
        if (awaiting) publish_reply(seq_now, SBN_RES_ABORTED, NULL, 0);
        tx_len = tx_pos = 0;
        break;
    case SBN_CTL_LOOPBACK_ON: loopback = true; break;
    case SBN_CTL_LOOPBACK_OFF: loopback = false; break;
    }
    console_printf("control %u\n", value);
    publish_status();
}

static void handle_token(uint32_t snapshot) {
    uint32_t offset = BUS_TOKEN_OFFSET(snapshot);
    uint8_t type = (uint8_t)(offset >> 9 & 7), byte = (uint8_t)(offset >> 1);
    ++tokens;
    if (trace) console_printf("token %u %02x (%08lx)\n", type, byte, (unsigned long)snapshot);
    switch (type) {
    case SBN_TK_BEGIN: req_len = 0; overflowed = 0; break;
    case SBN_TK_BYTE:
        if (req_len < SBN_REQ_MAX) req[req_len++] = byte;
        else overflowed = 1;
        break;
    case SBN_TK_DOORBELL: doorbell(byte); break;
    case SBN_TK_CONTROL: control(byte); break;
    case SBN_TK_LOG: palm_log_char(byte); break;
    case SBN_TK_ECHO: reg(SBN_R_ECHO, byte); break;
    default: ++unknown; break;
    }
    reg(SBN_R_TOKENS, (uint16_t)tokens);
    reg(SBN_R_REQ_BYTES, (uint16_t)req_len);
}

static void drain_tokens(void) {
    uint32_t head = bus_token_head();
    uint32_t fill = (head - token_tail) % BUS_TOKEN_RING_WORDS;
    if (fill > token_peak) token_peak = fill;
    while (token_tail != head) {
        handle_token(bus_token_ring[token_tail]);
        token_tail = (token_tail + 1) % BUS_TOKEN_RING_WORDS;
    }
}

/* NIO answers with the request's device and command (command 0 when the
 * device is missing), so a late reply to an abandoned request is not taken
 * for the current one. */
static bool reply_matches(void) {
    return rx_len >= 2 && rx[0] == dev_now && (rx[1] == cmd_now || rx[1] == 0);
}

static void slip_byte(uint8_t c) {
    if (c == SLIP_END) {
        if (rx_len && awaiting && reply_matches()) {
            if (rx_overflow) publish_reply(seq_now, SBN_RES_TOO_BIG, NULL, 0);
            else publish_reply(seq_now, SBN_RES_OK, rx, rx_len);
        } else if (rx_len) {
            ++frames_dropped;
            console_printf("dropped %lu-byte frame with no request waiting\n", (unsigned long)rx_len);
        }
        rx_len = 0;
        rx_esc = rx_overflow = false;
        return;
    }
    if (c == SLIP_ESC) { rx_esc = true; return; }
    if (rx_esc) {
        c = c == SLIP_ESC_END ? SLIP_END : c == SLIP_ESC_ESC ? SLIP_ESC : c;
        rx_esc = false;
    }
    if (rx_len < SBN_RESP_MAX) rx[rx_len++] = c;
    else rx_overflow = true;
}

static void pump_usb(void) {
    bool now = tud_cdc_n_connected(NIO_ITF);
    if (now != host) {
        host = now;
        console_printf("NIO link %s\n", host ? "open" : "closed");
        publish_status();
    }
    while (tx_pos < tx_len) {
        uint32_t room = tud_cdc_n_write_available(NIO_ITF);
        if (!room) break;
        uint32_t n = tx_len - tx_pos;
        tx_pos += tud_cdc_n_write(NIO_ITF, tx + tx_pos, n < room ? n : room);
    }
    tud_cdc_n_write_flush(NIO_ITF);
    uint8_t buf[64];
    while (tud_cdc_n_available(NIO_ITF)) {
        uint32_t n = tud_cdc_n_read(NIO_ITF, buf, sizeof buf);
        for (uint32_t i = 0; i < n; ++i) slip_byte(buf[i]);
    }
    if (awaiting && time_reached(deadline)) publish_reply(seq_now, SBN_RES_TIMEOUT, NULL, 0);
}

void mailbox_init(void) {
    for (uint32_t i = SBN_REGS / 2; i < BUS_TABLE_WORDS; ++i) bus_rom_words[i] = 0;
    reg(SBN_R_MAGIC, SBN_MAGIC_VALUE);
    reg(SBN_R_VERSION, SBN_ABI_VERSION);
    publish_status();
}

void mailbox_poll(void) {
    static uint32_t last_ms;
    drain_tokens();
    pump_usb();
    /* Once a millisecond: Core0 stays off the table while Core1 reads it. */
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    if (ms != last_ms) {
        last_ms = ms;
        reg(SBN_R_TICKS, (uint16_t)ms);
    }
}

uint32_t mailbox_chip_reset;

static const char *reset_reason(void) {
    uint32_t r = mailbox_chip_reset;
    if (r & POWMAN_CHIP_RESET_HAD_BOR_BITS) return "brown-out";
    if (r & POWMAN_CHIP_RESET_HAD_POR_BITS) return "power-on";
    if (r & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) return "RUN pin";
    if (r & POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS) return "glitch detector";
    if (r & (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
             POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS | POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS))
        return "watchdog/reboot";
    if (r & POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS) return "debugger";
    return "unknown";
}

static void status(void) {
    struct bus_totals t;
    uint32_t cs0, cs1;
    bus_get_totals(&t);
    bus_select_counts(&cs0, &cs1);
    /* Core1 publishes its own totals only when it stops; the PIO2 edge
     * counters are live. */
    console_printf("up %lu s, last reset: %s\n",
                   (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000), reset_reason());
    console_printf("bus %s, CS0 cycles %lu, CS1 cycles %lu (runs %lu, fault %lu)\n",
                   bus_is_active() ? "active" : "stopped", (unsigned long)cs0,
                   (unsigned long)cs1, (unsigned long)t.runs, (unsigned long)t.fault);
    console_printf("tokens %lu unknown %lu ring-peak %lu, request %lu bytes\n",
                   (unsigned long)tokens, (unsigned long)unknown, (unsigned long)token_peak,
                   (unsigned long)req_len);
    console_printf("nio %s, %s, loopback %s, dropped frames %lu\n", host ? "open" : "closed",
                   awaiting ? "waiting" : "idle", loopback ? "on" : "off",
                   (unsigned long)frames_dropped);
}

static void pins(void) {
    uint32_t lo = sio_hw->gpio_in, hi = sio_hw->gpio_hi_in;
    uint32_t a = (lo >> 1 & 0x7fffu) | (hi >> 4 & 7u) << 15 | (hi >> 8 & 0x1fu) << 18;
    uint32_t d = (lo >> 20 & 0xfffu) | (hi & 0xfu) << 12;
    console_printf("CS0 %lu CS1 %lu OE %lu WE %lu RESET %lu LOWBAT %lu IRQ %lu A %06lx D %04lx\n",
                   lo >> 16 & 1, lo >> 17 & 1, lo >> 18 & 1, lo >> 19 & 1, hi >> 13 & 1,
                   hi >> 14 & 1, lo & 1, (unsigned long)a << 1, (unsigned long)d);
}

/* Count level changes on the control lines for 200 ms. A seated module on a
 * running Prism shows OE toggling constantly (it is shared with the Prism's
 * own flash); a dark or badly seated slot shows nothing. */
static void watch(void) {
    static const uint8_t pin[] = {16, 17, 18, 19, 45, 46, 0};
    static const char *const name[] = {"CS0", "CS1", "OE", "WE", "RESET", "LOWBAT", "IRQ"};
    uint32_t changes[7] = {0}, lows[7] = {0}, samples = 0;
    uint64_t prev = gpio_get_all64(), end = time_us_64() + 200000;
    while (time_us_64() < end) {
        uint64_t now = gpio_get_all64(), diff = now ^ prev;
        for (unsigned i = 0; i < 7; ++i) {
            changes[i] += (uint32_t)(diff >> pin[i] & 1);
            lows[i] += (uint32_t)(~now >> pin[i] & 1);
        }
        prev = now;
        ++samples;
    }
    for (unsigned i = 0; i < 7; ++i)
        console_printf("%-6s changes %7lu  low %3lu%%\n", name[i], (unsigned long)changes[i],
                       (unsigned long)(100ull * lows[i] / samples));
}

/* Count level changes on every GPIO for the given milliseconds, to see which
 * pins move while the Palm does something. */
static void scan(const char *arg) {
    uint32_t ms = (uint32_t)atoi(arg);
    uint32_t changes[48] = {0};
    if (!ms || ms > 20000) ms = 3000;
    uint64_t prev = gpio_get_all64(), end = time_us_64() + 1000ull * ms;
    while (time_us_64() < end) {
        uint64_t now = gpio_get_all64(), diff = now ^ prev;
        while (diff) {
            unsigned pin = (unsigned)__builtin_ctzll(diff);
            ++changes[pin];
            diff &= diff - 1;
        }
        prev = now;
    }
    console_printf("scan %lu ms:", (unsigned long)ms);
    for (unsigned pin = 0; pin < 48; ++pin)
        if (changes[pin]) console_printf(" GP%u=%lu", pin, (unsigned long)changes[pin]);
    console_printf("\n");
}

/* With the bus engine stopped, pull each pin down with its input buffer off
 * (RP2350-E9 leakage otherwise holds a floating pad near 2 V), then read it.
 * A line the Prism drives high stays 1; an unconnected one reads 0. */
static void float_test(void) {
    static const uint8_t pin[] = {16, 17, 18, 19, 20, 45};
    static const char *const name[] = {"CS0", "CS1", "OE", "WE", "D0", "RESET"};
    if (bus_is_active()) {
        console_printf("float: run \"stop\" first\n");
        return;
    }
    for (unsigned i = 0; i < 6; ++i) {
        unsigned p = pin[i], ones = 0;
        uint32_t saved = pads_bank0_hw->io[p];
        for (unsigned k = 0; k < 8; ++k) {
            hw_clear_bits(&pads_bank0_hw->io[p], PADS_BANK0_GPIO0_IE_BITS);
            hw_set_bits(&pads_bank0_hw->io[p], PADS_BANK0_GPIO0_PDE_BITS);
            busy_wait_us(200);
            hw_set_bits(&pads_bank0_hw->io[p], PADS_BANK0_GPIO0_IE_BITS);
            busy_wait_at_least_cycles(20);
            ones += gpio_get(p);
        }
        pads_bank0_hw->io[p] = saved;
        console_printf("%-5s pulled down: high %u/8, %s\n", name[i], ones,
                       ones == 8 ? "driven high" : ones ? "toggling" : "floating or held low");
    }
}

/* "send <hex>": queue a raw FujiBus packet as if the Palm had sent it, under
 * sequence 0, which the Palm never uses. */
static void send_hex(const char *hex) {
    uint32_t n = 0;
    while (hex[0] && hex[1] && n < SBN_REQ_MAX) {
        unsigned v;
        if (sscanf(hex, "%2x", &v) != 1) break;
        req[n++] = (uint8_t)v;
        hex += 2;
        while (*hex == ' ') ++hex;
    }
    req_len = n;
    doorbell(0);
}

void mailbox_command(const char *line) {
    if (!strcmp(line, "status")) status();
    else if (!strcmp(line, "pins")) pins();
    else if (!strcmp(line, "watch")) watch();
    else if (!strncmp(line, "scan", 4)) scan(line + 4);
    else if (!strcmp(line, "float")) float_test();
    else if (!strncmp(line, "send ", 5)) send_hex(line + 5);
    else if (!strcmp(line, "regs")) {
        for (unsigned r = SBN_R_MAGIC; r <= SBN_R_TICKS; r += 2)
            console_printf("%04x: %04x\n", r, bus_rom_words[r / 2]);
    } else if (!strcmp(line, "trace on")) trace = true;
    else if (!strcmp(line, "trace off")) trace = false;
    else if (!strcmp(line, "loopback on")) { loopback = true; publish_status(); }
    else if (!strcmp(line, "loopback off")) { loopback = false; publish_status(); }
    else if (!strcmp(line, "stop")) bus_stop();
    else if (!strcmp(line, "arm")) bus_rearm();
    else if (!strcmp(line, "bootsel")) reset_usb_boot(0, 0);
    else console_printf("commands: status pins watch scan [ms] float regs send <hex> "
                        "trace on|off loopback on|off stop arm bootsel\n");
}
