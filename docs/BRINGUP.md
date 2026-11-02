# EP-DJ3000 bring-up checklist

The order of [`ARCHITECTURE.md`](ARCHITECTURE.md) section 6, with what to expect in the log at each step. **Nothing in
the firmware has run on a chip yet.** It compiles and links for the ESP32-S3 (PlatformIO and `idf.py`, ESP-IDF 6.1.0 with
esp_tinyusb 3.0.0 = TinyUSB 0.21.0), and the portable logic is covered by host tests (the USB audio class driver too: see
the last risks), so this checklist is where reality gets to disagree. Write down every
deviation: the "Risks" at the end say which ones are most likely.

Hardware: [`hardware/WIRING.md`](../hardware/WIRING.md), [`hardware/PLATTER.md`](../hardware/PLATTER.md).
Firmware build and flash: [`firmware/esp32s3/README.md`](../firmware/esp32s3/README.md).

## 0. Before power

* [ ] Continuity: no short 3V3-GND; each of GPIO4..11 open to GND and to each other (sectors isolated); CUE/REV open
  until pressed.
* [ ] Fader and knob ends on 3V3 and GND, wipers to GPIO1 / GPIO2.
* [ ] Platter sectors: adjacent sectors read open on a multimeter.
* [ ] If a DAC is fitted: SCK to GND, XSMT high, FMT/FLT/DEMP low, strap GPIO14-GND **removed** for now (step 4 adds it).

## 1. Flash and watch the log

Connect the DevKit's **UART** port, flash, open the monitor at **921600** baud (`pio device monitor` is preset;
`idf.py monitor` follows the build's console baud rate).

Expected, in this order (values will differ):

```
main: EP-DJ3000 core 0.1.0, protocol v1, ESP-IDF v6.1.0
main: config: nothing saved; deck A, platter source 1
adc: fader GPIO1 raw 2031, knob GPIO2 raw 1180 (x8 oversampling)
touch: 8 pad(s) on GPIO4..11, idle peak count 21346, shift 0
main: no AS5600 on I2C                       (or: as5600: found at 0x36, status 0x20 (magnet detected))
main: dac: mode AUTO, strap low 0/16 samples, codec none -> absent (MIDI only)
usb: "EP-DJ3000 Deck A" serial 7CDFA1xxxxxx, config 173 bytes, VID:PID 1209:0001, audio off
radio: ESP-NOW on channel 1, station 7C:DF:A1:xx:xx:xx, broadcast peer
trs: UART1 31250 baud, TX GPIO40 RX GPIO41
main: link group 0 (build default)
main: ready: caps 0x005A, serial A1xxxxxx, MIDI channel 1
```

What to look for:

* [ ] `ready` appears and the LED turns on (deck A blue, dim while the platter is untouched).
* [ ] `touch: ... idle peak count` is in the thousands or more. The warning `idle counts are tiny: pads not
  connected` means open pads or too few charge/discharge cycles.
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
diag: sens: fader=2031 knob=1180 angle=0(none) cue=0 rev=0 touch=[21346 21410 20987 21102 21390 21255 21033 21198] fresh=98%
```

* [ ] Move the fader and knob end to end: both span most of 0..4095 and move smoothly. A fader that does not reach
  both ends is calibrated in step 2, not fixed in wiring.
* [ ] Press CUE, REV: `cue=1` / `rev=1`.
* [ ] Put a finger on each sector in turn: that sector's count rises, a neighbour's a little. Write down idle
  noise (peak-to-peak) and finger rise for each sector (PLATTER.md "Tuning").
* [ ] `fresh=` is 90-100 %. Far below means the touch scan is slower than the 1 ms engine tick: lower
  `EPDJ_TOUCH_CHARGE_DISCHARGE_TIMES` in menuconfig and rebuild.
* [ ] Every 10 s: `diag: loop: ... N over 1 ms (max M us), K late wake-ups`. A healthy loop has 0 overruns and a max
  well under 1000 us. If it does not: first lower `EPDJ_ADC_OVERSAMPLE` (16 ADC conversions per millisecond are the
  biggest single cost), then look at what shares core 1.

## 2. macOS app: sensors and calibration

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
  100 ms).
* [ ] A flash write happens on `SAVE_CONFIG`: expect a brief hiccup in the engine tick (a `late wake-up` in the
  stats line is normal at that moment).

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
* [ ] Unplug and replug while streaming: the device returns without a reboot; `fill` re-primes.

## 5. Second unit and the link

Flash the second unit with `EPDJ_FIRST_BOOT_DECK_B` (or hold CUE+REV 5 s on a unit to swap A/B), no USB host needed
on it beyond power.

* [ ] Both logs show `radio: ESP-NOW on channel 1` and, within about a second, in the 10 s stats:
  `link: up, peers 1, radio rx ... tx ...` on both. LED stops the slow "searching" blink.
* [ ] Both units claim the same deck: the LED alternates with red and `LINK_STATUS` reports the conflict (fix with
  `SET_DECK`).
* [ ] **One-cable mode**: unplug the data cable from deck B (power it from a charger). On deck A's log
  `link: up, bridging`; the DJ software sees deck B's controls on the "Linked deck" port (cable 2) on MIDI channel 2.
* [ ] Latency: round-trip from a jog on B to a MIDI event at the host should be a few ms (docs/LINK.md target
  <= 5 ms typical). Measure it; it is a prediction.
* [ ] SYNC: deck A in SYNC_MASTER, deck B SYNC_FOLLOW: B's beat flash follows A's.
* [ ] Check `radio ... tx ... (queue full 0, failed 0)` stays at 0 under motion.
* [ ] Rig ids: `SET_CONFIG linkGroup = 1` on deck B only. Within about 1.5 s both units show `searching` and the
  `other-group` counters in their `link:` lines climb; set 1 on deck A as well and they link again, no reboot needed
  (`SAVE_CONFIG` keeps it, and after a reboot the log says `link group 1 (linkGroup config key)`). A unit built with a non-zero
  `EPDJ_LINK_GROUP` cannot be put back on group 0 over SysEx.

## 6. DJ software

* [ ] Load the Mixxx mapping first (`drivers/mixxx`), then the others (`drivers/README.md`). Route deck 1 audio to
  Deck A's audio device, deck 2 to Deck B's, and each line out to a mixer channel.

## Risks, in the order I would check them

1. **USB audio on a real host (UAC1, asynchronous, explicit feedback).** The unit is USB Audio Class 1.0 because the
   ESP32-S3 is full-speed only and TinyUSB's UAC1 driver is the one that sends the feedback value the way a full-speed
   host expects (3 bytes, 10.14); its UAC2 driver sends 4 bytes 16.16 at any speed (ARCHITECTURE.md decision 5). The
   descriptor is byte-identical to TinyUSB's own `TUD_AUDIO10_*` macros in the layout of its full-speed speaker example,
   and `tools/crosscheck_usb_desc/run.sh` drives TinyUSB's real audio driver on the host with the firmware's callbacks,
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
   0-byte packet *before* it re-arms the endpoint. The host check runs exactly that against the real driver: it returns
   true and does not re-arm, so one ZLP from the host during streaming would stop the stream until the host re-selects
   the interface. Hosts normally send continuous full packets; if audio dies mid-play with `usb frames` frozen in the
   stats line, this is the suspect. It is upstream behaviour, not patched here.
4. **ADC noise.** The S3's ADC is noisy; the fader is oversampled 8x and the engine filters it, but check that a
   still fader sends nothing (MIDI monitor: no CC 7/39 traffic at rest, CC 23/55 with the crossfader role).
5. **Touch counts.** Everything about PLATTER.md.
6. **Flash writes versus the USB interrupt.** The USB interrupt handler runs from flash. `SAVE_CONFIG` and `SET_DECK`
   erase and write flash with the cache off for milliseconds: expect an audio glitch at that moment.
7. **Bandwidth of the engine tick.** `loop: ... over 1 ms` in the stats line.
8. **The 3.3 V TRS MIDI stage** (WIRING.md): measure before connecting expensive gear.
