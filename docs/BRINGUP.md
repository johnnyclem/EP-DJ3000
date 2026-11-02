# EP-DJ3000 bring-up checklist

The order of [`ARCHITECTURE.md`](ARCHITECTURE.md) section 6, with what to expect in the log at each step. **Nothing in
the firmware has run on a chip yet.** It compiles and links for the ESP32-S3 (PlatformIO and `idf.py`, ESP-IDF 6.1.0 with
esp_tinyusb 3.0.0 = TinyUSB 0.21.0), and the portable logic is covered by host tests (the USB audio class driver too: see
the last risks), so this checklist is where reality gets to disagree. Write down every
deviation: the "Risks" at the end say which ones are most likely.

Hardware: [`hardware/WIRING.md`](../hardware/WIRING.md), [`hardware/PLATTER.md`](../hardware/PLATTER.md). There is no
enclosure for this design yet ([`hardware/README.md`](../hardware/README.md)): bring it up on the bench.
Firmware build and flash: [`firmware/esp32s3/README.md`](../firmware/esp32s3/README.md).

**The five things most likely to invalidate part of the design, in order of damage.** Everything else in this file is
routine next to them; prove these first and write the numbers down.

1. **Touch signal-to-noise on real conductive PLA** at the 0.8 ms auto-ranged scan (step 1, risks 5 and 10, `PLATTER.md`).
   The platter is the headline feature and nobody has seen a real count from it.
2. **USB Audio 1.0 with a feedback endpoint on a real host** (step 4, risks 1-3). Try macOS Sound settings first, then
   Linux `aplay`, then Windows.
3. **ESP-NOW latency, jitter and loss** under the bridge and time sync (step 5). Every figure in `LINK.md` is simulated.
4. **`REBOOT(bootloader)`**, the only advertised update path other than the UART port (step 2). Never tried.
5. **Noticing a pulled USB cable without VBUS sense**, which one-cable mode depends on (step 5, risk 9). Decide early
   whether to add the VBUS divider to the pin plan (`WIRING.md`) rather than rely on an untested idle-detect heuristic.

## 0. Before power

* [ ] Continuity: no short 3V3-GND; each of GPIO4..11 open to GND and to each other (sectors isolated); CUE/REV open
  until pressed.
* [ ] Fader and knob ends on 3V3 and GND, wipers to GPIO1 / GPIO2.
* [ ] Platter sectors: adjacent sectors read open on a multimeter.
* [ ] If a DAC is fitted: SCK to GND, XSMT high, FMT/FLT/DEMP low, strap GPIO14-GND **removed** for now (step 4 adds it).

## 1. Flash and watch the log

Connect the DevKit's **UART** port, flash, open the monitor at **921600** baud (`pio device monitor` is preset;
`idf.py monitor` follows the build's console baud rate).

Expected, in this order (the numbers are illustrations, not measurements: nothing has run on a chip; yours will differ):

```
main: EP-DJ3000 core 0.1.0, protocol v1, ESP-IDF v6.1.0
main: config: nothing saved; deck A, platter source 1
adc: fader GPIO1 raw 2031, knob GPIO2 raw 1180; 24 us per conversion, x8 oversampling (384 us per ms)
touch: scan 9760 us at 500 cycles, 2540 us at 125: target 800 us -> 34 cycles
touch: 8 pad(s) on GPIO4..11, 34 cycles per pad, idle peak count 1452, shift 0
touch: full scan of 8 pads takes 790 us (idle)
main: no AS5600 on I2C                       (or: as5600: found at 0x36, status 0x20 (magnet detected))
main: dac: mode AUTO, strap low 0/16 samples, codec none -> absent (MIDI only)
usb: "EP-DJ3000 Deck A" serial 7CDFA1xxxxxx, config 173 bytes, VID:PID 1209:0001, audio off
radio: ESP-NOW on channel 1, station 7C:DF:A1:xx:xx:xx, broadcast peer
trs: UART1 31250 baud, TX GPIO40 RX GPIO41
main: link group 0 (build default)
main: ready: caps 0x005A, serial A1xxxxxx, MIDI channel 1
```

What to look for:

* [ ] `ready` appears and the LED turns on (deck A blue, dim while the platter is untouched). The pin plan puts the
  WS2812 on GPIO48, which is where DevKitC-1 v1.0 has it; **v1.1 boards have it on GPIO38**, so on those there is no
  LED until `kStatusLed` in `board_pins.h` is changed (`WIRING.md`).
* [ ] `adc:` reports the measured cost of one conversion. The engine does 2 x oversampling of them every millisecond with
  interrupts off, so the firmware lowers the oversampling (and warns) when that would exceed 400 us. Write the figure down.
* [ ] `touch:` timed one full scan of the pads at two settings and chose the charge/discharge cycles that make it take
  about 0.8 ms (`EPDJ_TOUCH_SCAN_TARGET_US`). The pads are measured one after another, so this is how often a pad gets a new
  value: the default of 500 cycles would be roughly 10 ms for eight pads, i.e. each pad stale for ten 1 ms reads.
  `full scan of N pads takes ...` must be under 1.2 ms; the warning `scan is slower than the 1 ms engine tick` and
  `could not time the scan` (the scan register did not move: 48 cycles are used as a guess) both mean look here first.
  `limit reached: target not met` means even the shortest measurement (8 cycles) is too slow, or the pads are so small that
  500 cycles already scan faster than the target.
* [ ] `idle peak count` is now in the low thousands (it scales with the cycles chosen). The warning `idle counts are tiny:
  pads not connected` means open pads or too few charge/discharge cycles. **Check the finger rise against the idle noise
  at this count** (PLATTER.md "Tuning"): the shorter measurement is the price of a fresh value on every read, and nobody has
  seen the signal-to-noise on real conductive PLA yet. If it is too noisy, raise `EPDJ_TOUCH_SCAN_TARGET_US` (every pad then
  repeats its value for a few reads, the engine's 1 kHz tick still works) rather than the cycle count.
* [ ] `shift` is 0 unless the counts exceed 32768; then they are halved to fit 16 bits (harmless).
* [ ] `dac:` says how the DAC decision was made. In AUTO the strap pin (GPIO14) is sampled 16 times over 16 ms and counts
  only if every sample is low; a flickering strap prints `(UNSTABLE: wire or contact?)` and counts as absent.
* [ ] `link group` names the radio rig id in force: the `linkGroup` config key when it is non-zero, otherwise the build
  default (`EPDJ_LINK_GROUP`, 0).
* [ ] `caps 0x005A` = touch ring (0x02) + TRS MIDI (0x08) + link (0x10) + LED (0x40). `0x0001` is added with an AS5600,
  `0x0020` with the USB audio function (the engine adds DAC `0x0004` in `Info`).
* [ ] The reset loop with `Guru Meditation` or `assert failed`: note the line, and run the backtrace through
  `pio device monitor` (decoder preset) or `idf.py monitor`.
* [ ] Nothing printed after `ready` except one `diag: sens:` line per second. The engine task itself never logs.

Once a second:

```
diag: sens: fader=2031 knob=1180 angle=0(none) cue=0 rev=0 touch=[1452 1456 1427 1435 1455 1446 1424 1441]
```

* [ ] Move the fader and knob end to end: both span most of 0..4095 and move smoothly. A fader that does not reach
  both ends is calibrated in step 2, not fixed in wiring.
* [ ] Press CUE, REV: `cue=1` / `rev=1`.
* [ ] Put a finger on each sector in turn: that sector's count rises, a neighbour's a little. Write down idle
  noise (peak-to-peak) and finger rise for each sector (PLATTER.md "Tuning").
* [ ] Every 10 s: `diag: loop: ... N over 1 ms (max M us), K late wake-ups, R resyncs`. A healthy loop has 0 overruns and
  a max well under 1000 us. If it does not: the ADC conversions are the biggest single cost (the `adc:` line says how much
  budget they take), then a stuck I2C bus (AS5600: up to 4 ms per 100 ms), then what shares core 1. `resyncs` counts the
  times the loop was more than two ticks behind and dropped the lost ticks instead of running flat out; it must stay 0.

## 2. macOS app: sensors and calibration

No Mac, or the app does not build: do the same with SysEx by hand (section 2b).

Connect the native **USB** port to the Mac. In the log:

```
usb: mounted by host
```

* [ ] The Mac lists "EP-DJ3000 Deck A" under Audio MIDI Setup -> MIDI Studio with three ports ("Deck", "TRS MIDI",
  "Linked deck"). `system_profiler SPUSBDataType` shows VID 0x1209, PID 0x0001 and the serial.
* [ ] App Test tab: telemetry streams (`STREAM`). Fader and knob bars follow the controls. Buttons light.
* [ ] Calibrate fader and knob (sweep end to end); `CAL_END(apply)` reports a span above 1500 counts.
* [ ] Touch ring: set `touchOnPermille`/`touchOffPermille` per PLATTER.md; `SAVE_CONFIG`. On the monitor nothing
  new appears, but the next boot prints `config: ok` instead of `nothing saved`.
* [ ] Spin a finger around clockwise: `jogTotal` increases. If not, `jogInvert = 1`.
* [ ] `IDENTIFY` makes the LED blink white.
* [ ] Settings tab (or any `GET_CONFIG 0xFF`): 33 keys (ids 0-32) arrive, then `ACK[GET_CONFIG]` as the end marker; a dump
  that stops without the ACK is incomplete. The dump is 34 SysEx frames = 169 USB-MIDI packets = 676 bytes; the MIDI TX FIFO
  is 1024 bytes, so the whole dump fits in one go. The next `usb:` stats line should show `tx stalls` and `drops` unchanged.
  If they climb, the host is not reading the port fast enough (drops mean the rest of that frame was abandoned after
  100 ms; once that has happened every later event is dropped at its first refused packet until the host reads again).
* [ ] **Nobody reading the MIDI port.** Close every MIDI application, then move the platter and fader for 30 s while
  watching a second terminal with the TRS MIDI output (or the other deck's log): TRS and the link must keep their full rate.
  The `usb:` line shows `drops` climbing and `tx stalls` stopping at about 100 (one give-up window); open a MIDI monitor
  afterwards and note how much stale traffic it receives at first (up to 256 packets sit in the FIFO).
* [ ] **`REBOOT(bootloader)`** (app: Settings -> Reboot to bootloader). Never tried on a chip. Expected: the Mac's "EP-DJ3000"
  disappears, then a "USB JTAG/serial debug unit" (303a:1001) appears and `esptool --before no-reset --port <it> chip-id`
  answers. If the Mac sees nothing, the PHY was not handed back (`usb_dev::releaseForRomDownload`): use the UART port or
  BOOT + reset to flash, and tell the owner. Also try it on a unit with a DAC (audio function mounted).
* [ ] A flash write happens on `SAVE_CONFIG`: expect a brief hiccup in the engine tick (a `late wake-up` in the
  stats line is normal at that moment).

## 2b. Without the macOS app

The Mac app has never been compiled ([`macos/README.md`](../macos/README.md)), and Windows and Linux have no app at all.
Everything bring-up needs from it is plain SysEx ([`PROTOCOL.md`](PROTOCOL.md) section 3), so any program that can send
raw SysEx to the unit's **Deck port (USB-MIDI cable 0**, not "TRS MIDI" or "Linked deck") and show what comes back will
do: SysEx Librarian or MIDI Monitor on macOS, MIDI-OX on Windows, `amidi -l` then `amidi -p hw:N,0 -S 'F0 7D ...'` on
Linux, or `sendmidi` anywhere. **None of those programs was run here and nothing was sent to a unit.** What was run: every
frame below, built with the snippet below, was fed to the real firmware `Engine` through the end-to-end simulator
(`docs/E2E.md`) and answered as the third column says, and the snippet reproduces `protocol/test-vectors.json` exactly.

The frames are addressed to deck `7F` (any), so every unit on that port acts on them. With two units on USB, address one:
change the deck argument (0 = A, 1 = B); the checksum depends on it, so build the frame with the snippet instead of editing
the bytes. Frames are shown as the bytes a SysEx tool sends, `F0 ... F7`.

| What | Frame | Reply |
|---|---|---|
| Identify the unit (GET_INFO) | `F0 7D 45 44 01 7F 01 7F F7` | `INFO` (cmd `41`) |
| Read all 33 config values (GET_CONFIG) | `F0 7D 45 44 01 7F 02 01 7F 7E F7` | 33 x `CONFIG` (cmd `42`), then `ACK` for cmd 2 |
| Start telemetry at 20 Hz (STREAM) | `F0 7D 45 44 01 7F 06 00 14 66 F7` | `ACK`, then `TELEMETRY` (cmd `47`) frames for 5 s; send it again to keep it going |
| Stop telemetry | `F0 7D 45 44 01 7F 06 00 00 7A F7` | `ACK` |
| Fader calibration: start (CAL_BEGIN fader) | `F0 7D 45 44 01 7F 07 00 00 79 F7` | `ACK` |
| Fader calibration: finish and apply (CAL_END fader) | `F0 7D 45 44 01 7F 08 00 00 01 77 F7` | `CAL_RESULT` (cmd `48`), or `NAK` (cmd `7F`) with error 9 if the sweep was under 1500 counts |
| Knob calibration: start | `F0 7D 45 44 01 7F 07 00 01 78 F7` | `ACK` |
| Knob calibration: finish and apply | `F0 7D 45 44 01 7F 08 00 01 01 76 F7` | `CAL_RESULT` or `NAK` |
| Slide pot as tempo fader (SET_CONFIG faderRole = PITCH) | `F0 7D 45 44 01 7F 03 00 0B 02 00 00 00 70 F7` | `ACK` |
| Slide pot as channel volume again (faderRole = 1) | `F0 7D 45 44 01 7F 03 00 0B 01 00 00 00 71 F7` | `ACK` |
| Flip the jog direction (jogInvert = 1) | `F0 7D 45 44 01 7F 03 00 04 01 00 00 00 78 F7` | `ACK` |
| Clock master mode (mode = SYNC_MASTER) | `F0 7D 45 44 01 7F 03 00 02 01 00 00 00 7A F7` | `ACK` |
| Back to DJ controller (mode = DJ) | `F0 7D 45 44 01 7F 03 00 02 00 00 00 00 7B F7` | `ACK` |
| Emit MIDI Time Code in SYNC_MASTER (mtcEnable = 1) | `F0 7D 45 44 01 7F 03 00 18 01 00 00 00 64 F7` | `ACK` |
| Keep what you set (SAVE_CONFIG) | `F0 7D 45 44 01 7F 04 7C F7` | `ACK`, or `NAK` error 10 if flash failed |
| Factory defaults, keeping the deck (LOAD_DEFAULTS) | `F0 7D 45 44 01 7F 05 7B F7` | `ACK`; then `SAVE_CONFIG` to persist |
| Blink the LED for 5 s (IDENTIFY) | `F0 7D 45 44 01 7F 0D 00 05 6E F7` | `ACK` |
| Who is on the radio (LINK_STATUS) | `F0 7D 45 44 01 7F 0B 75 F7` | `LINK` (cmd `4B`) |
| Make the unit that answers to deck A become deck B (SET_DECK 1) | `F0 7D 45 44 01 00 09 00 01 75 F7` | `ACK`, then the unit restarts as Deck B |

Replies are `F0 7D 45 44 01 <deck> <cmd> ... F7`: `7E` is an ACK (payload = the command it answers), `7F` a NAK (command,
then error code: `PROTOCOL.md` has the table). For the calibration frames, the sweep happens between the two frames: move
the control end to end, slowly, and dwell at each end.

**Building other frames, and reading telemetry.** Python 3, standard library only. `frame` builds any request;
`telemetry` decodes one `TELEMETRY` frame captured as hex (`touchRaw` is what the touch-ring recipe in `PROTOCOL.md` section
3.2 and `PLATTER.md` "Tuning" need).

```python
import struct

def frame(deck, cmd, payload=()):
    raw, sept = list(payload), []
    for i in range(0, len(raw), 7):                      # 8 -> 7 packing
        grp = raw[i:i + 7]
        sept.append(sum((b >> 7) << j for j, b in enumerate(grp)))
        sept += [b & 0x7F for b in grp]
    body = [1, deck, cmd] + sept
    return [0xF0, 0x7D, 0x45, 0x44] + body + [(-sum(body)) & 0x7F, 0xF7]

def set_config(deck, key, value):                        # key ids: PROTOCOL.md section 3.1
    return frame(deck, 0x03, [key] + list(value.to_bytes(4, "little", signed=True)))

def unpack(sept):
    out = []
    for i in range(0, len(sept), 8):
        hdr, grp = sept[i], sept[i + 1:i + 8]
        out += [b | ((hdr >> j) & 1) << 7 for j, b in enumerate(grp)]
    return bytes(out)

def telemetry(hexstr):                                   # one 0x47 frame, e.g. "F0 7D 45 44 01 00 47 ..."
    f = bytes.fromhex(hexstr)
    assert f[6] == 0x47, "not a TELEMETRY frame"
    t = struct.unpack("<4B6Hi8H", unpack(f[7:-2]))
    names = ("seq", "buttons", "flags", "transport", "faderRaw", "knobRaw", "faderOut", "knobOut", "angle", "bpmX10", "jogTotal")
    return dict(zip(names, t), touchRaw=t[11:])

print(" ".join(f"{b:02X}" for b in set_config(0x7F, 6, 20)))   # touchOnPermille = 20 for every unit
```

Typical use, for step 2 of this file: send `STREAM`, copy a few `TELEMETRY` frames out of the SysEx tool, decode them
with `telemetry()`, work out the thresholds, send them with `set_config(deck, 6, on)` and `set_config(deck, 7, off)`, then
`SAVE_CONFIG`. It is slower than the app's wizard and has the same result.

## 3. MIDI

* [ ] MIDI monitor on the "Deck" port, channel 1: fader -> CC 7 (+39) (the default `faderRole` is CHANNEL_VOLUME; CC 23
  (+55) after `faderRole = CROSSFADER`), knob -> CC 24 (+56), CUE -> note 36,
  REV -> note 37, finger down/up -> note 38, platter motion -> CC 22 around 64.
* [ ] A quick tap on the platter sends note 39 (play pulse).
* [ ] The app's built-in player: load a track, scratch with a finger. Direction and feel are the things to judge;
  note the platter dead zone (tick threshold) for later tuning.
* [ ] SYNC_MASTER (hold CUE+REV 2-5 s, release): MIDI clock 0xF8 at the set tempo on the Deck port, start/stop with a
  platter tap. Back to DJ the same way.
* [ ] TRS MIDI: loop a cable from the TRS jack to a MIDI interface or the EP-133. Check the idle level first
  (WIRING.md). Bytes sent on the USB "TRS MIDI" port come out of the jack and vice versa. Log: `trs: rx overflows 0,
  tx dropped 0`.

## 4. DAC and USB audio

1. Power off, fit the DAC strap (GPIO14 to GND), power on. Log changes to:

```
main: dac: mode AUTO, strap low 16/16 samples, codec none -> present
audio: I2S BCK GPIO15 WS GPIO16 DOUT GPIO17, 48000 Hz 16-bit stereo, ring 2048 frames (42 ms), prime at 1024
usb: "EP-DJ3000 Deck A" serial ..., config 294 bytes, VID:PID 1209:0001, audio UAC1 on
main: ready: caps 0x007A ...
```

* [ ] The Mac shows an audio output named after the USB product, "EP-DJ3000 Deck A" (Sound settings: 2 channels, 48 kHz,
  volume slider); the function's own string is "EP-DJ3000 Audio". No new device, or MIDI vanished: the host rejected
  the configuration descriptor. Capture `log show` / `dmesg` / USBPcap, and see Risks.
* [ ] A loose strap: remove the jumper halfway, or touch the pin with a finger: a partial count such as `strap low 7/16 ... (UNSTABLE ...)` and
  no audio function (MIDI only) is the intended outcome. `dacMode = ON` forces the audio function, OFF removes it; both
  take effect after a reboot.
* [ ] `TEST_TONE` from the app (Test tab): a clean sine on the line out, no clicks at start/stop. Log:
  nothing; the audio task needs no prints. Scope the I2S lines if silent: BCK 1.536 MHz, LRCK 48 kHz.
* [ ] Play music to the unit's output ("EP-DJ3000 Deck A") from any app, 48 kHz. Log (each 10 s):

```
diag: audio: streaming running fill 1030 frames, usb frames 480000, over 0 under 0, reprimes 0, drift 31.2 ppm, fb 48.0015 smp/frame, slips -0 +0
```

* [ ] `fill` hovers near 1024 (half the 2048-frame ring) and stays there for minutes; `over`/`under` stay 0;
  `drift` settles to a stable value (tens of ppm); `slips` stays 0.
* [ ] `fill` runs away to 0 or 2048 and `slips` count up: the host is ignoring the feedback endpoint. That is what
  the slip compensator is for, but it should not be needed; note which host.
* [ ] Windows/Linux, if available: same checks. Linux: `aplay -l`, `speaker-test -c2 -r48000 -D hw:...`.
* [ ] Volume slider and mute on the host change the level (USB audio 1.0 feature unit: master range -80 dB..0 in 1 dB steps).
* [ ] Unplug and replug while streaming. The firmware cannot see VBUS (bus-powered PHY, no sense pin), so a pulled cable is
  noticed only as the bus going idle: `usb: bus idle (suspended, or the cable was pulled)` within a few ms, and about a
  second later the stats line says `suspended (no host)` and the engine stops sending to USB. On replug the host
  re-enumerates (`usb: mounted by host`), the audio state is cleared (`audio: idle`), and `fill` re-primes when the stream
  starts again. Expected without a reboot; not verified. A host that suspends an idle port (Windows selective suspend)
  produces the same messages and `bus active again` on resume.

## 5. Second unit and the link

Flash the second unit with `EPDJ_FIRST_BOOT_DECK_B` (`pio run -d firmware/esp32s3 -t menuconfig` -> EP-DJ3000; or hold
CUE+REV 5 s on a unit to swap A/B), no USB host needed on it beyond power.

* [ ] Both logs show `radio: ESP-NOW on channel 1` and, within about a second, in the 10 s stats:
  `link: up, peers 1, radio rx ... tx ...` on both. LED stops the slow "searching" blink.
* [ ] Both units claim the same deck: the LED alternates with red and `LINK_STATUS` reports the conflict (fix with
  `SET_DECK`).
* [ ] **One-cable mode**: unplug the data cable from deck B (power it from a charger). Deck B's log shows
  `usb: bus idle (...)`, and about a second later (the debounce) its `usb:` line says `suspended (no host)` and its link flags
  drop USB_HOST. On deck A's log `link: up, bridging`; the DJ software sees deck B's controls on the "Linked deck" port
  (cable 2) on MIDI channel 2. If B keeps power through its UART port instead, nothing changes in how it is noticed.
  If B never reports `bus idle`, the idle detection does not work without VBUS and one-cable mode needs the VBUS divider
  (below, risk 9).
* [ ] Latency: round-trip from a jog on B to a MIDI event at the host should be a few ms (docs/LINK.md target
  <= 5 ms typical). Measure it; it is a prediction.
* [ ] SYNC: deck A in SYNC_MASTER, deck B SYNC_FOLLOW: B's beat flash follows A's.
* [ ] Check `radio ... tx ... (queue full 0, failed 0)` stays at 0 under motion.
* [ ] Rig ids: `SET_CONFIG linkGroup = 1` on deck B only. Within about 1.5 s both units show `searching` and the
  `other-group` counters in their `link:` lines climb; set 1 on deck A as well and they link again, no reboot needed
  (`SAVE_CONFIG` keeps it, and after a reboot the log says `link group 1 (linkGroup config key)`). A unit built with a non-zero
  `EPDJ_LINK_GROUP` cannot be put back on group 0 over SysEx.

## 6. DJ software

* [ ] Load the Mixxx mapping first (`drivers/mixxx`), then the others (`drivers/README.md`). The slide pot is a channel
  volume by default, which does nothing audible in VirtualDJ, djay Pro or rekordbox with an external mixer: set `faderRole`
  to `PITCH` first (frame below).
* [ ] Audio, whichever topology you chose (`ARCHITECTURE.md` section 1). **A**: two cables, route deck 1 to "EP-DJ3000 Deck
  A" and deck 2 to "EP-DJ3000 Deck B", each line out to a mixer channel; whether the app can drive two USB sound cards at
  once is the thing to find out. **B**: units MIDI-only, deck 1 and deck 2 to two outputs of one multi-channel interface.

## Risks, in the order I would check them

1. **USB audio on a real host (UAC1, asynchronous, explicit feedback).** The unit is USB Audio Class 1.0 because the
   ESP32-S3 is full-speed only and TinyUSB's UAC1 driver is the one that sends the feedback value the way a full-speed
   host expects (3 bytes, 10.14); its UAC2 driver sends 4 bytes 16.16 at any speed (ARCHITECTURE.md decision 5). The
   descriptor is byte-identical to TinyUSB's own `TUD_AUDIO10_*` macros in the layout of its full-speed speaker example,
   and `firmware/esp32s3/tools/crosscheck_usb_desc/run.sh` drives TinyUSB's real audio driver on the host with the firmware's callbacks,
   but that example, not this unit, is what its authors tested on Windows, macOS and Linux. Not checked on any host:
   how each treats the feedback endpoint (`bRefresh` 0 and `bmAttributes` 0x01, as in the example), and iPadOS at all.
   Symptoms: the device enumerates but audio is garbled, plays at the wrong speed, or the buffer fill walks away
   (`slips` count up in the `audio:` line; the software slip hides a host that ignores feedback). Where to look: the
   descriptor is `core/src/usb_desc.cpp`, the requests are `components/epdj_usb/src/usb_audio.cpp`. If a host refuses the
   feedback endpoint, the two descriptor fields to try are `bmAttributes` 0x11 (usage = feedback) and `bRefresh` 1..9
   (audio 1.0 asks for 1..9; TinyUSB's example uses 0 so the DWC2 controller serves every endpoint at 1 ms).
2. **Stream start.** TinyUSB queues the first feedback packet while it handles SET_INTERFACE, before any callback can run,
   so it carries 0 (the host driver check shows `00 00 00`). The firmware's set-interface callback then sets 48.0 and
   every later packet carries at least that, until the audio task has a measurement. Hosts ignore out-of-range
   feedback values; if one mis-sets its rate at the start of a stream, this is why.
3. **A zero-length packet on the audio endpoint.** TinyUSB's `audiod_rx_xfer_isr` fails its FIFO-write check on a
   0-byte packet *before* it re-arms the endpoint, so one ZLP from the host during streaming would stop the stream until
   the host re-selects the interface. The build compiles a patched copy of that driver file (one `if`, see
   `firmware/esp32s3/README.md`); the host check runs a ZLP through the patched driver and sees the endpoint re-armed and the
   next packet arrive (and shows the unpatched file failing). Whether any host sends one is unknown. If audio dies mid-play
   with `usb frames` frozen in the stats line anyway, look at the endpoint, not at this.
4. **ADC noise.** The S3's ADC is noisy; the fader is oversampled 8x and the engine filters it, but check that a
   still fader sends nothing (MIDI monitor: no CC 7/39 traffic at rest, CC 23/55 with the crossfader role).
5. **Touch counts and scan period.** Everything about PLATTER.md, plus the `touch:` boot lines (step 1): the scan period decides
   how stale a pad value is, the cycle count decides how coarse it is.
6. **Flash writes versus the USB interrupt.** The USB interrupt handler runs from flash. `SAVE_CONFIG` and `SET_DECK`
   erase and write flash with the cache off for milliseconds: expect an audio glitch at that moment.
7. **Bandwidth of the engine tick.** `loop: ... over 1 ms` in the stats line. The ADC cost is a measurement at boot, not an
   assumption (`adc:` line), and the loop drops lost ticks instead of running flat out (`resyncs`).
   The ADC conversions run **with interrupts off**, up to 400 us of every millisecond, on core 1, the core that also runs
   the TinyUSB task and the USB OTG interrupt (`main.cpp` header comment). They may delay USB and I2S service; nobody has
   measured it. With the fader moving, watch `over`/`under` and `late wake-ups` in the `audio:` and `loop:` lines; if they
   climb, lower the budget (`AdcInputs::kBudgetUs`) or move the read to core 0.
8. **The 3.3 V TRS MIDI stage** (WIRING.md): measure before connecting expensive gear.
9. **No VBUS sense.** The USB PHY is configured bus-powered with no sense pin, which makes TinyUSB believe VBUS is always
   present: it never reports a detach, and a pulled cable shows up only as the bus going idle (step 5). If that proves
   unreliable on the bench, the supported fix is a 5 V -> 3.3 V resistor divider from the USB connector's VBUS to a spare GPIO,
   `self_powered = true` and `vbus_monitor_io = <that GPIO>` in `usb_device.cpp`. That needs a wiring change, so it was not made
   blind.
10. **Touch signal at the short measurement.** Auto-ranging trades counts for scan speed (risk 5 and step 1). If the finger
    rise is lost in the noise, the alternative is a slower scan (raise `EPDJ_TOUCH_SCAN_TARGET_US`) or summing a few
    consecutive scans in the engine; neither is built.
