/*
 * Copyright 2024 Chris Petrich (original Arduino sketch)
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors (Pico SDK port)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ---------------------------------------------------------------------------
 * Port of discover-classify_fake_DS18B20.ino (C. Petrich, version 9 Nov 2024)
 * from Arduino/OneWire to the Raspberry Pi Pico SDK.
 *
 * Changes relative to the original (Apache-2.0 section 4(b) notice):
 *  - Arduino/OneWire API replaced by the onewire.c driver; Serial -> printf.
 *  - Each test runs on one bus (ow_bus_t); the caller iterates over buses.
 *  - loop_discover() returns a structured result (verdict, deviation count)
 *    so the firmware can drive the status LED and print a summary.
 *  - Conversion times are measured in microseconds instead of milliseconds.
 *  - Alarm test: temperature-to-integer conversion is sign-correct below 0 C;
 *    the test stops early if no sensor is found.
 *  - Discover test explicitly restores 12-bit resolution and the original
 *    alarm bytes in the scratchpad when it finishes (EEPROM is not touched).
 *  - Family C test restructured without goto. Test logic and thresholds of
 *    all tests are otherwise unchanged.
 * ---------------------------------------------------------------------------
 *
 * The tests are INTENDED for EDUCATIONAL PURPOSES only. There may be
 * circumstances under which they permanently damage 1-Wire sensors in obvious
 * or non-obvious ways. Tests 0, 1 and 2 use only documented commands.
 * Test 3 sends undocumented commands and could conceivably mess up the
 * calibration parameters of clones.
 */
#include "ds18b20_tests.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "config.h"

#define MS750 DSCD_CONVERSION_MS

/* DS18B20 function codes (documented) */
#define DS_CONVERT_T        0x44
#define DS_WRITE_SCRATCHPAD 0x4E
#define DS_READ_SCRATCHPAD  0xBE
#define DS_RECALL_EEPROM    0xB8
#define DS_READ_POWER       0xB4

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */

static inline uint32_t millis(void) {
    return to_ms_since_boot(get_absolute_time());
}

static inline int16_t min16(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

static void print_hex(uint8_t value) {
    printf("%02X", value);
}

static void print_array(const uint8_t *data, int n, char sep) {
    for (int i = 0; i < n; i++) {
        print_hex(data[i]);
        if (i != n - 1) {
            putchar(sep);
        }
    }
}

void ds_print_rom(const uint8_t rom[8]) {
    print_array(rom, 8, '-');
}

/* address in Linux w1 subsystem format, i.e. without CRC */
static void print_w1_address(const uint8_t *addr) {
    print_hex(addr[0]);
    putchar('-');
    for (int i = 6; i > 0; i--) {
        print_hex(addr[i]);
    }
}

static inline int16_t raw_temp(const uint8_t *sp) {
    return (int16_t)((uint16_t)sp[0] | ((uint16_t)sp[1] << 8));
}

static inline float raw_to_celsius(const uint8_t *sp) {
    return (float)raw_temp(sp) / 16.0f;
}

static bool read_scratchpad(ow_bus_t *bus, const uint8_t *addr, uint8_t *buff9) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, DS_READ_SCRATCHPAD, false);
    for (int i = 0; i < 9; i++) {
        buff9[i] = ow_read_byte(bus);
    }
    return ow_crc8(buff9, 9) == 0;
}

/* The original sketch retries a failed scratchpad read once. */
static bool read_scratchpad_retry(ow_bus_t *bus, const uint8_t *addr, uint8_t *buff9) {
    if (read_scratchpad(bus, addr, buff9)) {
        return true;
    }
    return read_scratchpad(bus, addr, buff9);
}

static void write_scratchpad(ow_bus_t *bus, const uint8_t *addr, uint8_t th, uint8_t tl, uint8_t cfg) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, DS_WRITE_SCRATCHPAD, false);
    ow_write_byte(bus, th, false);
    ow_write_byte(bus, tl, false);
    ow_write_byte(bus, cfg, false);
}

/* true = sensor reports external (normal) power, false = parasite power */
static bool reports_normal_power(ow_bus_t *bus, const uint8_t *addr) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, DS_READ_POWER, false);
    return ow_read_bit(bus);
}

static void convert_all(ow_bus_t *bus, uint32_t wait_ms) {
    ow_reset(bus);
    ow_skip(bus);
    ow_write_byte(bus, DS_CONVERT_T, true); /* strong pull-up for parasite power */
    sleep_ms(wait_ms);
    ow_depower(bus);
}

const char *ds_verdict_name(ds_verdict_t verdict) {
    switch (verdict) {
    case DS_VERDICT_GENUINE:      return "GENUINE";
    case DS_VERDICT_CLONE:        return "CLONE";
    case DS_VERDICT_INCONCLUSIVE: return "INCONCLUSIVE";
    case DS_VERDICT_COMM_ERROR:   return "COMM ERROR";
    default:                      return "?";
    }
}

/* ------------------------------------------------------------------------ */
/* 0. Enumerate                                                              */
/* ------------------------------------------------------------------------ */

int ds_test_enumerate(ow_bus_t *bus) {
    uint8_t addr[8];
    uint8_t buffer[9];
    int count = 0;

    printf("Sensor ROM and Current Scratchpad Content:\n");
    ow_reset_search(bus);
    while (ow_search(bus, addr, false)) {
        read_scratchpad_retry(bus, addr, buffer);
        count++;
        printf("  %2d. ", count);
        print_array(addr, 8, '-');
        printf(", ");
        print_w1_address(addr);
        if (!reports_normal_power(bus, addr)) {
            printf(" (Parasite Power Mode)");
        }
        printf(": ");
        print_array(buffer, 9, '/');
        printf(" %.2f oC\n", (double)raw_to_celsius(buffer));
    }
    printf("  Number of Sensors: %d.\n\n", count);
    return count;
}

/* ------------------------------------------------------------------------ */
/* 1. Alarm function                                                         */
/* ------------------------------------------------------------------------ */

static void alarm_set_all(ow_bus_t *bus, uint8_t alarm_h, uint8_t alarm_l, int expected_count) {
    uint8_t addr[8];
    uint8_t buffer[9];
    int count = 0;

    ow_reset_search(bus);
    while (ow_search(bus, addr, false)) {
        write_scratchpad(bus, addr, alarm_h, alarm_l, 0x7F); /* 12-bit */
        printf("  ");
        print_array(addr, 8, '-');
        printf(": ");
        read_scratchpad_retry(bus, addr, buffer);
        print_array(&buffer[2], 3, '/');
        if (buffer[2] == alarm_h && buffer[3] == alarm_l && buffer[4] == 0x7F) {
            printf(" Ok.\n");
        } else {
            printf(" Unsuccessful Write. Error.\n");
        }
        count++;
    }
    if (expected_count >= 0 && count != expected_count) {
        printf("  Inconsistent Sensor Count, Check Connections!\n\n");
    }
}

/* returns number of sensors that answered the ALARM SEARCH */
static int alarm_search_count(ow_bus_t *bus) {
    uint8_t addr[8];
    uint8_t buffer[9];
    int count = 0;

    ow_reset_search(bus);
    while (ow_search(bus, addr, true)) {
        read_scratchpad_retry(bus, addr, buffer);
        printf("  ");
        print_array(addr, 8, '-');
        printf(": ");
        print_array(buffer, 9, '/');
        printf(" %.2f oC\n", (double)raw_to_celsius(buffer));
        count++;
    }
    printf("  Number of Sensors: %d.", count);
    return count;
}

void ds_test_alarm(ow_bus_t *bus) {
    uint8_t addr[8];
    uint8_t buffer[9];
    int16_t current_min_t, current_max_t; /* integer degrees C */
    const int16_t window_t = 3; /* allowance for temperature change; >= 1 if resolution > 9 bit */
    int attached = 0;
    bool error_found = false;

    /* Thresholds are set from the current temperature rather than hard-coded
     * so we can verify that the chips actually compare. Temperatures need to
     * be reasonably constant during this test. As of 2024, no sensors were
     * known to fail it. */

    printf("1. Trigger Temperature Conversion of All Sensors\n");
    convert_all(bus, MS750);

    printf("2. Find Current Temperature Range\n");
    current_min_t = 127;
    current_max_t = -128;
    ow_reset_search(bus);
    while (ow_search(bus, addr, false)) {
        read_scratchpad_retry(bus, addr, buffer);
        const int16_t t_int = (int16_t)(raw_temp(buffer) >> 4); /* floor, sign-correct */
        current_min_t = min16(current_min_t, t_int);
        current_max_t = max16(current_max_t, t_int);
        printf("  ");
        print_array(addr, 8, '-');
        printf(": ");
        print_array(buffer, 9, '/');
        printf(" %.2f oC\n", (double)raw_to_celsius(buffer));
        attached++;
    }
    printf("  Number of Sensors: %d.\n\n", attached);
    if (attached == 0) {
        printf("No sensors detected.\n");
        return;
    }

    {
        printf("3. Set Alarm Registers to Trigger High Temperature Alarm\n");
        const uint8_t alarm_h = (uint8_t)((current_min_t - window_t) & 0xFF);
        const uint8_t alarm_l = (uint8_t)(alarm_h - 1);
        alarm_set_all(bus, alarm_h, alarm_l, attached);
    }

    printf("4. Trigger Temperature Conversion of All Sensors\n");
    convert_all(bus, MS750);

    {
        printf("5. Find Sensors Signalling Alarm\n");
        printf("   (this should be all sensors)\n");
        const int count = alarm_search_count(bus);
        if (count == attached) {
            printf(" Ok.\n");
        } else if (count < attached) {
            printf("  ** Not all sensors raised alarm!!! **\n");
            error_found = true;
        } else {
            printf("  Inconsistent sensor count, check connections!\n");
        }
        printf("\n");
    }

    {
        printf("6. Set Alarm Registers to Trigger Low Temperature Alarm\n");
        const uint8_t alarm_l = (uint8_t)((current_max_t + window_t) & 0xFF);
        const uint8_t alarm_h = (uint8_t)(alarm_l + 1);
        alarm_set_all(bus, alarm_h, alarm_l, attached);
    }

    printf("7. Trigger Temperature Conversion of All Sensors\n");
    convert_all(bus, MS750);

    {
        printf("8. Find Sensors Signalling Alarm\n");
        printf("   (this should be all sensors)\n");
        const int count = alarm_search_count(bus);
        if (count == attached) {
            printf(" Ok.\n");
        } else if (count < attached) {
            printf("  ** Not all sensors raised alarm!!! **\n");
            error_found = true;
        } else {
            printf("  Inconsistent sensor count, check connections!\n");
        }
        printf("\n");
    }

    {
        printf("9. Set Alarm Registers to Not Trigger\n");
        const uint8_t alarm_l = (uint8_t)((current_min_t - window_t) & 0xFF);
        const uint8_t alarm_h = (uint8_t)((current_max_t + window_t) & 0xFF);
        alarm_set_all(bus, alarm_h, alarm_l, -1);
        printf("\n");
    }

    printf("10. Trigger Temperature Conversion of All Sensors\n");
    convert_all(bus, MS750);

    {
        printf("11. Find Sensors Signalling Alarm\n");
        printf("   (this should be no sensors)\n");
        const int count = alarm_search_count(bus);
        if (count == 0) {
            printf(" Ok.\n");
        } else {
            printf("  ** Sensors raised alarm even though they should not!!! **\n");
            error_found = true;
        }
        printf("\n");
    }

    printf("12. Request All Sensors to Recall EEPROM\n");
    ow_reset(bus);
    ow_skip(bus);
    ow_write_byte(bus, DS_RECALL_EEPROM, false);
    sleep_ms(10);

    printf("\n");
    if (error_found) {
        printf("** Test Revealed Alarm Implementation Errors **\n");
        printf("   (assuming sensor temperatures did not change too much)\n");
    } else {
        printf("No Alarm Implementation Errors Found\n");
    }
    printf("\n");
}

/* ------------------------------------------------------------------------ */
/* 2. Discover (safe)                                                        */
/* ------------------------------------------------------------------------ */

static bool is_power_up_or_failed_value(const uint8_t *sp) {
    return (sp[0] == 0x50 && sp[1] == 0x05) || (sp[0] == 0xFF && sp[1] == 0x07);
}

/* Runs all safe checks on one sensor. Fills `r` and prints a detailed log. */
static void discover_one(ow_bus_t *bus, const uint8_t *addr, ds_result_t *r) {
    uint8_t buffer0[9], buffer1[9], buffer2[9], buffer3[9];
    int fake_flags = 0;
    bool t_ok = false;

    memset(r, 0, sizeof(*r));
    r->pin = bus->pin;
    memcpy(r->rom, addr, 8);

    print_array(addr, 8, '-');
    r->rom_crc_ok = (ow_crc8(addr, 8) == 0);
    if (!r->rom_crc_ok) {
        /* some clones can have their ROM overwritten with arbitrary nonsense */
        fake_flags++;
        printf(" (CRC Error -> Error.)");
    }

    if (addr[6] != 0 || addr[5] != 0 || addr[0] != 0x28) {
        /* as of 2024: catches all families but A1 and A3 */
        fake_flags++;
        printf(": ROM does not follow expected pattern 28-xx-xx-xx-xx-00-00-crc. Error.");
    } else if (addr[4] == 0) {
        /* catches Family A3 */
        fake_flags++;
        printf(": ROM pattern pre-dates C4 die version, suggesting the chip is either"
               " over 15 years old or a clone.\n                         Assuming sensor is a clone. Error.");
    } else {
        printf(": ROM ok.");
    }
    printf("\n");

    read_scratchpad_retry(bus, addr, buffer0);
    printf("  Scratchpad Register: ");
    print_array(buffer0, 9, '/');
    if (ow_crc8(buffer0, 9) != 0) {
        /* Most likely a bad connection rather than a bad sensor. */
        printf("  CRC Error. Check connections or replace sensor.\n");
        r->verdict = DS_VERDICT_COMM_ERROR;
        r->deviations = fake_flags;
        return;
    }
    printf("\n");

    r->parasite_power = !reports_normal_power(bus, addr);
    printf("  Info only: ");
    if (!r->parasite_power) {
        printf("Sensor is not reporting Parasite Power Mode. Ok.\n");
    } else {
        printf("Sensor is reporting to be in Parasite Power Mode.\n"
               "             Note that some parts of this need the sensor\n"
               "             to not be in Parasite Power Mode. Hence:\n"
               "             ** THE RESULT OF THIS TEST IS INVALID **\n");
    }

    /* EEPROM content may have been programmed by the user: info only. */
    printf("  Info only: Scratchpad bytes 2,3,4 (");
    print_array(buffer0 + 2, 3, '/');
    printf("):");
    if (buffer0[2] != 0x4B || buffer0[3] != 0x46 || buffer0[4] != 0x7F) {
        printf(" not DS18B20 default values 4B/46/7F.\n");
    } else {
        printf(" DS18B20 default values.\n");
    }

    printf("  Scratchpad <byte 5> = 0x");
    print_hex(buffer0[5]);
    printf(":");
    if (buffer0[5] != 0xFF) {
        /* potentially catches Families B1 (not B1v2), B2, D1, D2 */
        fake_flags++;
        printf(" should have been 0xFF according to datasheet. Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  Scratchpad <byte 6> = 0x");
    print_hex(buffer0[6]);
    printf(":");
    if ((buffer0[6] == 0x00 || buffer0[6] > 0x10) ||
        (!is_power_up_or_failed_value(buffer0) && buffer0[6] != (uint8_t)(0x10 - (buffer0[0] & 0x0F)))) {
        /* typically catches B1 (not B1v2), B2, C, D1, D2, F, G after a conversion */
        fake_flags++;
        printf(" unexpected value. Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  Scratchpad <byte 7> = 0x");
    print_hex(buffer0[7]);
    printf(":");
    if (buffer0[7] != 0x10) {
        /* may catch B1, B2, D1, D2 (not B1v2) */
        fake_flags++;
        printf(" should have been 0x10 according to datasheet. Error.\n");
    } else {
        printf(" ok.\n");
    }

    {
        /* send 5 bytes into scratchpad and read back */
        ow_reset(bus);
        ow_select(bus, addr);
        ow_write_byte(bus, DS_WRITE_SCRATCHPAD, false); /* MUST be followed by 3 bytes */
        ow_write_byte(bus, buffer0[2], false);
        ow_write_byte(bus, buffer0[3], false);
        ow_write_byte(bus, 0x7F, false);
        ow_write_byte(bus, 0x11, false);
        ow_write_byte(bus, 0x22, false);
        ow_reset(bus);
        read_scratchpad_retry(bus, addr, buffer1);
        printf("  Last three bytes of scratchpad after sending 5 bytes: ");
        print_array(&buffer1[5], 3, '/');
        /* bytes 5 and 7 are fixed, byte 6 must not change */
        if (buffer1[5] != 0xFF || buffer1[6] != buffer0[6] || buffer1[7] != 0x10) {
            /* catches B1 (not B1v2) and B2 */
            fake_flags++;
            printf(" unexpected (expected FF/<unchanged>/10). Error.\n");
        } else {
            printf(" ok.\n");
        }
    }

    {
        write_scratchpad(bus, addr, buffer0[2], buffer0[3], 0x00);
        ow_reset(bus);
        read_scratchpad_retry(bus, addr, buffer1);
        printf("  Scratchpad config register <byte 4> after writing 0x00: 0x");
        print_hex(buffer1[4]);
        if (buffer1[4] != 0x1F) {
            /* catches Families C and F */
            fake_flags++;
            printf(" unexpected (expected 1F). Error.\n");
        } else {
            printf(" ok.\n");
        }

        write_scratchpad(bus, addr, buffer0[2], buffer0[3], 0xFF);
        ow_reset(bus);
        read_scratchpad_retry(bus, addr, buffer1);
        printf("  Scratchpad config register <byte 4> after writing 0xFF: 0x");
        print_hex(buffer1[4]);
        if (buffer1[4] != 0x7F) {
            /* catches Family F */
            fake_flags++;
            printf(" unexpected (expected 7F). Error.\n");
        } else {
            printf(" ok.\n");
        }
    }

    /* set the resolution to 10 bit and modify alarm registers */
    write_scratchpad(bus, addr, buffer0[2] ^ 0xFF, buffer0[3] ^ 0xFF, 0x3F);
    ow_reset(bus);
    read_scratchpad_retry(bus, addr, buffer1);

    printf("  0x4E modifies alarm registers:");
    if (buffer1[2] != (uint8_t)(buffer0[2] ^ 0xFF) || buffer1[3] != (uint8_t)(buffer0[3] ^ 0xFF)) {
        fake_flags++;
        printf(" cannot modify content as expected (want: ");
        print_hex(buffer0[2] ^ 0xFF);
        putchar('/');
        print_hex(buffer0[3] ^ 0xFF);
        printf(", got: ");
        print_array(buffer1 + 2, 2, '/');
        printf("). Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  0x4E accepts 10 bit resolution:");
    if (buffer1[4] != 0x3F) {
        /* catches Families C, F */
        fake_flags++;
        printf(" rejected (expected: 0x3F, got: 0x");
        print_hex(buffer1[4]);
        printf("). Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  0x4E preserves reserved bytes:");
    if (buffer1[5] != buffer0[5] || buffer1[6] != buffer0[6] || buffer1[7] != buffer0[7]) {
        /* may catch B1, B2, D1, D2 (not B1v2) */
        fake_flags++;
        printf(" no, got: ");
        print_array(buffer1 + 5, 3, '/');
        printf(". Error.\n");
    } else {
        printf(" ok.\n");
    }

    /* set the resolution to 12 bit */
    write_scratchpad(bus, addr, buffer0[2], buffer0[3], 0x7F);
    ow_reset(bus);
    read_scratchpad_retry(bus, addr, buffer2);

    printf("  0x4E accepts 12 bit resolution:");
    if (buffer2[4] != 0x7F) {
        /* no sensor should fail this */
        fake_flags++;
        printf(" rejected (expected: 0x7F, got: 0x");
        print_hex(buffer2[4]);
        printf("). Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  0x4E preserves reserved bytes:");
    if (buffer2[5] != buffer1[5] || buffer2[6] != buffer1[6] || buffer2[7] != buffer1[7]) {
        /* Families B1 and B2 would fail this */
        fake_flags++;
        printf(" no, got: ");
        print_array(buffer2 + 5, 3, '/');
        printf(". Error.\n");
    } else {
        printf(" ok.\n");
    }

    printf("  Checking <byte 6> upon temperature change:");
    {
        /* Back-to-back conversions usually warm the sensor slightly, so
         * <byte 6> can be checked at a temperature where it is meaningful. */
        int count = 5;
        do {
            count--;
            if (count < 0) {
                break;
            }
            ow_reset(bus);
            ow_select(bus, addr);
            ow_write_byte(bus, DS_CONVERT_T, true); /* keep line high for parasite power */
            sleep_ms(MS750);
            ow_depower(bus);
            read_scratchpad_retry(bus, addr, buffer3);
        } while (is_power_up_or_failed_value(buffer3) ||
                 (buffer3[6] == 0x0C && ((buffer3[0] + buffer3[6]) & 0x0F) == 0x00));

        if (count < 0) {
            printf(" Inconclusive. Please change sensor temperature and repeat.\n");
            t_ok = false;
        } else {
            t_ok = true;
            const uint8_t expect_value = (uint8_t)(0x10 - (buffer3[0] & 0x0F));
            if (buffer3[6] != 0x0C && buffer3[6] == expect_value) {
                printf(" ok.\n");
            } else {
                fake_flags++;
                printf(" <byte 0> = 0x");
                print_hex(buffer3[0]);
                printf(" but <byte 6> = 0x");
                print_hex(buffer3[6]);
                printf(" rather than 0x");
                print_hex(expect_value);
                printf(". Error.\n");
            }
        }
        r->temp_c = raw_to_celsius(buffer3);
        r->temp_valid = (ow_crc8(buffer3, 9) == 0) && !is_power_up_or_failed_value(buffer3);
        printf("    Info only: Temperature reading is %.2f oC.\n", (double)r->temp_c);
    }

    printf("  Testing if end of temperature conversion can be polled: ");
    bool reports_conversion_progress = true;
    {
        const int short_ms = 3;
        /* sensor should read LOW after 3 ms and HIGH after 750 ms */
        ow_reset(bus);
        ow_select(bus, addr);
        ow_write_byte(bus, DS_CONVERT_T, false);
        sleep_ms(short_ms);
        const bool after_short = ow_read_bit(bus);
        sleep_ms(MS750 - short_ms);
        const bool after_750ms = ow_read_bit(bus);
        if (!after_short && after_750ms) {
            printf("ok.\n");
        } else {
            /* catches Family F */
            fake_flags++;
            printf("\n      Read %s after %d ms (expected LOW), read %s after %d ms (expected HIGH). Error.\n",
                   after_short ? "HIGH" : "LOW", short_ms, after_750ms ? "HIGH" : "LOW", MS750);
            reports_conversion_progress = false;
        }
    }

    printf("    Polling status valid immediately: ");
    if (reports_conversion_progress) {
        const int n_samples = 4;
        int ok_count = 0;
        for (int i = 0; i < n_samples; i++) {
            ow_reset(bus);
            ow_select(bus, addr);
            ow_write_byte(bus, DS_CONVERT_T, false);
            if (!ow_read_bit(bus)) {
                ok_count++;
            }
            sleep_ms(MS750); /* let conversion finish */
        }
        if (ok_count == n_samples) {
            printf("ok.\n");
        } else {
            /* catches Family A3 */
            fake_flags++;
            printf("Sensor failed to signal immediately %d out of %d times. Error.\n",
                   n_samples - ok_count, n_samples);
        }
    } else {
        fake_flags++;
        printf("test not possible. Error.\n");
    }

    printf("    Conversion speed at different resolutions: ");
    if (reports_conversion_progress) {
        const uint32_t timeout_us = 3000u * 1000u;
        int bit_set_ok = 0;
        float conv_ms[4];
        for (int idx = 0; idx < 4; idx++) {
            write_scratchpad(bus, addr, buffer0[2], buffer0[3], (uint8_t)(idx * 32 + 0x1F));
            sleep_ms(1);
            read_scratchpad_retry(bus, addr, buffer2);
            const int true_idx = buffer2[4] / 32;
            if (true_idx == idx) {
                bit_set_ok++;
            }
            printf("%d-bit: ", true_idx + 9);

            ow_reset(bus);
            ow_select(bus, addr);
            ow_write_byte(bus, DS_CONVERT_T, false);
            const uint32_t start = time_us_32();
            sleep_ms(1); /* in case the sensor is slow to pull the line down (e.g. A3) */
            while (!ow_read_bit(bus) && (time_us_32() - start) < timeout_us) {
                /* wait for "done" */
            }
            const uint32_t elapsed_us = time_us_32() - start;
            conv_ms[idx] = (float)elapsed_us / 1000.0f;
            if (elapsed_us < timeout_us) {
                printf("%.1f ms", (double)conv_ms[idx]);
            } else {
                printf("(timeout)");
            }
            if (idx != 3) {
                printf(", ");
            }
        }
        printf(".\n      ");

        bool div_resolution = true;
        if (bit_set_ok != 4) {
            /* catches Families C, F */
            fake_flags++;
            printf("Could not change resolution. Error.\n      ");
            div_resolution = false;
        } else {
            printf("Changing resolution possible. Ok.\n      ");
        }
        if (div_resolution) {
            const float r1 = conv_ms[1] / conv_ms[0];
            const float r2 = conv_ms[2] / conv_ms[1];
            const float r3 = conv_ms[3] / conv_ms[2];
            const float r_avg = (r1 + r2 + r3) / 3.0f;
            if (fabsf(r_avg - 2.0f) < 0.3f) {
                printf("Conversion time doubles with each bit in resolution. Ok.\n      ");
            } else if (fabsf(r_avg - 1.41f) < 0.2f) {
                /* catches Family H */
                fake_flags++;
                printf("Conversion time increases by factor 1.41 with each bit in resolution. Error.\n      ");
            } else if (fabsf(r_avg - 1.0f) < 0.15f) {
                /* catches Families C, D1, D2, E, F */
                fake_flags++;
                printf("Conversion time independent of resolution. Error.\n      ");
            } else {
                fake_flags++;
                printf("Conversion time does not scale as expected with resolution. Error.\n      ");
            }
        }
        if (conv_ms[3] < (float)DSCD_GENUINE_12BIT_MIN_MS) {
            /* catches Families A2, C, D1, D2, E, G */
            fake_flags++;
            printf("12-bit conversion faster than expected. Error.\n");
        } else if (conv_ms[3] > (float)DSCD_GENUINE_12BIT_MAX_MS) {
            /* some sensors of Family B1, B1v2, B2 may fail this */
            fake_flags++;
            printf("12-bit conversion slower than expected. Error.\n");
        } else {
            /* empirical bounds, neither documented nor guaranteed */
            printf("12-bit conversion time as expected. Ok.\n");
        }
    } else {
        fake_flags++;
        printf("test not possible. Error.\n");
    }

    /* leave the sensor at 12 bit with its original alarm registers */
    write_scratchpad(bus, addr, buffer0[2], buffer0[3], 0x7F);

    printf("  --> %s", t_ok ? "Sensor" : "Temperature test not completed, otherwise sensor");
    if (fake_flags == 0) {
        printf(" responded like a genuine DS18B20.\n");
        printf("  Not tested: EEPROM, Parasite Power, and undocumented commands.\n");
    } else {
        printf(" is not a genuine DS18B20 based on %d deviations.\n", fake_flags);
    }

    r->deviations = fake_flags;
    r->temp_test_ok = t_ok;
    if (r->parasite_power) {
        r->verdict = DS_VERDICT_INCONCLUSIVE; /* sketch: "result of this test is invalid" */
    } else if (fake_flags > 0) {
        r->verdict = DS_VERDICT_CLONE;
    } else if (!t_ok) {
        r->verdict = DS_VERDICT_INCONCLUSIVE;
    } else {
        r->verdict = DS_VERDICT_GENUINE;
    }
    printf("\n");
}

size_t ds_test_discover(ow_bus_t *bus, ds_result_t *results, size_t max_results) {
    /* Collect ROMs first: the tests issue many resets which would otherwise
     * have to be interleaved with the search state machine. */
    uint8_t roms[DSCD_MAX_SENSORS_PER_BUS][8];
    size_t n = 0;
    uint8_t addr[8];

    ow_reset_search(bus);
    while (n < DSCD_MAX_SENSORS_PER_BUS && ow_search(bus, addr, false)) {
        memcpy(roms[n++], addr, 8);
    }

    for (size_t i = 0; i < n; i++) {
        ds_result_t tmp;
        discover_one(bus, roms[i], i < max_results ? &results[i] : &tmp);
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/* 3. Classify (aggressive, undocumented commands)                           */
/* ------------------------------------------------------------------------ */

static int32_t time_conversion_ms(ow_bus_t *bus, const uint8_t *addr) {
    const uint32_t timeout_ms = 3000;
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, DS_CONVERT_T, false);
    const uint32_t start = millis();
    sleep_ms(1); /* in case the sensor is slow to pull the line down (e.g. A3) */
    while (!ow_read_bit(bus) && (millis() - start) < timeout_ms) {
    }
    const uint32_t t = millis() - start;
    return t >= timeout_ms ? -1 : (int32_t)t;
}

static void send_reset_cmd(ow_bus_t *bus, const uint8_t *addr) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, 0x64, false);
    sleep_ms(10);
}

static uint8_t one_byte_return(ow_bus_t *bus, const uint8_t *addr, uint8_t cmd) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, cmd, false);
    return ow_read_byte(bus);
}

static void n_byte_return(ow_bus_t *bus, const uint8_t *addr, uint8_t cmd, int count, uint8_t *buffer) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, cmd, false);
    for (int i = 0; i < count; i++) {
        buffer[i] = ow_read_byte(bus);
    }
}

static uint8_t bit_invert(uint8_t a) {
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        b = (uint8_t)((b << 1) | (a & 0x01));
        a >>= 1;
    }
    return b;
}

static void param2trim(uint16_t offset_11bit, uint8_t curve_5bit, uint8_t *trim1, uint8_t *trim2) {
    *trim1 = bit_invert((uint8_t)(offset_11bit & 0xFF));
    *trim2 = (uint8_t)(curve_5bit * 8 + offset_11bit / 256);
}

static void trim2param(uint8_t trim1, uint8_t trim2, uint16_t *offset_11bit, uint8_t *curve_5bit) {
    *offset_11bit = (uint16_t)(bit_invert(trim1) + ((uint16_t)(trim2 & 0x07)) * 256);
    *curve_5bit = trim2 / 8;
}

static void get_trim_a(ow_bus_t *bus, const uint8_t *addr, uint8_t *trim1, uint8_t *trim2) {
    ow_reset(bus);
    if (addr) ow_select(bus, addr); else ow_skip(bus);
    ow_write_byte(bus, 0x93, false);
    *trim1 = ow_read_byte(bus);
    ow_reset(bus);
    if (addr) ow_select(bus, addr); else ow_skip(bus);
    ow_write_byte(bus, 0x68, false);
    *trim2 = ow_read_byte(bus);
}

static void set_trim_a(ow_bus_t *bus, const uint8_t *addr, uint8_t trim1, uint8_t trim2) {
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, 0x95, false);
    ow_write_byte(bus, trim1, false);
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, 0x63, false);
    ow_write_byte(bus, trim2, false);
    /* not stored permanently: no 0x64 reset here */
}

static void get_trim_params_a(ow_bus_t *bus, const uint8_t *addr, uint16_t *offset, uint8_t *curve) {
    uint8_t t1, t2;
    get_trim_a(bus, addr, &t1, &t2);
    trim2param(t1, t2, offset, curve);
}

static void set_trim_params_a(ow_bus_t *bus, const uint8_t *addr, uint16_t offset, uint8_t curve) {
    uint8_t t1, t2;
    param2trim(offset, curve, &t1, &t2);
    set_trim_a(bus, addr, t1, t2);
}

static bool is_valid_a_scratchpad(const uint8_t *b) {
    if (b[4] != 0x7F && b[4] != 0x5F && b[4] != 0x3F && b[4] != 0x1F) return false;
    if (b[0] == 0x50 && b[1] == 0x05 && b[6] == 0x0C) return true; /* power-up */
    if (b[0] == 0xFF && b[1] == 0x07 && b[6] == 0x0C) return true; /* failed conversion */
    return b[6] == (uint8_t)(0x10 - (b[0] & 0x0F));
}

static bool is_all_00(const uint8_t *b, int n) {
    for (int i = 0; i < n; i++) {
        if (b[i] != 0x00) return false;
    }
    return true;
}

static void trigger_convert(ow_bus_t *bus, const uint8_t *addr, uint8_t conf, uint32_t wait_ms) {
    write_scratchpad(bus, addr, 0xAF, 0xFE, conf);
    ow_reset(bus);
    ow_select(bus, addr);
    ow_write_byte(bus, DS_CONVERT_T, true);
    sleep_ms(wait_ms);
    ow_depower(bus);
}

static int offset_param_range(ow_bus_t *bus, const uint8_t *addr) {
    uint16_t offset0, offset1;
    uint8_t curve0, curve1;
    uint8_t buff[9];
    get_trim_params_a(bus, addr, &offset0, &curve0);
    const uint16_t test_offset_0 = (uint16_t)(0x300 + ((offset0 ^ 0x18) & 0xFF));
    const uint16_t test_offset_1 = (uint16_t)(test_offset_0 + 0x100);
    set_trim_params_a(bus, addr, test_offset_0, curve0);
    get_trim_params_a(bus, addr, &offset1, &curve1);
    if (offset1 != test_offset_0) return 0;
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    const int16_t temp_offset_0 = raw_temp(buff);
    set_trim_params_a(bus, addr, test_offset_1, curve0);
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    set_trim_params_a(bus, addr, offset0, curve0);
    const int16_t temp_offset_1 = raw_temp(buff);
    return (temp_offset_1 - temp_offset_0 > 128) ? 1 : 0;
}

/* 1 = unsigned (A1), 2 = signed (A2), -1 = inconclusive, -2 = not A,
 * -3 = CRC error, 0 = trim parameters have no effect */
static int curve_param_prop(ow_bus_t *bus, const uint8_t *addr) {
    uint8_t buff[9];
    const uint16_t off = 0x3FE; /* approx. half way, LSB != 0xFF */
    int16_t r0f, r00, r1f, r10;

    set_trim_params_a(bus, addr, off, 0x0F);
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    if (ow_crc8(buff, 9) != 0) return -3;
    if (!is_valid_a_scratchpad(buff)) return -2;
    r0f = raw_temp(buff);
    {
        uint16_t o;
        uint8_t c;
        get_trim_params_a(bus, addr, &o, &c);
        if (o != off || c != 0x0F) return 0;
    }

    set_trim_params_a(bus, addr, off, 0x00);
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    if (ow_crc8(buff, 9) != 0) return -3;
    if (!is_valid_a_scratchpad(buff)) return -2;
    r00 = raw_temp(buff);

    set_trim_params_a(bus, addr, off, 0x1F);
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    if (ow_crc8(buff, 9) != 0) return -3;
    if (!is_valid_a_scratchpad(buff)) return -2;
    r1f = raw_temp(buff);

    set_trim_params_a(bus, addr, off, 0x10);
    trigger_convert(bus, addr, 0x7F, MS750 + 50);
    read_scratchpad_retry(bus, addr, buff);
    if (ow_crc8(buff, 9) != 0) return -3;
    if (!is_valid_a_scratchpad(buff)) return -2;
    r10 = raw_temp(buff);

    const int16_t mini = min16(r00, min16(r0f, min16(r10, r1f)));
    const int16_t maxi = max16(r00, max16(r0f, max16(r10, r1f)));
    const bool is_signed = (r0f - r10) > (r1f - r00);
    const bool is_unsigned = (r0f - r10) < (r1f - r00);

    if (is_signed && (maxi - mini > 20 * 16)) return 2;                          /* A2 */
    if (is_unsigned && (maxi - mini > 1 * 16) && (maxi - mini < 6 * 16)) return 1; /* A1 */
    return -1;
}

/* Family C: config register is constant. 1 = C, 0 = not C, -1 = read error */
static int test_family_c(ow_bus_t *bus, const uint8_t *addr) {
    uint8_t buff[9];
    write_scratchpad(bus, addr, 0xAA, 0x55, 0x00);
    read_scratchpad_retry(bus, addr, buff);
    if (is_all_00(buff, 9) || ow_crc8(buff, 9) != 0) return -1;
    const uint8_t cfg1 = buff[4];

    write_scratchpad(bus, addr, 0xAA, 0x55, 0xFF);
    read_scratchpad_retry(bus, addr, buff);
    if (is_all_00(buff, 9) || ow_crc8(buff, 9) != 0) return -1;
    const uint8_t cfg2 = buff[4];

    send_reset_cmd(bus, addr);
    return (cfg1 == cfg2) ? 1 : 0;
}

static void classify_one(ow_bus_t *bus, const uint8_t *addr) {
    print_array(addr, 8, '-');
    if (ow_crc8(addr, 8) != 0) {
        printf(" (CRC Error)");
    }

    const bool normal_power = reports_normal_power(bus, addr);
    if (!normal_power) {
        printf(" (Parasite Power Mode)");
    }
    printf(":");

    int identified = 0;

    { /* Family A */
        const uint8_t r68 = one_byte_return(bus, addr, 0x68);
        const uint8_t r93 = one_byte_return(bus, addr, 0x93);
        if (r68 != 0xFF) {
            const int opr = offset_param_range(bus, addr);
            const int cpp = curve_param_prop(bus, addr);
            if (cpp == 1 && opr) {
                printf(" Family A1 (Genuine).");
                identified++;
            } else if (cpp == 2) {
                printf(" Family A2 (Clone)."); /* signed, 32 oC range */
                identified++;
            } else if (cpp == 1 && !opr) {
                /* A3: Trim1/Trim2 present, curve ok, offset not; late busy signal */
                if (reports_normal_power(bus, addr)) {
                    int ok_count = 0;
                    const int n_rounds = 4;
                    for (int i = 0; i < n_rounds; i++) {
                        ow_reset(bus);
                        ow_select(bus, addr);
                        ow_write_byte(bus, DS_CONVERT_T, false);
                        if (!ow_read_bit(bus)) ok_count++;
                        sleep_ms(MS750);
                    }
                    if (ok_count != n_rounds) {
                        printf(" Family A3 (Clone).");
                    } else {
                        printf(" Family A3 variant (Clone). Please report.");
                    }
                } else {
                    printf(" Family A3 (Clone).");
                }
                identified++;
            } else if (cpp == -3) {
                printf(" (Error reading scratchpad register [A].)");
            } else if (cpp == -1) {
                printf(" Family A, unknown subtype (0x93=");
                print_hex(r93);
                printf(", 0x68=");
                print_hex(r68);
                printf("). Please report.");
                identified++;
            }
            /* cpp == 0 or -2: not Family A as we know it */
        }
        send_reset_cmd(bus, addr);
    }

    { /* Family B */
        const uint8_t r97 = one_byte_return(bus, addr, 0x97);
        if (r97 != 0xFF) {
            uint8_t buffer0[9], buffer1[9];
            bool version2 = true;
            read_scratchpad_retry(bus, addr, buffer0);
            const uint8_t new6 = buffer0[6] ^ 0xFF;
            const uint8_t new7 = buffer0[7] ^ 0xFF;
            ow_reset(bus);
            ow_select(bus, addr);
            ow_write_byte(bus, DS_WRITE_SCRATCHPAD, false);
            ow_write_byte(bus, buffer0[2], false);
            ow_write_byte(bus, buffer0[3], false);
            ow_write_byte(bus, 0x7F, false);
            ow_write_byte(bus, new6, false);
            ow_write_byte(bus, new7, false);
            ow_reset(bus);
            read_scratchpad_retry(bus, addr, buffer1);
            if (buffer1[6] == new6 || buffer1[7] == new7) {
                version2 = false; /* original 2019 behaviour */
            }
            if (r97 == 0x22) printf(" Family B1");
            else if (r97 == 0x31) printf(" Family B2");
            else printf(" Family B");
            if (version2) printf("v2");
            printf(" (Clone)");
            if (r97 != 0x22 && r97 != 0x31) {
                printf(", unknown subtype (0x97=");
                print_hex(r97);
                printf(")");
            }
            printf(".");
            identified++;
        }
    }

    { /* Family C */
        const int c = test_family_c(bus, addr);
        if (c == 1) {
            printf(" Family C (Clone).");
            identified++;
        } else if (c < 0) {
            printf(" (Error reading scratchpad register [C].)");
        }
    }

    { /* Family D */
        const uint8_t r8b = one_byte_return(bus, addr, 0x8B);
        if (r8b == 0x06) printf(" Family D1 (Clone w/o parasite power mode).");
        else if (r8b == 0x02) printf(" Family D1 (Clone).");
        else if (r8b == 0x00) printf(" Family D2 (Clone w/o parasite power mode).");
        else if (r8b != 0xFF) {
            printf(" Family D (Clone), unknown subtype (0x8B=");
            print_hex(r8b);
            printf(").");
        }
        if (r8b != 0xFF) identified++;
    }

    { /* Family E: 2-byte extra scratchpad register (0xDE read / 0x2E write) */
        ow_reset(bus);
        ow_select(bus, addr);
        ow_write_byte(bus, 0xDE, false);
        const uint8_t sp21 = ow_read_byte(bus);
        const uint8_t sp22 = ow_read_byte(bus);
        const uint8_t sp21alt = (uint8_t)~sp21;
        const uint8_t sp22alt = (uint8_t)~sp22;
        ow_reset(bus);
        ow_select(bus, addr);
        ow_write_byte(bus, 0x2E, false);
        ow_write_byte(bus, sp21alt, false);
        ow_write_byte(bus, sp22alt, false);
        ow_reset(bus);
        ow_select(bus, addr);
        ow_write_byte(bus, 0xDE, false);
        const uint8_t sp21b = ow_read_byte(bus);
        const uint8_t sp22b = ow_read_byte(bus);
        if (sp21b == sp21alt && sp22b == sp22alt) {
            printf(" Family E (Clone).");
            identified++;
        }
    }

    { /* Family F: returns 3 bytes on 0x19 */
        uint8_t buffer[3];
        n_byte_return(bus, addr, 0x19, 3, buffer);
        if (buffer[0] != 0xFF || buffer[1] != 0xFF || buffer[2] != 0xFF) {
            printf(" Family F (Clone).");
            identified++;
        }
    }

    { /* Family G: responds to 0x8E, peculiar <byte 6> bug */
        const uint8_t r8e = one_byte_return(bus, addr, 0x8E);
        if (r8e != 0xFF) {
            uint8_t buffer[9];
            ow_reset(bus);
            ow_select(bus, addr);
            ow_write_byte(bus, DS_CONVERT_T, true);
            sleep_ms(MS750 + 50);
            ow_depower(bus);
            read_scratchpad_retry(bus, addr, buffer);
            if (buffer[6] == (uint8_t)(0x20 - (buffer[0] & 0x0F))) {
                printf(" Family G (Clone).");
            } else {
                printf(" Possibly Family G variant (Clone). Please report.");
            }
            identified++;
        }
    }

    { /* Family H: conversion time scales by sqrt(2). Not in parasite mode. */
        uint8_t buffer0[9], buffer1[9];
        read_scratchpad_retry(bus, addr, buffer0);

        do {
            write_scratchpad(bus, addr, buffer0[2], buffer0[3], 0 * 32 + 0x1F); /* 9 bit */
            sleep_ms(1);
            read_scratchpad_retry(bus, addr, buffer1);
            if (buffer1[4] / 32 != 0) break;
            int32_t t9 = time_conversion_ms(bus, addr);
            const float time_9bit = t9 > 1 ? (float)t9 : 1.0f;

            write_scratchpad(bus, addr, buffer0[2], buffer0[3], 1 * 32 + 0x1F); /* 10 bit */
            sleep_ms(1);
            read_scratchpad_retry(bus, addr, buffer1);
            if (buffer1[4] / 32 != 1) break;
            int32_t t10 = time_conversion_ms(bus, addr);
            const float time_10bit = t10 > 1 ? (float)t10 : 1.0f;

            const float ratio = time_10bit / time_9bit;
            if (fabsf(ratio - 1.41f) <= 0.2f) {
                printf(" Family H (Clone).");
                identified++;
            } else if (fabsf(ratio - 2.0f) <= 0.2f || fabsf(ratio - 1.0f) <= 0.2f) {
                /* ratios known from other families */
            } else {
                identified++;
                printf(" Possibly Family H variant (Clone). Please report.");
            }
        } while (0);

        /* (re-)set to 12 bit */
        write_scratchpad(bus, addr, buffer0[2], buffer0[3], 3 * 32 + 0x1F);
    }

    if (identified == 0) {
        printf(" (Could not identify Family.)");
    }
    if (identified > 1) {
        printf(" (Part may belong to a Family not seen in 2024.)");
    }
    if (identified != 1) {
        if (normal_power) {
            printf("  Please report.");
        } else if (identified == 0) {
            printf("  Might be Family H: please repeat test not in parasite power mode.");
        } else {
            printf("  Please repeat test not in parasite power mode.");
        }
    }
    printf("\n");
}

void ds_test_classify(ow_bus_t *bus) {
    uint8_t roms[DSCD_MAX_SENSORS_PER_BUS][8];
    size_t n = 0;
    uint8_t addr[8];

    ow_reset_search(bus);
    while (n < DSCD_MAX_SENSORS_PER_BUS && ow_search(bus, addr, false)) {
        memcpy(roms[n++], addr, 8);
    }
    for (size_t i = 0; i < n; i++) {
        classify_one(bus, roms[i]);
    }
    if (n == 0) {
        /* maybe the pull-up resistor is missing */
        printf("No sensors detected.\n");
    }
}
