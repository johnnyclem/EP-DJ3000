#include "controls.h"

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

// AS5600 on I2C0. Halls and the knob on ADC0..2.
// Cue and rev are active-low, other side to ground.
#define PIN_SDA 4
#define PIN_SCL 5
#define PIN_CUE 6
#define PIN_REV 7
#define PIN_HALL_A 26
#define PIN_HALL_B 27
#define PIN_KNOB 28

// 12-bit Pico ADC. Trim these on the bench. Difference, low end to high end.
#define FADER_LO 320
#define FADER_HI 2800

// Counts of platter travel per millisecond that equal 1x playback.
#define COUNTS_PER_UNITY 8

static const uint8_t AS5600 = 0x36;
static int16_t last_angle = -1;
static bool last_cue = false;
static int8_t jog_sign = 1;

static int16_t read_angle(void) {
  uint8_t reg = 0x0C;
  uint8_t buf[2];
  if (i2c_write_timeout_us(i2c0, AS5600, &reg, 1, true, 1000) != 1) {
    return last_angle < 0 ? 0 : last_angle;
  }
  if (i2c_read_timeout_us(i2c0, AS5600, buf, 2, false, 1000) != 2) {
    return last_angle < 0 ? 0 : last_angle;
  }
  return (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

static uint16_t read_adc(int input) {
  adc_select_input(input);
  return adc_read();
}

void controls_init(void) {
  i2c_init(i2c0, 400000);
  gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
  gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(PIN_SDA);
  gpio_pull_up(PIN_SCL);

  gpio_init(PIN_CUE);
  gpio_init(PIN_REV);
  gpio_set_dir(PIN_CUE, GPIO_IN);
  gpio_set_dir(PIN_REV, GPIO_IN);
  gpio_pull_up(PIN_CUE);
  gpio_pull_up(PIN_REV);

  adc_init();
  adc_gpio_init(PIN_HALL_A);
  adc_gpio_init(PIN_HALL_B);
  adc_gpio_init(PIN_KNOB);

  last_angle = read_angle();
}

void controls_poll(deck_state_t *out) {
  int16_t ang = read_angle();
  int16_t d = (int16_t)(ang - last_angle);
  if (d > 2048) d = (int16_t)(d - 4096);
  if (d < -2048) d = (int16_t)(d + 4096);
  last_angle = ang;
  d = (int16_t)(d * jog_sign);

  bool rev = gpio_get(PIN_REV) == 0;
  if (rev) d = (int16_t)(-d);

  // Knob is a trim on the jog scale, from half speed to double.
  uint16_t knob = read_adc(2);
  int32_t scale = 32768 + (int32_t)knob * 2;
  out->rate_q16 = (int32_t)d * (65536 / COUNTS_PER_UNITY) * scale / 65536;

  int raw = (int)read_adc(0) - (int)read_adc(1);
  int span = FADER_HI - FADER_LO;
  int pos = raw - FADER_LO;
  if (pos < 0) pos = 0;
  if (pos > span) pos = span;
  out->gain_q15 = (uint16_t)((pos * 32767) / span);

  bool cue = gpio_get(PIN_CUE) == 0;
  out->cue_edge = cue && !last_cue;
  last_cue = cue;
}
