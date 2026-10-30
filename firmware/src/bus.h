#pragma once
#include <stdbool.h>
#include <stdint.h>

/* CS0 serves the ROM table, CS1 the register table that follows it. Both are
 * indexed by A1-A15, so each holds 32768 big-endian words. */
#define BUS_TABLE_WORDS 32768u
extern uint16_t *const bus_rom_words;
extern uint16_t *const bus_cs1_words;

/* Every CS0 read in the token window (A12-A15 set) is logged as one
 * GPIO0-31 snapshot; bits 1-15 hold A1-A15, i.e. the byte offset. */
#define BUS_TOKEN_RING_WORDS 2048u
#define BUS_TOKEN_OFFSET(w) ((w) & 0xfffeu)
extern volatile uint32_t bus_token_ring[BUS_TOKEN_RING_WORDS];

struct bus_totals {
    uint32_t runs, requests, posted, completed, attempted, cancelled, fault;
};

void bus_pins_init(void);
void bus_load_rom(const uint8_t *rom, uint32_t size);
void bus_token_capture_init(void);
uint32_t bus_token_head(void);
void bus_select_counts(uint32_t *cs0, uint32_t *cs1);
void bus_launch_core1(void);
void bus_stop(void);
void bus_rearm(void);
bool bus_is_active(void);
void bus_get_totals(struct bus_totals *out);
