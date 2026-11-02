# EP-DJ3000 firmware: ESP32-S3 platform layer

ESP-IDF project that runs the portable core (`../core`, no ESP-IDF inside) on an ESP32-S3: sensors, USB MIDI and USB
audio, the I2S DAC, TRS MIDI, the ESP-NOW link, NVS. The decisions are in
[`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md), the wire contract in [`docs/PROTOCOL.md`](../../docs/PROTOCOL.md).
First power-up: [`docs/BRINGUP.md`](../../docs/BRINGUP.md). Wiring: [`hardware/WIRING.md`](../../hardware/WIRING.md).

**Status: compiles and links for the ESP32-S3; has not run on a chip.**

## Build

Tested toolchain: **ESP-IDF 6.1.0** (what PlatformIO's `espressif32` 8.0.0 installs) with `espressif/esp_tinyusb`
**3.0.0** (it bundles TinyUSB 0.21.0). The code was written for IDF 5.3+ (two version guards: ESP-NOW callback
signature and rate API) but only 6.1.0 has been compiled.

### PlatformIO

```sh
pio run -d firmware/esp32s3                      # build; first run downloads esp_tinyusb from the component registry
pio run -d firmware/esp32s3 -t upload            # flash over the DevKit's UART port
pio device monitor -d firmware/esp32s3           # 921600 baud
```

`platformio.ini` sets `src_dir = main` so PlatformIO and `idf.py` share one tree.

### idf.py

```sh
cd firmware/esp32s3
idf.py set-target esp32s3        # once
idf.py menuconfig                # optional: "EP-DJ3000" menu
idf.py build flash monitor
```

Both read `sdkconfig.defaults`. `idf.py` writes `sdkconfig`, PlatformIO `sdkconfig.esp32s3`; neither is committed.
`dependencies.lock` pins the resolved component versions and is committed. After changing `sdkconfig.defaults`,
delete the generated sdkconfig: IDF only applies defaults to symbols that are not already set.

### Configuration (`menuconfig` -> EP-DJ3000)

| Option | Default | |
|---|---|---|
| `EPDJ_PLATTER_*` | touch ring | first-boot platter source (afterwards the `platterSource` config key) |
| `EPDJ_TOUCH_PAD_COUNT` | 8 | pads wired from GPIO4 up; 1 for an AS5600 build (hold pad) |
| `EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES`, `_INTERVAL_CYCLES` | 500, 16 | touch scan speed versus resolution |
| `EPDJ_ADC_OVERSAMPLE` | 8 | conversions averaged per fader/knob sample |
| `EPDJ_AS5600_ENABLE` | on | probe for the encoder at boot |
| `EPDJ_USB_AUDIO` | on | UAC2 function when a DAC is present at boot |
| `EPDJ_AUDIO_RING_*` | 2048 frames | USB-to-DAC buffer (21 ms target delay) |
| `EPDJ_DAC_PROBE_I2C` | off | also look for codec chips on I2C |
| `EPDJ_TRS_MIDI`, `EPDJ_LINK`, `EPDJ_LINK_CHANNEL`, `EPDJ_LINK_GROUP` | on, on, 1, 0 | |
| `EPDJ_FIRST_BOOT_DECK_*` | A | flash the second unit as B |
| `EPDJ_LOG_SENSORS`, `EPDJ_LOG_PERIOD_S` | on, 10 | diagnostics lines |

With `EPDJ_USB_AUDIO` off the unit is MIDI-only and the audio class driver is configured out of TinyUSB.

## Layout

```
CMakeLists.txt, platformio.ini, sdkconfig.defaults, partitions.csv
main/                 app_main (boot order and the THREADING MODEL comment), engine task, EnginePlatform, diagnostics
components/
  epdj_core/          wrapper: compiles ../../core/src in place, no copy (-Werror -Wconversion stays on)
  epdj_hal/           board_pins.h (+ static_asserts), touch, ADC, buttons, AS5600, NVS ConfigStore, TRS MIDI, LED, DAC detect
  epdj_usb/           descriptors from core/usb_desc, esp_tinyusb install, MIDI, UAC2 control requests and audio interrupt
    tusb_override/      CFG_TUSB_CONFIG_FILE: esp_tinyusb's config + the audio class
  epdj_audio/         USB -> ring -> I2S, rate feedback, drift fallback, volume, test tone
  epdj_link/          ESP-NOW broadcast transport
tools/crosscheck_usb_desc/   byte-compares the descriptor builder with TinyUSB's own macros
```

New `.cpp` files in `../core/src` are picked up automatically by `idf.py` (the wrapper globs with `CONFIGURE_DEPENDS`). PlatformIO ships ninja 1.7.1, too old to re-check that glob: after adding a core source file under `pio`, touch `components/epdj_core/CMakeLists.txt`.

## How UAC2 got enabled

`esp_tinyusb`'s `tusb_config.h` has no audio class and is compiled into TinyUSB itself. TinyUSB includes
`CFG_TUSB_CONFIG_FILE` instead when it is defined, so `components/epdj_usb/CMakeLists.txt` defines it on the
`esp_tinyusb` library (PUBLIC, so everything linking it agrees) as `epdj_tusb_config.h`, which includes the original
and adds the audio class and larger MIDI FIFOs. `esp_tinyusb` 3.0 always compiles `audio_device.c`, so no source list
had to change. The descriptor is `core/src/usb_desc.cpp`'s; it is host-tested and `tools/crosscheck_usb_desc/run.sh`
shows it is byte-identical to what TinyUSB's `TUD_MIDI_*`/`TUD_AUDIO20_*` macros produce (173 and 316 bytes).

Read the **UAC2 at full speed** risk in `docs/BRINGUP.md` before trusting USB audio on macOS.

## Not here

No OTA (update with `REBOOT(bootloader)` from the app, which sets the ROM force-download flag, or the BOOT button), no
BLE-MIDI, no Wi-Fi station/AP, no secrets, no network services.
