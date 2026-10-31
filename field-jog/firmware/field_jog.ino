// FIELD JOG — USB MIDI for an Arduino Pro Micro (ATmega32U4).
// Untested on the bench. Map it in djay / Mixxx and trim the two fader numbers.
//
// Library: MIDIUSB (Arduino Library Manager)
//
//   AS5600  VCC -> VCC, GND -> GND, SDA -> 2, SCL -> 3
//   Hall A  (fader, low end)  -> A0
//   Hall B  (fader, high end) -> A1
//   Cue button  -> D4, other side to GND
//   Rev button  -> D5, other side to GND
//   Knob pot    -> A2 (wiper), ends to VCC and GND. Leave A2 empty if no pot.
//
// Platter magnet must be diametric (the kind AS5600 expects), glued to the
// M3 axle, chip-side toward the encoder, 1–3 mm away.
//
// Messages, channel 1:
//   CC 22  jog, relative, 64 = still, 65.. = forward, 63.. = back
//   CC 23  crossfader 0..127
//   CC 24  knob
//   note 36  CUE
//   note 37  REV

#include <Wire.h>
#include <MIDIUSB.h>

const uint8_t AS5600 = 0x36;
const int8_t JOG_SIGN = 1;     // flip to -1 if scratch direction is backwards
const int FADER_LO = 80;       // analog difference at the low end of travel, edit me
const int FADER_HI = 700;      // analog difference at the high end of travel, edit me

int16_t lastAngle = -1;
int lastFader = -1;
int lastKnob = -1;
bool lastCue = false;
bool lastRev = false;

int16_t readAngle() {
  Wire.beginTransmission(AS5600);
  Wire.write(0x0C);             // RAW ANGLE, 0..4095
  if (Wire.endTransmission() != 0) return lastAngle < 0 ? 0 : lastAngle;
  Wire.requestFrom(AS5600, (uint8_t)2);
  if (Wire.available() < 2) return lastAngle < 0 ? 0 : lastAngle;
  uint16_t hi = Wire.read();
  uint16_t lo = Wire.read();
  return (hi << 8) | lo;
}

void sendCC(uint8_t cc, uint8_t value) {
  midiEventPacket_t pkt = {0x0B, 0xB0, cc, value};
  MidiUSB.sendMIDI(pkt);
}

void sendNote(uint8_t note, bool on) {
  uint8_t head = on ? 0x09 : 0x08;
  uint8_t status = on ? 0x90 : 0x80;
  midiEventPacket_t pkt = {head, status, note, on ? 100 : 0};
  MidiUSB.sendMIDI(pkt);
}

void setup() {
  pinMode(4, INPUT_PULLUP);
  pinMode(5, INPUT_PULLUP);
  Wire.begin();
  Wire.setClock(400000);
  lastAngle = readAngle();
}

void loop() {
  int16_t ang = readAngle();
  int16_t d = ang - lastAngle;
  if (d > 2048) d -= 4096;
  if (d < -2048) d += 4096;
  lastAngle = ang;
  d = (int16_t)(d * JOG_SIGN);
  if (abs(d) > 1) {
    int vel = constrain(d / 6, -20, 20);
    if (vel != 0) sendCC(22, (uint8_t)constrain(64 + vel, 1, 127));
  }

  // Two halls at the ends of the slot. Difference is monotonic, not linear.
  long a = analogRead(A0);
  long b = analogRead(A1);
  int raw = (int)(a - b);
  int fader = constrain(map(raw, FADER_LO, FADER_HI, 0, 127), 0, 127);
  if (lastFader < 0 || abs(fader - lastFader) >= 1) {
    sendCC(23, (uint8_t)fader);
    lastFader = fader;
  }

  int knob = analogRead(A2) >> 3;   // 0..127
  if (lastKnob < 0 || abs(knob - lastKnob) >= 1) {
    sendCC(24, (uint8_t)knob);
    lastKnob = knob;
  }

  bool cue = digitalRead(4) == LOW;
  bool rev = digitalRead(5) == LOW;
  if (cue != lastCue) { sendNote(36, cue); lastCue = cue; }
  if (rev != lastRev) { sendNote(37, rev); lastRev = rev; }

  MidiUSB.flush();
  delay(2);
}
