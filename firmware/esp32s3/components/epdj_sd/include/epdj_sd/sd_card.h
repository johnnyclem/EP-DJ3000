// microSD library. Mounts a FAT card in SDMMC 1-bit mode, lists *.wav in the root, and plays the
// selected file through the DAC. See hardware/WIRING.md for the socket and the pins.
#pragma once

namespace epdj {
namespace hal {
class I2cBus;
}

namespace sd {

// True while CUE and REV belong to the browser. The engine task must not turn them into MIDI then.
bool buttonsCaptured();

// Mounts the card, draws the first track, and starts the browser task. Safe to call when the glass
// never came up or the DAC did not start: the card still mounts, and playback waits for the DAC.
void start(hal::I2cBus& oledBus);

}  // namespace sd
}  // namespace epdj
