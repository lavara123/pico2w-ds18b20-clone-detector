/*
 * Copyright 2024 Chris Petrich (original Arduino sketch)
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors (Pico SDK port)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Build-time configuration. Every value can be overridden from CMake, e.g.
 *   target_compile_definitions(ds18b20_clone_detector PRIVATE DSCD_SCAN_INTERVAL_MS=500)
 */
#ifndef DSCD_CONFIG_H
#define DSCD_CONFIG_H

/* ---------------------------------------------------------------------------
 * 1-Wire buses
 *
 * Every pin in this list is an independent 1-Wire bus. Each bus needs its own
 * 4.7 kOhm pull-up resistor to 3V3 (pin 36). Sensors can be attached to any
 * of these pins, one or several per pin.
 *
 * GP23, GP24, GP25 and GP29 are used by the CYW43 radio on the Pico 2 W and
 * must NOT be used here.
 * ------------------------------------------------------------------------- */
#ifndef DSCD_BUS_PINS
#define DSCD_BUS_PINS 28, 27, 26, 22, 21, 20, 19, 18
#endif

/* Maximum number of sensors tracked per bus / in total (for the summary). */
#ifndef DSCD_MAX_SENSORS_PER_BUS
#define DSCD_MAX_SENSORS_PER_BUS 16
#endif
#ifndef DSCD_MAX_SENSORS_TOTAL
#define DSCD_MAX_SENSORS_TOTAL 64
#endif

/* Enable the RP2350 internal pull-up (~50 kOhm) in addition to the external
 * resistor. It keeps unused pins from floating; it is far too weak to replace
 * the external 4.7 kOhm resistor. */
#ifndef DSCD_ENABLE_INTERNAL_PULLUP
#define DSCD_ENABLE_INTERNAL_PULLUP 1
#endif

/* ---------------------------------------------------------------------------
 * Automatic mode: the buses are scanned periodically. When the set of attached
 * sensors changes (and is stable for DSCD_STABLE_SCANS scans) the safe
 * "discover" test (menu item 2) is executed automatically and the LED shows
 * the result. This makes the board usable without a PC.
 * ------------------------------------------------------------------------- */
#ifndef DSCD_AUTO_TEST
#define DSCD_AUTO_TEST 1
#endif
#ifndef DSCD_SCAN_INTERVAL_MS
#define DSCD_SCAN_INTERVAL_MS 1000
#endif
#ifndef DSCD_STABLE_SCANS
#define DSCD_STABLE_SCANS 2
#endif

/* How long to wait for a USB serial terminal after power-up so that the
 * banner is not lost (the firmware also runs without a terminal). */
#ifndef DSCD_USB_WAIT_MS
#define DSCD_USB_WAIT_MS 2500
#endif

/* ---------------------------------------------------------------------------
 * DS18B20 timing. 750 ms is the datasheet maximum for a 12-bit conversion.
 * The 12-bit window below was established empirically by C. Petrich for
 * genuine Maxim/Analog Devices parts; it is neither documented nor guaranteed.
 * ------------------------------------------------------------------------- */
#ifndef DSCD_CONVERSION_MS
#define DSCD_CONVERSION_MS 750
#endif
#ifndef DSCD_GENUINE_12BIT_MIN_MS
#define DSCD_GENUINE_12BIT_MIN_MS 550
#endif
#ifndef DSCD_GENUINE_12BIT_MAX_MS
#define DSCD_GENUINE_12BIT_MAX_MS 660
#endif

#endif /* DSCD_CONFIG_H */
