/*
 * Copyright 2024 Chris Petrich (original Arduino sketch)
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors (Pico SDK port)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * DS18B20 clone detector for the Raspberry Pi Pico 2 W.
 *
 *  - Up to 8 independent 1-Wire buses (default GP28, GP27, GP26, GP22, GP21,
 *    GP20, GP19, GP18), sensors can be attached to any of them.
 *  - Stand-alone mode: when sensors are plugged in, the safe "discover" test
 *    runs automatically and the on-board LED shows the verdict:
 *        solid ON          -> all sensors genuine
 *        blinking (2 Hz)   -> at least one clone
 *        double blink      -> inconclusive (parasite power / temp. test)
 *        short blip / 2 s  -> no sensor attached
 *        fast flicker      -> test running
 *  - Interactive menu on the USB serial console (115200 8N1, any baud works
 *    for USB CDC) with the four tests of the original sketch.
 *
 * Based on https://github.com/cpetrich/counterfeit_DS18B20 by Chris Petrich.
 */
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#if LIB_PICO_STDIO_USB
#include "pico/stdio_usb.h"
#endif

#include "config.h"
#include "ds18b20_tests.h"
#include "onewire.h"
#include "status_led.h"

#ifndef DSCD_VERSION
#define DSCD_VERSION "dev"
#endif

static const uint k_bus_pins[] = {DSCD_BUS_PINS};
#define NUM_BUSES (sizeof(k_bus_pins) / sizeof(k_bus_pins[0]))

static ow_bus_t g_buses[NUM_BUSES];
static ow_reset_result_t g_bus_state[NUM_BUSES];

static ds_result_t g_results[DSCD_MAX_SENSORS_TOTAL];
static size_t g_result_count;
static bool g_have_results;

static bool g_led_ok;

/* ------------------------------------------------------------------------ */

static void print_bus_list(void) {
    for (size_t i = 0; i < NUM_BUSES; i++) {
        printf("%sGP%u", i ? ", " : "", k_bus_pins[i]);
    }
}

static void print_banner(void) {
    printf("\n\n=================================================================\n");
    printf(" DS18B20 clone detector for Raspberry Pi Pico 2 W  v%s\n", DSCD_VERSION);
    printf(" Port of discover-classify_fake_DS18B20 (Nov 2024) by Chris Petrich\n");
    printf(" https://github.com/cpetrich/counterfeit_DS18B20  (Apache-2.0)\n");
    printf("=================================================================\n");
    printf("This firmware tests DS18B20 sensors for differences with the DS18B20\n");
    printf("produced by Analog Devices / Maxim Integrated / Dallas Semiconductor.\n");
    printf("  We are in a game of whack-a-mole: new clones keep appearing.\n");
    printf("  Please consider reporting suspicious results to the original project.\n");
    printf("1-Wire buses (4.7k pull-up to 3V3 on each): ");
    print_bus_list();
    printf("\nStatus LED: %s\n", g_led_ok ? "ok" : "NOT AVAILABLE (CYW43 init failed)");
}

static void print_menu(void) {
    printf("\nSelect the test (sensors on ");
    print_bus_list();
    printf("):\n");
    printf("  0. Enumerate attached sensors.\n\n");
    printf("  1. Check temperature alarm function with a safe test according to datasheet.\n\n");
    printf("  2. Discover clones with a safe test, using only function codes documented in the datasheet.\n");
    printf("     This may be sufficient to detect clones. (Runs automatically on hot-plug.)\n\n");
    printf("  3. Classify clones with an aggressive test, using undocumented function codes.\n");
    printf("     This could conceivably mess up calibration parameters. Only use this on sensors\n");
    printf("     dedicated to testing.\n\n");
    printf("  s. Show summary of the last discover run.\n");
    printf("  a. Re-run the automatic test now.\n");
    printf("  h. Show this menu.\n\n");
    printf("Your choice> ");
    fflush(stdout);
}

/* ------------------------------------------------------------------------ */
/* bus scanning                                                              */
/* ------------------------------------------------------------------------ */

static inline uint32_t fnv1a(uint32_t h, uint8_t b) {
    return (h ^ b) * 16777619u;
}

/* Scans all buses. Returns a signature of the attached sensor set and the
 * total sensor count. Reports bus faults once when they appear/disappear. */
static uint32_t scan_buses(int *total_out) {
    uint32_t sig = 2166136261u;
    int total = 0;

    for (size_t i = 0; i < NUM_BUSES; i++) {
        ow_bus_t *bus = &g_buses[i];
        const ow_reset_result_t st = ow_reset_ex(bus);

        if (st != g_bus_state[i]) {
            if (st == OW_RESET_BUS_LOW) {
                printf("\n[GP%u] WARNING: data line stuck LOW - short circuit, reversed wiring"
                       " or missing pull-up?\n", bus->pin);
            } else if (g_bus_state[i] == OW_RESET_BUS_LOW) {
                printf("\n[GP%u] data line released, bus ok again.\n", bus->pin);
            }
            g_bus_state[i] = st;
        }
        if (st != OW_RESET_PRESENCE) {
            continue;
        }

        uint8_t rom[8];
        ow_reset_search(bus);
        while (ow_search(bus, rom, false)) {
            sig = fnv1a(sig, (uint8_t)bus->pin);
            for (int k = 0; k < 8; k++) {
                sig = fnv1a(sig, rom[k]);
            }
            if (++total >= 255) {
                break; /* protects against a noisy bus producing endless ROMs */
            }
        }
    }
    *total_out = total;
    return sig;
}

/* ------------------------------------------------------------------------ */
/* results                                                                   */
/* ------------------------------------------------------------------------ */

static led_mode_t led_mode_from_results(void) {
    if (g_result_count == 0) {
        return LED_MODE_IDLE;
    }
    bool any_clone = false, any_unsure = false;
    for (size_t i = 0; i < g_result_count; i++) {
        switch (g_results[i].verdict) {
        case DS_VERDICT_CLONE:        any_clone = true; break;
        case DS_VERDICT_INCONCLUSIVE:
        case DS_VERDICT_COMM_ERROR:   any_unsure = true; break;
        default: break;
        }
    }
    if (any_clone) return LED_MODE_CLONE;
    if (any_unsure) return LED_MODE_INCONCLUSIVE;
    return LED_MODE_GENUINE;
}

static void print_summary(void) {
    printf("\n========================== SUMMARY ==========================\n");
    if (!g_have_results) {
        printf(" No discover run yet.\n");
    } else if (g_result_count == 0) {
        printf(" No sensors found on ");
        print_bus_list();
        printf(".\n");
    } else {
        printf("  #  Bus   ROM                      Verdict       Dev.  Temp\n");
        int n_gen = 0, n_clone = 0, n_other = 0;
        for (size_t i = 0; i < g_result_count; i++) {
            const ds_result_t *r = &g_results[i];
            printf(" %2u  GP%-2u  ", (unsigned)(i + 1), r->pin);
            ds_print_rom(r->rom);
            printf("  %-12s  %3d  ", ds_verdict_name(r->verdict), r->deviations);
            if (r->temp_valid) {
                printf("%7.2f oC", (double)r->temp_c);
            } else {
                printf("     -");
            }
            if (r->parasite_power) printf("  (parasite power)");
            if (!r->rom_crc_ok) printf("  (ROM CRC error)");
            printf("\n");
            if (r->verdict == DS_VERDICT_GENUINE) n_gen++;
            else if (r->verdict == DS_VERDICT_CLONE) n_clone++;
            else n_other++;
        }
        printf(" -----------------------------------------------------------\n");
        printf(" Genuine: %d   Clone: %d   Inconclusive/Error: %d\n", n_gen, n_clone, n_other);
        if (n_other) {
            printf(" Hint: inconclusive -> power the sensor from 3V3 (not parasite),\n"
                   "       warm it with your fingers and press 'a' to repeat.\n");
        }
    }
    printf(" LED: %s\n", status_led_mode_name(led_mode_from_results()));
    printf("=============================================================\n");
}

static void run_discover_all(const char *reason) {
    status_led_set(LED_MODE_BUSY);
    printf("\n>>> Discover test (%s)\n", reason);

    g_result_count = 0;
    for (size_t i = 0; i < NUM_BUSES; i++) {
        ow_bus_t *bus = &g_buses[i];
        if (!ow_reset(bus)) {
            continue;
        }
        printf("\n----- Bus GP%u -----\n", bus->pin);
        const size_t room = DSCD_MAX_SENSORS_TOTAL - g_result_count;
        const size_t n = ds_test_discover(bus, &g_results[g_result_count], room);
        g_result_count += (n < room) ? n : room;
    }
    g_have_results = true;

    print_summary();
    status_led_set(led_mode_from_results());
}

typedef void (*bus_test_fn)(ow_bus_t *bus);

static void run_on_all_buses(bus_test_fn fn) {
    const led_mode_t prev = status_led_get();
    status_led_set(LED_MODE_BUSY);
    int buses_with_sensors = 0;
    for (size_t i = 0; i < NUM_BUSES; i++) {
        ow_bus_t *bus = &g_buses[i];
        if (!ow_reset(bus)) {
            continue;
        }
        buses_with_sensors++;
        printf("\n----- Bus GP%u -----\n", bus->pin);
        fn(bus);
    }
    if (buses_with_sensors == 0) {
        printf("No sensors detected on ");
        print_bus_list();
        printf(". Check wiring and the 4.7k pull-up resistor.\n");
    }
    printf("-------------\n");
    status_led_set(prev);
}

static void enumerate_wrapper(ow_bus_t *bus) {
    (void)ds_test_enumerate(bus);
}

static bool confirm(const char *question, uint32_t timeout_ms) {
    printf("%s [y/N] ", question);
    fflush(stdout);
    const uint32_t start = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - start < timeout_ms) {
        const int c = getchar_timeout_us(10 * 1000);
        if (c == PICO_ERROR_TIMEOUT || c == '\r' || c == '\n') {
            continue;
        }
        putchar(c);
        putchar('\n');
        return c == 'y' || c == 'Y';
    }
    printf("(timeout)\n");
    return false;
}

/* ------------------------------------------------------------------------ */

int main(void) {
    stdio_init_all();

    g_led_ok = status_led_init();
    status_led_set(LED_MODE_BUSY);

    for (size_t i = 0; i < NUM_BUSES; i++) {
        ow_init(&g_buses[i], k_bus_pins[i], DSCD_ENABLE_INTERNAL_PULLUP);
        g_bus_state[i] = OW_RESET_NO_DEVICE;
    }

#if LIB_PICO_STDIO_USB
    /* give a serial terminal the chance to connect so the banner is not lost */
    const uint32_t wait_start = to_ms_since_boot(get_absolute_time());
    while (!stdio_usb_connected() &&
           to_ms_since_boot(get_absolute_time()) - wait_start < DSCD_USB_WAIT_MS) {
        sleep_ms(10);
    }
#endif

    print_banner();
    print_menu();
    status_led_set(LED_MODE_IDLE);

    uint32_t candidate_sig = 0;
    int stable_count = 0;
    uint32_t tested_sig = 0;
    bool tested_valid = false;
    uint32_t last_scan = 0;

    for (;;) {
        const int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT && c != '\r' && c != '\n') {
            putchar(c);
            printf("\n\n");
            switch (c) {
            case '0':
                run_on_all_buses(enumerate_wrapper);
                break;
            case '1':
                run_on_all_buses(ds_test_alarm);
                break;
            case '2':
                run_discover_all("manual");
                break;
            case '3':
                printf("WARNING: test 3 sends UNDOCUMENTED commands. It could conceivably\n"
                       "mess up calibration parameters of clones. Only use it on sensors\n"
                       "dedicated to testing.\n");
                if (confirm("Continue?", 30 * 1000)) {
                    run_on_all_buses(ds_test_classify);
                } else {
                    printf("Cancelled.\n");
                }
                break;
            case 's':
            case 'S':
                print_summary();
                break;
            case 'a':
            case 'A':
                tested_valid = false; /* forces a new automatic run */
                stable_count = 0;
                break;
            case 'h':
            case 'H':
            case '?':
                break;
            default:
                printf("Unknown choice.\n");
                break;
            }
            print_menu();
            last_scan = to_ms_since_boot(get_absolute_time());
        }

#if DSCD_AUTO_TEST
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_scan >= DSCD_SCAN_INTERVAL_MS) {
            last_scan = now;
            int total = 0;
            const uint32_t sig = scan_buses(&total);

            if (sig == candidate_sig) {
                if (stable_count < DSCD_STABLE_SCANS) stable_count++;
            } else {
                candidate_sig = sig;
                stable_count = 1;
            }

            if (stable_count >= DSCD_STABLE_SCANS && (!tested_valid || sig != tested_sig)) {
                tested_sig = sig;
                tested_valid = true;
                if (total == 0) {
                    g_result_count = 0;
                    g_have_results = true;
                    status_led_set(LED_MODE_IDLE);
                    printf("\n[auto] No sensors on any bus. Waiting for sensors...\n");
                } else {
                    printf("\n[auto] %d sensor(s) detected.\n", total);
                    run_discover_all("automatic");
                    print_menu();
                }
            }
        }
#endif
        sleep_ms(5);
    }
}
