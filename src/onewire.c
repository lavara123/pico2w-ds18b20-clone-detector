/*
 * Copyright 2026 The pico2w-ds18b20-clone-detector contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "onewire.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

/* Standard-speed timings in microseconds (AN126, same values as the widely
 * used Arduino OneWire library). */
#define T_RESET_LOW_US        480
#define T_PRESENCE_SAMPLE_US   70
#define T_RESET_RECOVERY_US   410
#define T_BUS_HIGH_TIMEOUT_US 250
#define T_W1_LOW_US            10
#define T_W1_RELEASE_US        55
#define T_W0_LOW_US            65
#define T_W0_RELEASE_US         5
#define T_R_LOW_US              3
#define T_R_SAMPLE_US          10
#define T_R_RECOVERY_US        53

static inline void line_low(const ow_bus_t *bus) {
    gpio_put(bus->pin, 0);
    gpio_set_dir(bus->pin, GPIO_OUT);
}

static inline void line_release(const ow_bus_t *bus) {
    gpio_set_dir(bus->pin, GPIO_IN);
    gpio_put(bus->pin, 0);
}

static inline bool line_level(const ow_bus_t *bus) {
    return gpio_get(bus->pin);
}

void ow_init(ow_bus_t *bus, uint pin, bool internal_pullup) {
    memset(bus, 0, sizeof(*bus));
    bus->pin = pin;
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_put(pin, 0);
    if (internal_pullup) {
        gpio_pull_up(pin);
    } else {
        gpio_disable_pulls(pin);
    }
    /* Strong pull-up during parasite-power conversions needs ~1.5 mA. */
    gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    ow_reset_search(bus);
}

ow_reset_result_t ow_reset_ex(ow_bus_t *bus) {
    line_release(bus);

    /* Wait for the line to be high (devices may still be finishing a slot). */
    const uint32_t t0 = time_us_32();
    while (!line_level(bus)) {
        if (time_us_32() - t0 > T_BUS_HIGH_TIMEOUT_US) {
            return OW_RESET_BUS_LOW;
        }
    }

    uint32_t irq = save_and_disable_interrupts();
    line_low(bus);
    restore_interrupts(irq);
    busy_wait_us_32(T_RESET_LOW_US); /* longer is harmless, shorter is not */

    irq = save_and_disable_interrupts();
    line_release(bus);
    busy_wait_us_32(T_PRESENCE_SAMPLE_US);
    const bool presence = !line_level(bus);
    restore_interrupts(irq);

    busy_wait_us_32(T_RESET_RECOVERY_US);
    return presence ? OW_RESET_PRESENCE : OW_RESET_NO_DEVICE;
}

void ow_write_bit(ow_bus_t *bus, bool bit) {
    uint32_t irq = save_and_disable_interrupts();
    line_low(bus);
    if (bit) {
        busy_wait_us_32(T_W1_LOW_US);
        line_release(bus);
        restore_interrupts(irq);
        busy_wait_us_32(T_W1_RELEASE_US);
    } else {
        busy_wait_us_32(T_W0_LOW_US);
        line_release(bus);
        restore_interrupts(irq);
        busy_wait_us_32(T_W0_RELEASE_US);
    }
}

bool ow_read_bit(ow_bus_t *bus) {
    uint32_t irq = save_and_disable_interrupts();
    line_low(bus);
    busy_wait_us_32(T_R_LOW_US);
    line_release(bus);
    busy_wait_us_32(T_R_SAMPLE_US);
    const bool bit = line_level(bus);
    restore_interrupts(irq);
    busy_wait_us_32(T_R_RECOVERY_US);
    return bit;
}

void ow_write_byte(ow_bus_t *bus, uint8_t value, bool power) {
    for (int i = 0; i < 8; i++) {
        ow_write_bit(bus, value & 0x01);
        value >>= 1;
    }
    if (power) {
        /* strong pull-up: actively drive the line high */
        uint32_t irq = save_and_disable_interrupts();
        gpio_put(bus->pin, 1);
        gpio_set_dir(bus->pin, GPIO_OUT);
        restore_interrupts(irq);
    }
}

uint8_t ow_read_byte(ow_bus_t *bus) {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (ow_read_bit(bus)) {
            value |= (uint8_t)(1u << i);
        }
    }
    return value;
}

void ow_depower(ow_bus_t *bus) {
    line_release(bus);
}

void ow_select(ow_bus_t *bus, const uint8_t rom[8]) {
    ow_write_byte(bus, OW_CMD_MATCH_ROM, false);
    for (int i = 0; i < 8; i++) {
        ow_write_byte(bus, rom[i], false);
    }
}

void ow_skip(ow_bus_t *bus) {
    ow_write_byte(bus, OW_CMD_SKIP_ROM, false);
}

void ow_reset_search(ow_bus_t *bus) {
    bus->last_discrepancy = 0;
    bus->last_device = false;
    memset(bus->rom, 0, sizeof(bus->rom));
}

bool ow_search(ow_bus_t *bus, uint8_t rom[8], bool alarm_only) {
    if (bus->last_device) {
        ow_reset_search(bus);
        return false;
    }
    if (!ow_reset(bus)) {
        ow_reset_search(bus);
        return false;
    }

    ow_write_byte(bus, alarm_only ? OW_CMD_ALARM_SEARCH : OW_CMD_SEARCH_ROM, false);

    int id_bit_number = 1;
    int last_zero = 0;
    int rom_byte_number = 0;
    uint8_t rom_byte_mask = 1;

    do {
        const bool id_bit = ow_read_bit(bus);
        const bool cmp_id_bit = ow_read_bit(bus);
        bool direction;

        if (id_bit && cmp_id_bit) {
            break; /* no device participating */
        }
        if (id_bit != cmp_id_bit) {
            direction = id_bit; /* all devices agree on this bit */
        } else {
            if (id_bit_number < bus->last_discrepancy) {
                direction = (bus->rom[rom_byte_number] & rom_byte_mask) != 0;
            } else {
                direction = (id_bit_number == bus->last_discrepancy);
            }
            if (!direction) {
                last_zero = id_bit_number;
            }
        }

        if (direction) {
            bus->rom[rom_byte_number] |= rom_byte_mask;
        } else {
            bus->rom[rom_byte_number] &= (uint8_t)~rom_byte_mask;
        }
        ow_write_bit(bus, direction);

        id_bit_number++;
        rom_byte_mask = (uint8_t)(rom_byte_mask << 1);
        if (rom_byte_mask == 0) {
            rom_byte_number++;
            rom_byte_mask = 1;
        }
    } while (rom_byte_number < 8);

    if (id_bit_number < 65) {
        ow_reset_search(bus);
        return false;
    }

    bus->last_discrepancy = last_zero;
    if (last_zero == 0) {
        bus->last_device = true;
    }
    memcpy(rom, bus->rom, 8);
    /* Note: the ROM CRC is deliberately NOT checked here. Some clones allow
     * their ROM to be overwritten, so the caller reports CRC errors itself. */
    return true;
}

uint8_t ow_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t in = *data++;
        for (int i = 0; i < 8; i++) {
            const uint8_t mix = (crc ^ in) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            in >>= 1;
        }
    }
    return crc;
}
