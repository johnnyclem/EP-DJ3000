# EP-DJ3000 firmware: ESP32-S3 platform layer

ESP-IDF project that runs the portable core (`../core`, no ESP-IDF inside) on an ESP32-S3: sensors, USB MIDI and USB
audio, the I2S DAC, TRS MIDI, the ESP-NOW link, NVS. The decisions are in
[`docs/ARCHITECTURE.md`](../../docs/ARCHITECTURE.md), the wire contract in [`docs/PROTOCOL.md`](../../docs/PROTOCOL.md).
First power-up: [`docs/BRINGUP.md`](../../docs/BRINGUP.md). Wiring: [`hardware/WIRING.md`](../../hardware/WIRING.md).

**Status: compiles and links for the ESP32-S3; has not run on a chip.**

## Build

Tested toolchain: **ESP-IDF 6.1.0** (what PlatformIO's `espressif32` 8.0.0 installs) with `espressif/esp_tinyusb`
**3.0.0** (it bundles TinyUSB 0.21.0). The code was written for IDF 5.3+ (two version guards: ESP-NOW callback
signature and rate API) but only 6.1.0 has been compiled. `platformio.ini` pins `platform = espressif32@8.0.0`, the
release that produced the verified build (PlatformIO Core 6.2.0; it installs ESP-IDF 6.1.0, GCC 15.2.0 and esptool
5.4.0), so a newer platform release cannot swap the toolchain under you. Moving to a newer platform means re-running
the build and checking for warnings first.

`pio run -d firmware/esp32s3` was run from a clean copy of the tree for this README: it succeeds with no compiler
warnings (the RAM and flash figures print at the end of the build).

### PlatformIO

```sh
pio run -d firmware/esp32s3                      # build; first run downloads esp_tinyusb from the component registry
pio run -d firmware/esp32s3 -t upload            # flash over the DevKit's UART port
pio device monitor -d firmware/esp32s3           # 921600 baud
pio run -d firmware/esp32s3 -t menuconfig        # the "EP-DJ3000" menu below (opens a full-screen TUI)
```

Saving menuconfig writes `sdkconfig.esp32s3` (git-ignored). IDF applies `sdkconfig.defaults` only to symbols that file does
not already set, so after editing the defaults delete the generated file. **That includes the Kconfig defaults, and contract
v1.3 changed two of them** (first-boot platter AS5600 instead of the touch ring, one touch pad instead of eight): a checkout
that was built before keeps `CONFIG_EPDJ_PLATTER_TOUCH_RING=y` and `CONFIG_EPDJ_TOUCH_PAD_COUNT=8` in its old
`sdkconfig.esp32s3`, so delete that file (or pick the values in `menuconfig`) to get the new build. A unit that already saved
its configuration to flash keeps its saved `platterSource` as well: `LOAD_DEFAULTS`, then `SAVE_CONFIG` (the boot log says so).

**Two USB-C cables.** The DevKitC-1 has two ports. **UART** (the CP210x bridge) is for flashing and the log. **USB** is the
chip's native port, which the firmware gives to TinyUSB as soon as it boots, so from the first flash on, the Mac sees a
MIDI device there and no serial port. Keep both cables plugged in while developing. If the firmware ever hangs the native
port, flash over UART, or hold BOOT while pressing reset. The status LED is on GPIO38 on DevKitC-1 v1.1 boards (the
default here) and GPIO48 on v1.0: pick the revision with `EPDJ_BOARD_REV` (`WIRING.md`). A wrong pick leaves the LED dark;
the boot log line `led: WS2812 on GPIO..` says which pin was driven.

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
| `EPDJ_BOARD_DEVKITC_1_V1_1` / `_V1_0` | v1.1 | DevKitC-1 revision: status LED on GPIO38 (v1.1) or GPIO48 (v1.0), feeds `kStatusLed` in `board_pins.h` |
| `EPDJ_PLATTER_*` | AS5600 | first-boot platter source (afterwards the `platterSource` config key): the AS5600 encoder under a free-spinning platter, or, EXPERIMENTAL, the touch ring, or hold-only |
| `EPDJ_TOUCH_PAD_COUNT` | 1 | pads wired from GPIO4 up: 1 = the hold pad of the AS5600 build (GPIO4); 0 = no hold pad (jog works, no touch note ever); 3 to 8 for the experimental ring |
| `EPDJ_TOUCH_SCAN_TARGET_US` | 800 | boot-time auto-range: charge/discharge cycles are chosen so one scan of all pads takes about this long (0 = off) |
| `EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES`, `_INTERVAL_CYCLES` | 500, 16 | cycles per pad: the upper limit for auto-range, or the fixed value with the target at 0; sleep between measurements |
| `EPDJ_ADC_OVERSAMPLE` | 8 | upper limit on conversions averaged per fader/knob sample; boot lowers it so that the ADC conversions plus the AS5600 read fit 400 us of each millisecond |
| `EPDJ_AS5600_ENABLE` | on | probe for the encoder at boot (I2C SDA GPIO21, SCL GPIO47, address 0x36); needed by the default AS5600 platter |
| `EPDJ_USB_AUDIO` | on | USB Audio 1.0 function when a DAC is present at boot |
| `EPDJ_AUDIO_RING_*` | 2048 frames | USB-to-DAC buffer (21 ms target delay) |
| `EPDJ_DAC_PROBE_I2C` | off | also look for codec chips on I2C |
| `EPDJ_TRS_MIDI`, `EPDJ_LINK`, `EPDJ_LINK_CHANNEL` | on, on, 1 | |
| `EPDJ_LINK_GROUP` | 0 | build-time default rig id; the `linkGroup` config key (SysEx, live, saved) wins when non-zero |
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
  epdj_usb/           descriptors from core/usb_desc, esp_tinyusb install, MIDI, USB audio control requests and audio interrupt
    tusb_override/      CFG_TUSB_CONFIG_FILE: esp_tinyusb's config + the audio class
  epdj_audio/         USB -> ring -> I2S, rate feedback, drift fallback, volume, test tone
  epdj_link/          ESP-NOW broadcast transport
tools/crosscheck_usb_desc/   byte-compares the descriptor builder with TinyUSB's own macros; runs its audio and MIDI drivers on the host
tools/host_checks/           host tests of the output policy, tick pacing, touch auto-range, and the AS5600 magnet decode and sensor time budget
```

New `.cpp` files in `../core/src` are picked up automatically by `idf.py` (the wrapper globs with `CONFIGURE_DEPENDS`). PlatformIO ships ninja 1.7.1, too old to re-check that glob: after adding a core source file under `pio`, touch `components/epdj_core/CMakeLists.txt`.

## How USB audio got enabled

`esp_tinyusb`'s `tusb_config.h` has no audio class and is compiled into TinyUSB itself. TinyUSB includes
`CFG_TUSB_CONFIG_FILE` instead when it is defined, so `components/epdj_usb/CMakeLists.txt` defines it on the
`esp_tinyusb` library (PUBLIC, so everything linking it agrees) as `epdj_tusb_config.h`, which includes the original
and adds the audio class and larger MIDI FIFOs. `esp_tinyusb` 3.0 always compiles `audio_device.c`, so no source list
had to change.

The audio function is **USB Audio Class 1.0**, not 2.0 (`docs/ARCHITECTURE.md` decision 5): the ESP32-S3 is full-speed
only, and TinyUSB's UAC1 driver sends the feedback value as three bytes 10.14 the way a full-speed host expects, where its
UAC2 driver always sends four bytes 16.16. The firmware hands `tud_audio_n_fb_set()` a Q16.16 value that is already a
multiple of 4, so the driver's `>> 2` is exact and the wire bytes are `FeedbackController::encodeUsb()`'s.

The descriptor is `core/src/usb_desc.cpp`'s (host-tested).

### Sizing of the SysEx reply path

A `GET_CONFIG` dump (`0xFF`) answers one `CONFIG` frame per key (33 keys, ids 0-32) and then `ACK[GET_CONFIG]`: 34 SysEx
frames, 506 bytes on the wire.

| Stage | Size | Dump needs | |
|---|---|---|---|
| engine output ring (`Engine::kOutQueueBytes`) | 4096 B, records carry a 4-byte header | 642 B | `static_assert` in `core/src/engine.cpp` |
| USB-MIDI packets (4 bytes per 3 bytes of SysEx) | TX FIFO `CFG_TUD_MIDI_TX_BUFSIZE` = 1024 B | 169 packets = 676 B | `static_assert` in `usb_device.cpp`; it was 512 B, which refuses 40 of the 169 packets in one go |
| engine task, per 1 ms iteration | up to 64 output events | 34 events | `kMaxOutPerIteration` |

The whole dump therefore goes into the FIFO in the iteration that produced it. If the host is slow to read the port, the
engine task keeps one event in hand and retries every millisecond (`usb: ... tx stalls`); only after 100 ms does it give up
on the rest of that frame (`drops`), and a dump that ends without its ACK is incomplete by definition. After that give-up the
USB sink counts as stalled: every later event is dropped on its first refused packet (still `drops`), and the first accepted
packet clears the state, so a host that never reads the MIDI port (some hosts only read it while an application has it open) costs TRS MIDI and the radio one 100 ms window instead of 100 ms per event. Packets already in the FIFO stay there and reach
the first reader that opens the port. Requests from the
host are small and sequential (the app waits for each answer), so the 256-byte RX FIFO and the 128-packet receive queue
are not close to their limits.

### A one-line patch to TinyUSB's audio driver

TinyUSB's `audiod_rx_xfer_isr` returns before re-arming the isochronous OUT endpoint when a zero-length packet arrives, and
that ends the stream until the host re-selects the interface. `components/epdj_usb/tinyusb_audio_patch.cmake` applies the
fix (skip the FIFO write for an empty packet) to a copy of `audio_device.c` in the build directory at configure time, and
`CMakeLists.txt` swaps the copy into the `esp_tinyusb` library's sources. The managed component is never edited. If
`esp_tinyusb` is updated and the lines have moved or been fixed, CMake stops with a message saying so. Worth reporting upstream.

### Host checks of the USB layer

```sh
cd firmware/esp32s3 && sh tools/crosscheck_usb_desc/run.sh    # needs the managed_components download from one build; no chip
```

1. The descriptor builder's bytes against the same descriptors written with TinyUSB's own `TUD_MIDI_*` /
   `TUD_AUDIO10_*` macros (173 bytes MIDI only, 294 with audio).
2. TinyUSB's real `audio_device.c` and `midi_device.c`, compiled for the host with the firmware's own TinyUSB
   configuration, open those descriptors; `usb_audio.cpp` (unmodified) answers the audio driver. It checks which
   endpoints open, the feedback bytes on the wire for 20 000 values against `encodeUsb`, that OUT packets reach
   `pushFromUsb`, the mute / volume / sampling frequency requests (including the ones that must stall), and that a real
   `GET_CONFIG` dump produced by `ConfigServer` fits the MIDI TX FIFO and comes out of the bulk IN endpoint byte for byte.
   Only the layer below the class drivers (endpoint transfers, control status) is a recorder.

   It also feeds the driver a zero-length packet and checks that the endpoint is re-armed and the next packet arrives
   (`EPDJ_TINYUSB_UNPATCHED=1` runs TinyUSB's own file, where that check fails).

Neither is a substitute for a host: see `docs/BRINGUP.md`.

### Host checks of the platform logic

```sh
cd firmware/esp32s3 && sh tools/host_checks/run.sh            # no chip, no ESP-IDF
```

The pure parts of the platform layer are headers with no ESP-IDF in them, compiled here against fakes with the core's test
harness (AddressSanitizer + UBSan): `main/output_pump.h` (what the engine task does with each output event, including a USB
endpoint that never drains), `main/tick_pacing.h` (when the engine task drops lost ticks), `epdj_hal/touch_range.h` (the
touch measurement-length fit) and `epdj_hal/sensor_budget.h` (the AS5600 magnet-status decode, and how the 400 us of sensor
time per millisecond is split between the I2C read and the ADC). What they cannot cover is the FreeRTOS, USB, I2C and touch
hardware behaviour around them.

## The platter at boot, and what the engine task costs

The default build reads an AS5600 over I2C and ONE touch pad (GPIO4, the hold pad). `app_main` prints one line for it and
warns about everything that would make the platter misbehave:

```
platter: source AS5600 | AS5600 answered at 0x36, magnet detected, read <N> us | touch pads wired: 1 (GPIO4 = hold pad)
platter: source AS5600 | AS5600 NOT answering at 0x36 (SDA GPIO21, SCL GPIO47) | touch pads wired: 1 (GPIO4 = hold pad)
!!! platterSource is AS5600 but no encoder answered (nothing ACKed the address): JOG WILL BE SILENT. Check the I2C wiring (SDA GPIO21, SCL GPIO47, 3V3, GND), the breakout's 3.3 V/5 V strap and the magnet gap (1-3 mm), then reboot !!!
```

(Those lines are the format, not output from a chip: this firmware has never run on one.) Magnet status comes from the
AS5600 STATUS register: `detected`, `too weak` (move it closer, check it is diametrically magnetised), `too strong` (move it
away) or `not detected`, each with its own WARN. Further WARNs: an encoder answers but `platterSource` is not AS5600 (a unit
that saved TOUCH_RING before v1.3); the AS5600 build with no hold pad (no touch note, so DJ apps never enter scratch mode); a
ring configured with fewer than 3 pads. The once-a-second `sens:` line shows the angle (`(bad)` when the last read failed,
`(none)` without an encoder), and a `WARN` follows whenever the count of failed I2C reads grows.

Without an answering encoder the unit stays silent on jog: the engine task leaves `angleValid = false` and the engine ignores
`angle12`. It is not retried while running; fix the wiring and reboot.

**Cost of the encoder read, per 1 ms engine tick** (assumptions; the boot measures the real figure and prints it in the `adc:` line):
one register read at the 400 kHz the device is configured for is a write of the register address, a repeated start and a
read of two bytes, about 45 SCL clocks (112 us on the wire plus start/stop), and the IDF driver adds interrupt, event-group
and task-switch time, so about 150 to 200 us. Unlike an ADC conversion it does not run with interrupts off: the engine task
sleeps on the driver's semaphore, so TinyUSB (same core) keeps running. Both still come out of the same 1 ms, so
`AdcInputs::begin(i2cReadUs)` takes the measured read off the 400 us sensor budget (`sensor_budget.h`) and lowers the ADC
oversampling to fit, with a WARN if even one conversion per channel does not. The encoder is read first in each tick, so its
timestamp is the tick's, not 400 us later. A failed read can block for its 2 ms timeout; two in a row back the bus off for
100 ms (`As5600::readAngle`), at most 4 ms lost in 100. None of this has been timed on a chip.

## Not here

No OTA. Update over the DevKit's UART port, or with BOOT + reset. `REBOOT(bootloader)` from the app stops the USB stack, hands
the native port back to the chip's USB-Serial-JTAG (the ROM's download port) and sets the ROM force-download flag, but that
sequence has not been tried on a chip: do not rely on it until `docs/BRINGUP.md` step 2 has been ticked. No
BLE-MIDI, no Wi-Fi station/AP, no secrets, no network services.
