/*
 * Copyright 2024 Chris Petrich (original Arduino sketch)
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors (Pico SDK port)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * DS18B20 authenticity tests, ported from
 * https://github.com/cpetrich/counterfeit_DS18B20 (discover-classify_fake_DS18B20.ino,
 * version 9 Nov 2024). The test logic and thresholds are unchanged.
 */
#ifndef DSCD_DS18B20_TESTS_H
#define DSCD_DS18B20_TESTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "onewire.h"

typedef enum {
    DS_VERDICT_GENUINE = 0,   /* responded like a genuine Maxim/ADI DS18B20 */
    DS_VERDICT_CLONE,         /* one or more deviations from the datasheet  */
    DS_VERDICT_INCONCLUSIVE,  /* parasite power or temperature test not done */
    DS_VERDICT_COMM_ERROR,    /* scratchpad CRC error: wiring problem        */
} ds_verdict_t;

typedef struct {
    uint         pin;
    uint8_t      rom[8];
    ds_verdict_t verdict;
    int          deviations;
    bool         rom_crc_ok;
    bool         parasite_power;
    bool         temp_test_ok;
    bool         temp_valid;
    float        temp_c;
} ds_result_t;

/* Menu item 0: list sensors, ROM, scratchpad and temperature. Returns sensor count. */
int ds_test_enumerate(ow_bus_t *bus);

/* Menu item 1: verify the TH/TL alarm function (documented commands only). */
void ds_test_alarm(ow_bus_t *bus);

/* Menu item 2: safe test using only documented function codes.
 * Fills up to max_results entries and returns the number of sensors tested. */
size_t ds_test_discover(ow_bus_t *bus, ds_result_t *results, size_t max_results);

/* Menu item 3: AGGRESSIVE classification using undocumented function codes.
 * Could conceivably change calibration of clones. Use on test sensors only. */
void ds_test_classify(ow_bus_t *bus);

const char *ds_verdict_name(ds_verdict_t verdict);
void ds_print_rom(const uint8_t rom[8]);

#endif /* DSCD_DS18B20_TESTS_H */
