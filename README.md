# DS18B20 Clone Detector for Raspberry Pi Pico 2 W

**English** | [Türkçe](README.tr.md)

Find out whether your DS18B20 temperature sensors are **genuine** (Maxim / Analog Devices) or **clones/counterfeits** — with a Raspberry Pi Pico 2 W, no PC required.

Plug a sensor in, and the on-board LED tells you the result:

| LED | Meaning |
|---|---|
| **Solid ON** | All attached sensors responded like a genuine DS18B20 |
| **Blinking (2 Hz, continuous)** | At least one sensor is a **clone** |
| Double blink every second | Inconclusive (parasite power, or temperature test could not complete) |
| Short blip every 2 s | Idle – no sensor attached |
| Fast flicker | Test is running |

A full report and an interactive test menu are available on the USB serial console.

> **Credits.** This project is a C / Pico SDK port of
> [`discover-classify_fake_DS18B20.ino`](https://github.com/cpetrich/counterfeit_DS18B20)
> by **Chris Petrich**. All of the test procedures, thresholds and the sensor
> family classification are his work, based on years of research into DS18B20
> clones. Read his excellent write-up: **https://github.com/cpetrich/counterfeit_DS18B20**.
> If you find a sensor that behaves unexpectedly, please report it there.

---

## Features

- **Pico SDK 2.3.1**, Pico 2 W (RP2350), builds out of the box with the official *Raspberry Pi Pico* VS Code extension.
- **8 independent 1-Wire buses**: GP28, GP27, GP26, GP22, GP21, GP20, GP19, GP18. Attach sensors to any of them, one or many per pin.
- **Stand-alone mode**: buses are scanned every second; when the set of sensors changes, the safe test runs automatically and the LED shows the verdict.
- **Hot-plug**: swap sensors one after another and watch the LED.
- All four tests of the original sketch, unchanged in logic:
  0. Enumerate sensors
  1. Alarm-function test (documented commands)
  2. Discover clones – safe test, documented commands only (**runs automatically**)
  3. Classify the clone family – aggressive, undocumented commands (asks for confirmation)
- Summary table with verdict, number of deviations and temperature for every sensor.
- Clean, dependency-free 1-Wire driver (AN126/AN187), with strong pull-up support and short interrupt-free slots so USB stays responsive.
- The LED (wired to the CYW43 radio chip on the Pico 2 W) is driven from **core 1**, so radio-driver activity never disturbs the 1-Wire timing on core 0.
- GitHub Actions CI builds the `.uf2` on every push and attaches it to releases.

## Hardware

- Raspberry Pi Pico 2 W
- DS18B20 sensor(s) to test (TO-92 or waterproof probe)
- **4.7 kΩ resistor per bus pin you use** (data → 3V3)
- Breadboard and jumper wires

### Wiring

```
 Pico 2 W                                   DS18B20 (flat side facing you)
                                            ┌─────────┐
 3V3(OUT) pin 36 ───────┬───────────────────│ 3 VDD   │
                        │                   │         │
                      [4.7k]                │         │
                        │                   │         │
 GP28     pin 34 ───────┴───────────────────│ 2 DQ    │
                                            │         │
 GND      pin 38 ───────────────────────────│ 1 GND   │
                                            └─────────┘
 Waterproof probes: red = VDD, yellow (or white) = DQ, black = GND
```

Any of the bus pins can be used the same way – each with **its own** 4.7 kΩ pull-up to 3V3:

| GPIO | Physical pin | | GPIO | Physical pin |
|---|---|---|---|---|
| GP28 | 34 | | GP21 | 27 |
| GP27 | 32 | | GP20 | 26 |
| GP26 | 31 | | GP19 | 25 |
| GP22 | 29 | | GP18 | 24 |

3V3(OUT) = pin 36, GND = pins 23, 28, 33 or 38.

Notes:

- Power the sensors from **3V3**, never from 5 V / VBUS / VSYS (GP26–GP28 are ADC pins and are not 5 V tolerant).
- **Do not use parasite power** (VDD tied to GND). Several checks need the sensor to be externally powered; the firmware will report *inconclusive*.
- The internal ~50 kΩ pull-up of the RP2350 is enabled only to keep unused pins from floating. It is **not** a substitute for the external 4.7 kΩ resistor.
- GP23, GP24, GP25 and GP29 are used by the wireless chip and are deliberately not in the list.

## Getting the firmware

### Option A – download

Download `ds18b20_clone_detector.uf2` from the [Releases](../../releases) page (or from the *Actions* tab → latest run → *Artifacts*).

### Option B – build with VS Code (recommended)

1. Install VS Code and the **Raspberry Pi Pico** extension (`raspberry-pi.raspberry-pi-pico`).
2. Clone this repository.
3. In the Pico sidebar choose **Import Project**, select the cloned folder, pick **SDK 2.3.1** and confirm. The extension downloads the SDK, toolchain and picotool if needed.
4. Click **Compile Project** in the status bar. The firmware ends up in `build/ds18b20_clone_detector.uf2`.

### Option C – command line

```bash
export PICO_SDK_PATH=/path/to/pico-sdk   # 2.3.1, with submodules
cmake -S . -B build -DPICO_BOARD=pico2_w
cmake --build build -j
```

## Flashing

1. Hold the **BOOTSEL** button and connect the Pico 2 W via USB. A drive called `RP2350` appears.
2. Copy `ds18b20_clone_detector.uf2` onto it. The board reboots and starts.

(Or, in VS Code, click **Run** while the board is in BOOTSEL mode.)

## Usage

### Without a PC

Power the board from any USB supply, plug sensors in, wait ~10 s per sensor while the LED flickers, then read the LED.

### With the serial console

Open the serial port (VS Code: *Serial Monitor* extension; Linux: `picocom /dev/ttyACM0`; any baud rate). You get the banner and this menu:

```
  0. Enumerate attached sensors.
  1. Check temperature alarm function with a safe test according to datasheet.
  2. Discover clones with a safe test, using only function codes documented in the datasheet.
  3. Classify clones with an aggressive test, using undocumented function codes.
  s. Show summary of the last discover run.
  a. Re-run the automatic test now.
  h. Show this menu.
```

After each discover run a summary is printed, for example:

```
  #  Bus   ROM                      Verdict       Dev.  Temp
  1  GP28  28-xx-xx-xx-xx-00-00-xx  GENUINE         0    23.44 oC
  2  GP27  28-xx-xx-xx-xx-xx-xx-xx  CLONE           6    23.81 oC
```

The detailed log above the summary shows every individual check, exactly as the original sketch does.

### Interpreting the results

- **GENUINE** – the sensor behaved like a Maxim/ADI DS18B20 in every safe test. Note: Family A3 clones are specifically designed to deceive; the safe test catches them through their ROM pattern and polling behaviour, but nothing short of buying from an authorised distributor is a guarantee.
- **CLONE** – at least one deviation from the datasheet or from genuine behaviour. Use test **3** to see which family (A2, A3, B1, B2, C, D1, D2, E, F, G, H) it belongs to. The meaning of each family is explained in the [original README](https://github.com/cpetrich/counterfeit_DS18B20#what-families-of-ds18b20-like-chips-can-i-expect-to-encounter).
- **INCONCLUSIVE** – the sensor is in parasite power mode, or <byte 6> could not be verified at the current temperature. Power it from 3V3, warm it with your fingers and press `a`.
- **COMM ERROR** – scratchpad CRC error: check wiring and the pull-up resistor.

> ⚠️ **Test 3** sends undocumented commands. It could conceivably change calibration parameters of clones. Only use it on sensors dedicated to testing. Tests 0–2 use documented commands only and do not write the EEPROM.

## Configuration

All settings live in [`src/config.h`](src/config.h) and can be overridden from `CMakeLists.txt` with `target_compile_definitions(...)`:

| Macro | Default | Description |
|---|---|---|
| `DSCD_BUS_PINS` | `28, 27, 26, 22, 21, 20, 19, 18` | 1-Wire bus pins |
| `DSCD_AUTO_TEST` | `1` | Run the safe test automatically on (hot-)plug |
| `DSCD_SCAN_INTERVAL_MS` | `1000` | Bus scan period |
| `DSCD_STABLE_SCANS` | `2` | Identical scans required before testing (debounce) |
| `DSCD_ENABLE_INTERNAL_PULLUP` | `1` | Enable RP2350 internal pull-ups |
| `DSCD_GENUINE_12BIT_MIN_MS` / `_MAX_MS` | `550` / `660` | Empirical 12-bit conversion time window of genuine parts |
| `DSCD_USB_WAIT_MS` | `2500` | Time to wait for a serial terminal at boot |

## Project layout

```
├── CMakeLists.txt          Pico SDK project (VS Code extension compatible)
├── pico_sdk_import.cmake
├── src/
│   ├── main.c              buses, auto mode, menu, summary
│   ├── config.h            build-time configuration
│   ├── onewire.c/.h        bit-banged 1-Wire master (multi-bus)
│   ├── ds18b20_tests.c/.h  port of the original tests
│   └── status_led.c/.h     LED patterns on core 1 (CYW43)
├── .github/workflows/      CI build + release
├── LICENSE                 Apache License 2.0
└── NOTICE                  attribution
```

## Differences from the original sketch

- Arduino + OneWire library replaced by the Pico SDK and an own 1-Wire driver.
- Multiple buses; stand-alone automatic mode with LED verdict; summary table.
- Conversion times measured in microseconds.
- Alarm test: sign-correct integer temperature below 0 °C.
- The test logic and all thresholds are otherwise unchanged. Every modified source file states its changes in its header (Apache-2.0 §4(b)).

## Disclaimer

Intended for educational purposes. As the original author puts it: there may be circumstances under which the tests permanently damage 1-Wire sensors in obvious or non-obvious ways. The software is provided "AS IS", without warranties of any kind – see the license.

## License

[Apache License 2.0](LICENSE).

- Original sketch and research: Copyright 2024 **Chris Petrich** – https://github.com/cpetrich/counterfeit_DS18B20
- Pico SDK port: Copyright 2026 the pico2w-ds18b20-clone-detector contributors

See [NOTICE](NOTICE) for attribution details.
