/*
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * On-board LED status indicator.
 *
 * On the Pico 2 W the LED is connected to the CYW43 wireless chip, not to an
 * RP2350 GPIO. Talking to the CYW43 involves SPI transfers and interrupts, so
 * the LED is owned entirely by core 1: core 1 initialises the CYW43 and runs
 * the blink patterns. Core 0 only writes the desired mode. This keeps the
 * bit-banged 1-Wire timing on core 0 free from radio-driver jitter.
 */
#ifndef DSCD_STATUS_LED_H
#define DSCD_STATUS_LED_H

#include <stdbool.h>

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_IDLE,         /* no sensor: short blip every 2 s (heartbeat)  */
    LED_MODE_BUSY,         /* test running: fast flicker (10 Hz)           */
    LED_MODE_GENUINE,      /* all sensors genuine: solid ON                */
    LED_MODE_CLONE,        /* at least one clone: continuous blink (2 Hz)  */
    LED_MODE_INCONCLUSIVE, /* could not decide: double blink every second  */
} led_mode_t;

/* Launches core 1 and initialises the LED. Returns false if the LED driver
 * could not be started (the firmware keeps working without the LED). */
bool status_led_init(void);

void       status_led_set(led_mode_t mode);
led_mode_t status_led_get(void);
const char *status_led_mode_name(led_mode_t mode);

#endif /* DSCD_STATUS_LED_H */
