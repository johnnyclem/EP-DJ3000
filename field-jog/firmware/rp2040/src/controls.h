#ifndef CONTROLS_H_
#define CONTROLS_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  int32_t rate_q16;
  uint16_t gain_q15;
  bool cue_edge;
} deck_state_t;

void controls_init(void);
void controls_poll(deck_state_t *out);

#endif
