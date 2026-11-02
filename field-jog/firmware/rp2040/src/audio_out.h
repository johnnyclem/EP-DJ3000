#ifndef AUDIO_OUT_H_
#define AUDIO_OUT_H_

#include <stdint.h>

#define AUDIO_RATE 48000
#define AUDIO_FRAME 48

void audio_out_init(void);

// 65536 is 1x forward through the loop. Negative plays backwards. 0 is silence.
void audio_out_set_rate(int32_t rate_q16);

// 0 is mute, 32767 is unity. This is the fader, before the USB volume trim.
void audio_out_set_gain(uint16_t gain_q15);

// Host mute/volume. volume_db is the master channel, roughly -90..+30.
void audio_out_set_usb(bool mute, int16_t volume_db);

// One vinyl click, mixed over the next few milliseconds. Ignores the fader.
void audio_out_cue(void);

// Build one millisecond and hand it to USB and the UART DAC.
void audio_out_task(void);

#endif
