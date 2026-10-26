/*
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal, dependency-free bit-banged 1-Wire master for RP2040 / RP2350.
 *
 * - Any GPIO can be a bus; several buses can coexist (one ow_bus_t each).
 * - Open-drain is emulated by switching the pin between "input" (released,
 *   pulled high by the external resistor) and "output low".
 * - Only the time-critical part of each slot runs with interrupts disabled
 *   (max. ~65 us), so USB stdio keeps working.
 * - "Strong pull-up" (actively driving the line high after a command, needed
 *   for parasite-powered sensors) is supported via the `power` argument of
 *   ow_write_byte() and is cancelled by ow_depower() or any later bus access.
 *
 * Timing follows Maxim application note AN126 ("standard speed").
 * Search follows Maxim application note AN187.
 */
#ifndef DSCD_ONEWIRE_H
#define DSCD_ONEWIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/types.h"

/* ROM commands */
#define OW_CMD_SEARCH_ROM     0xF0
#define OW_CMD_ALARM_SEARCH   0xEC
#define OW_CMD_MATCH_ROM      0x55
#define OW_CMD_SKIP_ROM       0xCC

typedef enum {
    OW_RESET_PRESENCE = 0,  /* at least one device answered */
    OW_RESET_NO_DEVICE,     /* bus idle, nobody answered */
    OW_RESET_BUS_LOW,       /* line never went high: short circuit or missing pull-up */
} ow_reset_result_t;

typedef struct {
    uint    pin;
    /* search state (AN187) */
    uint8_t rom[8];
    int     last_discrepancy;
    bool    last_device;
} ow_bus_t;

void ow_init(ow_bus_t *bus, uint pin, bool internal_pullup);

ow_reset_result_t ow_reset_ex(ow_bus_t *bus);
/* Arduino-OneWire compatible: true if a presence pulse was detected. */
static inline bool ow_reset(ow_bus_t *bus) { return ow_reset_ex(bus) == OW_RESET_PRESENCE; }

void    ow_write_bit(ow_bus_t *bus, bool bit);
bool    ow_read_bit(ow_bus_t *bus);
void    ow_write_byte(ow_bus_t *bus, uint8_t value, bool power);
uint8_t ow_read_byte(ow_bus_t *bus);
void    ow_depower(ow_bus_t *bus);

void ow_select(ow_bus_t *bus, const uint8_t rom[8]);
void ow_skip(ow_bus_t *bus);

void ow_reset_search(ow_bus_t *bus);
/* Returns the next ROM on the bus. alarm_only = true issues ALARM SEARCH (0xEC). */
bool ow_search(ow_bus_t *bus, uint8_t rom[8], bool alarm_only);

/* Dallas/Maxim CRC-8 (x^8 + x^5 + x^4 + 1). Returns 0 for a valid block incl. CRC byte. */
uint8_t ow_crc8(const uint8_t *data, size_t len);

#endif /* DSCD_ONEWIRE_H */
