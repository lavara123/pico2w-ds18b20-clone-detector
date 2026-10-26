/*
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "status_led.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#if defined(PICO_CYW43_ARCH_THREADSAFE_BACKGROUND)
#include "pico/cyw43_arch.h"
#define DSCD_LED_ON_CYW43 1
#elif defined(PICO_DEFAULT_LED_PIN)
#define DSCD_LED_ON_GPIO 1
#endif

static volatile led_mode_t g_mode = LED_MODE_BUSY;

static void led_hw_put(bool on) {
#if defined(DSCD_LED_ON_CYW43)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#elif defined(DSCD_LED_ON_GPIO)
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)on;
#endif
}

static bool led_hw_init(void) {
#if defined(DSCD_LED_ON_CYW43)
    return cyw43_arch_init() == 0;
#elif defined(DSCD_LED_ON_GPIO)
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return true;
#else
    return false;
#endif
}

/* Returns the LED level for `mode` at `t_ms` milliseconds into the pattern. */
static bool pattern_level(led_mode_t mode, uint32_t t_ms) {
    switch (mode) {
    case LED_MODE_IDLE:
        return (t_ms % 2000u) < 40u;
    case LED_MODE_BUSY:
        return (t_ms % 100u) < 50u;
    case LED_MODE_GENUINE:
        return true;
    case LED_MODE_CLONE:
        return (t_ms % 500u) < 250u;
    case LED_MODE_INCONCLUSIVE: {
        const uint32_t p = t_ms % 1000u;
        return (p < 100u) || (p >= 200u && p < 300u);
    }
    case LED_MODE_OFF:
    default:
        return false;
    }
}

static void core1_main(void) {
    const bool ok = led_hw_init();
    multicore_fifo_push_blocking(ok ? 1u : 0u);
    if (!ok) {
        for (;;) {
            tight_loop_contents();
            sleep_ms(1000);
        }
    }

    led_mode_t last_mode = (led_mode_t)-1;
    uint32_t pattern_start = 0;
    bool level = false;
    led_hw_put(false);

    for (;;) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const led_mode_t mode = g_mode;
        if (mode != last_mode) {
            last_mode = mode;
            pattern_start = now; /* restart pattern so changes are visible at once */
        }
        const bool want = pattern_level(mode, now - pattern_start);
        if (want != level) {
            led_hw_put(want);
            level = want;
        }
        sleep_ms(5);
    }
}

bool status_led_init(void) {
    multicore_launch_core1(core1_main);
    uint32_t result = 0;
    if (!multicore_fifo_pop_timeout_us(3 * 1000 * 1000, &result)) {
        return false;
    }
    return result == 1u;
}

void status_led_set(led_mode_t mode) {
    g_mode = mode;
}

led_mode_t status_led_get(void) {
    return g_mode;
}

const char *status_led_mode_name(led_mode_t mode) {
    switch (mode) {
    case LED_MODE_OFF:          return "off";
    case LED_MODE_IDLE:         return "idle (short blip every 2 s)";
    case LED_MODE_BUSY:         return "busy (fast flicker)";
    case LED_MODE_GENUINE:      return "GENUINE (solid on)";
    case LED_MODE_CLONE:        return "CLONE (blinking)";
    case LED_MODE_INCONCLUSIVE: return "inconclusive (double blink)";
    default:                    return "?";
    }
}
