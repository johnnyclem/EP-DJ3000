// End-to-end: the REAL firmware Engine (epdj_engine_sim) driven by simulated sensors, feeding the REAL
// drivers/mixxx mapping running against a mock Mixxx engine. See docs/E2E.md for what this does and does not prove.
//
//   node drivers/test/e2e/run.mjs [name-filter]      (tools/e2e.sh builds the simulator first)
//
// Exit 0 when everything passed or when the simulator is not built (reported as SKIP), 1 on any failure.

import assert from "node:assert/strict";

import { loadScript, makeEngine, P, Rig } from "./mixxx-host.mjs";
import { CONFIG_KEYS, parseSysex, setConfig, Sim, simBinary, sysexFrame, wireRuleViolations } from "./sim.mjs";

if (!simBinary()) {
  console.log("SKIP  epdj_engine_sim is not built (tools/e2e.sh builds it); Node end-to-end tests not run");
  process.exit(0);
}

const SETTLE_MS = 3600; // the touch detector (the hold pad's, or the ring's) learns its baseline for 250 ms and settles for 3 s
const K = CONFIG_KEYS;
const observations = [];
const observe = (msg) => observations.push(msg);

// ---- helpers ---------------------------------------------------------------------------------------------------

/**
 * A running simulator with one script instance per (unit, cable) listed in `ports`, all sharing one Mixxx engine.
 * Without `platter=` in `init` the unit is the default build: a free-spinning platter on an AS5600 and ONE hold pad.
 * The experimental touch ring is `init: "units=1 platter=ring"`.
 */
async function world({ init = "units=1", ports = [[0, 0]], settle = true, slice = 2 } = {}) {
  const sim = await Sim.start(init);
  const engine = makeEngine();
  const rig = new Rig(sim, engine, { slice });
  rig.platter = /\bplatter=ring\b/.test(init) ? "ring" : "as5600";
  const scripts = ports.map(([unit, cable]) => {
    const s = rig.attach(loadScript(engine), unit, cable);
    s.init();
    return s;
  });
  if (settle) await rig.run(SETTLE_MS);
  engine.take();
  return { sim, engine, rig, scripts, script: scripts[0] };
}

async function withWorld(opts, fn) {
  const w = await world(opts);
  try {
    const result = await fn(w);
    assert.deepEqual(wireRuleViolations(w.sim.log), [], "the firmware broke a wire rule");
    return result;
  } finally {
    await w.sim.close();
  }
}

/**
 * A hand scratches: it lands, dwells, moves the platter `revs` (signed: negative = counter-clockwise) over `ms`, dwells,
 * lifts. On the default hardware it lands on the hold pad and the AS5600 turns from wherever it is (`startRev` means
 * nothing there); on the touch ring the finger lands at `startRev` and slides.
 */
async function gesture(rig, { startRev = 0.25, revs = 2, ms = 4000, dwell = 100, tail = 150, after = 300, unit = 0 } = {}) {
  if (rig.platter === "ring") {
    await rig.cmd(`finger u=${unit} rev=${startRev} pressure=1`);
    await rig.run(dwell);
    await rig.ramp("finger", startRev, startRev + revs, ms, { unit });
  } else {
    await rig.cmd(`finger u=${unit} pressure=1`);
    await rig.run(dwell);
    await rig.spin(revs, ms, { unit });
  }
  await rig.run(tail);
  await rig.cmd(`finger u=${unit} pressure=0`);
  await rig.run(after);
}

const relative = (v) => v - P.relativeCenter;
const EPDJ_BEND_SCALE = 1.0; // EPDJ3000Mixxx.BEND_SCALE: [ChannelN],jog = ticks * BEND_SCALE (UNTUNED, drivers/mixxx)
const sum = (xs) => xs.reduce((a, b) => a + b, 0);

/** Sum of the jog CC (relative) values a unit sent on a cable and channel, optionally within [from, to). */
function jogSum(rig, { unit = 0, cable = 0, ch = 0, from = 0, to = Infinity } = {}) {
  return sum(rig.messages(unit, cable)
    .filter((m) => m.bytes[0] === (0xb0 | ch) && m.bytes[1] === P.cc.jog && m.t >= from && m.t < to)
    .map((m) => relative(m.bytes[2])));
}

/** 14-bit values of a CC pair in the order the unit sent them: [{t, value, order}] one entry per message. */
function pairTrace(rig, { unit = 0, cable = 0, ch = 0, msb, lsb, from = 0 }) {
  const out = [];
  let m = null, l = null;
  for (const e of rig.messages(unit, cable)) {
    if (e.t < from) continue;
    if (e.bytes[0] !== (0xb0 | ch)) continue;
    if (e.bytes[1] === msb) m = e.bytes[2];
    else if (e.bytes[1] === lsb) l = e.bytes[2];
    else continue;
    if (m !== null && l !== null) out.push({ t: e.t, value: (m << 7) | l, half: e.bytes[1] === msb ? "msb" : "lsb" });
  }
  return out;
}

/** PROTOCOL.md 2.4 for the default calibration/dead zones, linear curve, not inverted. */
function expectedFader14(raw, { rawMin = 40, rawMax = 4055, dead = 0.015 } = {}) {
  const p = Math.min(1, Math.max(0, (raw - rawMin) / (rawMax - rawMin)));
  const live = p <= dead ? 0 : p >= 1 - dead ? 1 : (p - dead) / (1 - 2 * dead);
  return Math.round(live * 16383);
}

/**
 * What the script does with each slide-pot role. Contract v1.2: a factory unit's pot is the tempo fader, PITCH (CC 25/57),
 * which the script sets as the deck's `rate` (Mixxx range -1..1). CHANNEL_VOLUME (CC 7/39) is the deck's `volume` (0..1).
 */
const POT = {
  pitch: { role: 2, msb: P.cc.pitchMsb, lsb: P.cc.pitchLsb, key: "rate", mixxx: (v14) => -1 + (2 * v14) / 16383 },
  volume: { role: 1, msb: P.cc.channelVolumeMsb, lsb: P.cc.channelVolumeLsb, key: "volume", mixxx: (v14) => v14 / 16383 },
};
const nearly = (a, b) => Math.abs(a - b) < 1e-9;

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

// ---- platter ---------------------------------------------------------------------------------------------------

// The default build, then the experimental ring (named in the test title, selected with `init platter=ring`).
for (const platter of ["as5600", "ring"]) {
  const ring = platter === "ring";
  const tag = ring ? "touch ring (experimental)" : "AS5600";
  for (const [deckId, dir] of [[0, 1], [0, -1], [1, 1], [1, -1]]) {
    test(`${tag} scratch sweep, Deck ${"AB"[deckId]}, ${dir > 0 ? "clockwise" : "counter-clockwise"}: every jog tick reaches scratchTick, none leak into pitch bend`, async () => {
      await withWorld({ init: `units=1 deck0=${deckId}${ring ? " platter=ring" : ""}` }, async ({ sim, engine, rig }) => {
        await gesture(rig, { revs: 2 * dir });
        const calls = engine.take();
        const deck = deckId + 1;

        const enables = calls.filter((c) => c.fn === "scratchEnable");
        assert.equal(enables.length, 1, "one touch, one scratchEnable");
        assert.equal(enables[0].deck, deck);
        assert.equal(enables[0].intervalsPerRev, P.jogTicksPerRev);
        assert.equal(calls.filter((c) => c.fn === "scratchDisable").length, 1);
        assert.equal(engine.isScratching(deck), false, "released");

        const ticks = calls.filter((c) => c.fn === "scratchTick");
        assert.ok(ticks.every((c) => c.deck === deck && c.whileScratching), "ticks only while scratching, on the right deck");
        const scratched = sum(ticks.map((c) => c.interval));
        const wire = jogSum(rig, { ch: deckId });
        const total = (await sim.status(0)).jogTotal;
        assert.equal(wire, total, "the wire carried every tick the firmware counted (jogTotal)");
        assert.equal(scratched, wire, `scratchTick total ${scratched} must equal the wire total ${wire}`);
        assert.equal(calls.filter((c) => c.fn === "setValue" && c.k === "jog").length, 0, "no tick leaked into pitch bend");

        assert.ok(scratched * dir > 0, "clockwise is positive");
        if (ring) {
          const err = Math.abs(Math.abs(scratched) - 2 * P.jogTicksPerRev) / (2 * P.jogTicksPerRev);
          observe(`ring: 2-revolution ${dir > 0 ? "CW" : "CCW"} sweep on Deck ${"AB"[deckId]} delivered ${scratched} ticks (${(err * 100).toFixed(2)}% short of 2048)`);
          assert.ok(err < 0.04, `expected ~2048 ticks, got ${scratched}`);
        } else {
          assert.equal(scratched, 2 * P.jogTicksPerRev * dir, "two revolutions of the encoder are exactly 2048 ticks");
        }
      });
    });
  }
}

test("AS5600: a full one-revolution spin of the untouched platter reaches the Mixxx script as exactly 1024 jog ticks of pitch bend", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    // Mixxx reads [ChannelN],jog once per audio callback and zeroes it (RateControl::getJogFactor); do the same every 2 ms.
    let bent = 0;
    const consume = () => { bent += engine.getValue("[Channel1]", "jog"); engine.mixxxSet("[Channel1]", "jog", 0); };
    const startRev = rig.encoderRev.get(0) ?? 0;
    for (let i = 0; i < 500; i++) {
      await rig.ramp("as5600", startRev + i / 500, startRev + (i + 1) / 500, 2); // one revolution in 1 s: 2 ticks per 2 ms
      consume();
    }
    await rig.run(100);
    consume();
    const calls = engine.take();
    assert.equal(calls.filter((c) => c.fn === "scratchEnable" || c.fn === "scratchTick").length, 0, "nobody touched the platter: not scratching");
    assert.equal(bent, P.jogTicksPerRev * EPDJ_BEND_SCALE, "1024 ticks, none lost to clamping or to the 2 ms message pacing");
    assert.equal(jogSum(rig), P.jogTicksPerRev, "and that is exactly what the wire carried");
    assert.equal((await sim.status(0)).jogTotal, P.jogTicksPerRev, "and what the firmware counted");
    assert.equal(rig.messages().filter((m) => m.bytes[1] === P.note.touch).length, 0, "no touch note without a finger on the hold pad");
  });
});

test("AS5600: touching the hold pad switches Mixxx into scratch mode (scratchEnable, ticks, scratchDisable) even when the platter does not move", async () => {
  await withWorld({}, async ({ engine, rig }) => {
    await rig.cmd("finger pressure=1");
    await rig.run(120);
    assert.equal(engine.isScratching(1), true, "a finger on the pad is scratch mode");
    await rig.run(1000);
    assert.equal(engine.isScratching(1), true, "a still finger holds scratch mode");
    await rig.cmd("finger pressure=0");
    await rig.run(200);
    assert.equal(engine.isScratching(1), false);
    const calls = engine.take();
    assert.deepEqual(calls.map((c) => c.fn), ["scratchEnable", "scratchDisable"], "nothing else: the encoder is at rest");
    assert.equal(calls[0].intervalsPerRev, P.jogTicksPerRev);
  });
});

test("AS5600: the hand lets go of a spinning platter: ticks go to scratchTick while touched, the coasting ones become pitch bend, none are lost", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    await rig.cmd("finger pressure=1");
    await rig.run(100);
    await rig.spin(0.5, 500); // pushed by hand: 512 ticks while touched
    await rig.cmd("finger pressure=0");
    const mark = rig.heard.length;
    await rig.spin(0.5, 500); // it coasts on at the same speed: 512 more, untouched
    await rig.run(300);
    const calls = engine.take();
    const disable = calls.findIndex((c) => c.fn === "scratchDisable");
    assert.ok(disable > 0, "released");
    const scratched = sum(calls.filter((c) => c.fn === "scratchTick").map((c) => c.interval));
    const bendCalls = calls.filter((c) => c.fn === "setValue" && c.k === "jog");
    assert.ok(calls.slice(disable).every((c) => c.fn !== "scratchTick"), "no scratchTick after scratchDisable");
    assert.ok(bendCalls.length > 100, "the coasting platter moves the pitch bend");
    assert.equal(jogSum(rig), (await sim.status(0)).jogTotal, "the wire carried everything the firmware counted");
    assert.equal((await sim.status(0)).jogTotal, 1024);
    assert.ok(scratched > 500 && scratched <= 1024, `the ticks made while the hand was on it went to scratchTick (${scratched})`);
    assert.ok(rig.heard.slice(mark).some((e) => e.bytes[0] === 0xb0 && e.bytes[1] === P.cc.jog), "jog messages after the release");
  });
});

test("AS5600: a unit whose encoder never answers is silent on jog (scratch mode and the pitch bend both see nothing), but the hold pad still scratches", async () => {
  await withWorld({ init: "units=1 as5600=0" }, async ({ sim, engine, rig }) => {
    await gesture(rig, { revs: 2 }); // somebody touches the pad and turns the (absent) platter
    const calls = engine.take();
    assert.deepEqual(calls.map((c) => c.fn), ["scratchEnable", "scratchDisable"], "the pad works, nothing else comes");
    assert.equal(rig.messages().filter((m) => m.bytes[1] === P.cc.jog).length, 0, "not a single jog message");
    assert.equal((await sim.status(0)).jogTotal, 0);
  });
});

test("AS5600: reads that fail while the platter keeps turning produce no ticks, and the first good read afterwards is not a jump", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    await rig.run(100);
    await rig.setEncoder(0, { valid: false }); // the chip is there but every I2C read fails (a loose wire)
    await rig.spin(0.37, 400);                 // the platter turns on regardless
    await rig.run(100);
    assert.equal((await sim.status(0)).jogTotal, 0, "not one tick while the reads fail");
    assert.equal(rig.messages().filter((m) => m.bytes[1] === P.cc.jog).length, 0);
    await rig.setEncoder(rig.encoderRev.get(0), { valid: true }); // the wire is fixed; the platter is 0.37 rev from where it was
    await rig.run(100);
    assert.equal((await sim.status(0)).jogTotal, 0, "the first good read re-anchors: the displacement made while blind is not a jump");
    await rig.spin(0.25, 500);
    await rig.run(100);
    assert.equal((await sim.status(0)).jogTotal, 256, "and motion counts exactly again");
    assert.equal(engine.take("scratchTick").length, 0);
  });
});

test("a fast flick released immediately on the TOUCH RING: nothing is scratched after scratchDisable, and nothing arrives after the touch-off note", async () => {
  await withWorld({ init: "units=1 platter=ring" }, async ({ engine, rig }) => {
    for (const revPerSec of [4, 12]) {
      await rig.cmd("finger rev=0.1 pressure=1");
      await rig.run(60);
      await rig.ramp("finger", 0.1, 0.1 + revPerSec * 0.25, 250);
      await rig.cmd("finger pressure=0");
      await rig.run(400);
    }
    const calls = engine.take();
    const lastDisable = calls.map((c) => c.fn).lastIndexOf("scratchDisable");
    const late = calls.slice(lastDisable).filter((c) => c.fn === "scratchTick" || (c.fn === "setValue" && c.k === "jog"));
    assert.deepEqual(late, [], "no jog after the platter was released");
    const touchOff = rig.messages().filter((m) => m.bytes[1] === P.note.touch && (m.bytes[2] === 0 || (m.bytes[0] & 0xf0) === 0x80)).map((m) => m.t);
    const lastOff = touchOff[touchOff.length - 1];
    const jogAfter = rig.messages().filter((m) => m.bytes[1] === P.cc.jog && m.t > lastOff);
    assert.equal(jogAfter.length, 0, "jog CCs after the final touch-off");
  });
});

test("AS5600: a fast flick released immediately: nothing is scratched after scratchDisable (the rest of the motion is pitch bend, not lost)", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    for (const revPerSec of [4, 12]) {
      await rig.cmd("finger pressure=1");
      await rig.run(60);
      await rig.spin(revPerSec * 0.25, 250);
      await rig.cmd("finger pressure=0");
      await rig.run(400);
    }
    const calls = engine.take();
    const lastDisable = calls.map((c) => c.fn).lastIndexOf("scratchDisable");
    assert.ok(lastDisable >= 0);
    assert.deepEqual(calls.slice(lastDisable).filter((c) => c.fn === "scratchTick"), [], "no scratchTick after the platter was released");
    assert.equal(engine.isScratching(1), false);
    // Unlike the ring, the encoder keeps reporting motion after the hand is gone, so the end of the flick arrives as pitch bend.
    assert.equal(jogSum(rig), (await sim.status(0)).jogTotal, "and the wire carried all of it");
  });
});

test("touch with no movement: scratch mode on and off, zero ticks, no pitch bend", async () => {
  await withWorld({}, async ({ engine, rig }) => {
    await rig.cmd("finger rev=0.4 pressure=1");
    await rig.run(120);
    assert.equal(engine.isScratching(1), true);
    await rig.run(1000);
    assert.equal(engine.isScratching(1), true, "a still finger holds scratch mode");
    await rig.cmd("finger pressure=0");
    await rig.run(200);
    assert.equal(engine.isScratching(1), false);
    const calls = engine.take();
    const ticks = calls.filter((c) => c.fn === "scratchTick").map((c) => c.interval);
    observe(`a resting finger on the hold pad for 1.1 s produced ${ticks.length} jog message(s), net ${sum(ticks)} tick(s)`);
    assert.equal(ticks.length, 0, `a resting finger on the hold pad moved the record: ${ticks}`);
    assert.equal(calls.filter((c) => c.fn === "setValue" && c.k === "jog").length, 0);
  });
});

test("touch with no movement on the TOUCH RING: scratch mode on and off, no net ticks, no pitch bend", async () => {
  await withWorld({ init: "units=1 platter=ring" }, async ({ engine, rig }) => {
    await rig.cmd("finger rev=0.4 pressure=1");
    await rig.run(120);
    assert.equal(engine.isScratching(1), true);
    await rig.run(1000);
    assert.equal(engine.isScratching(1), true, "a still finger holds scratch mode");
    await rig.cmd("finger pressure=0");
    await rig.run(200);
    assert.equal(engine.isScratching(1), false);
    const calls = engine.take();
    const ticks = calls.filter((c) => c.fn === "scratchTick").map((c) => c.interval);
    observe(`ring: a resting finger for 1.1 s (+-3 per mille ring noise) produced ${ticks.length} jog message(s), net ${sum(ticks)} tick(s)`);
    assert.ok(ticks.length <= 4 && Math.abs(sum(ticks)) <= 4, `a resting finger moved the record: ${ticks}`);
    assert.equal(calls.filter((c) => c.fn === "setValue" && c.k === "jog").length, 0);
  });
});

test("jogInvert set over SysEx flips the sign the script sees (the software twin of the AS5600's DIR pin)", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    await setConfig(sim, 0, 0, K.jogInvert, 1);
    await gesture(rig, { revs: 1, ms: 2000 });
    const scratched = sum(engine.take("scratchTick").map((c) => c.interval));
    assert.equal(scratched, -P.jogTicksPerRev, `clockwise with jogInvert=1 is exactly -1024, got ${scratched}`);
  });
});

test("AS5600 is the factory platterSource, and untouched motion becomes pitch bend while touched motion scratches", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    assert.equal((await sim.status(0)).cfg[K.platterSource], 0, "nothing was configured: platterSource is AS5600");
    await rig.run(100);
    engine.take();
    // untouched: 0.25 rev clockwise in 500 ms = 256 ticks at 0.5 rev/s, nobody on the hold pad
    await rig.spin(0.25, 500);
    await rig.run(100);
    const bendCalls = engine.take();
    assert.equal(bendCalls.filter((c) => c.fn === "scratchTick").length, 0);
    const bend = bendCalls.filter((c) => c.fn === "setValue" && c.k === "jog");
    assert.ok(bend.length > 0, "untouched platter motion must reach [ChannelN],jog");
    assert.ok(bend.every((c) => c.v >= -3 && c.v <= 3));
    const wire = jogSum(rig);
    assert.equal(wire, 256, `a quarter turn is exactly 256 ticks, wire carried ${wire}`);
    // touched: the hold pad says touched, the AS5600 supplies the angle
    await rig.cmd("finger pressure=1");
    await rig.run(100);
    assert.equal(engine.isScratching(1), true);
    const before = jogSum(rig);
    await rig.spin(0.5, 1000);
    await rig.run(100);
    const calls = engine.take();
    const scratched = sum(calls.filter((c) => c.fn === "scratchTick").map((c) => c.interval));
    assert.equal(scratched, jogSum(rig) - before, "ticks while touched all went to scratchTick");
    assert.equal(scratched, 512, "half a turn is exactly 512 ticks");
  });
});

// ---- fader and knob --------------------------------------------------------------------------------------------

/** Sweeps the pot over its whole travel and checks what the script and the wire saw. `role` is set over SysEx first unless it is the default. */
function faderSweepTest(name, pot, { setRole = false } = {}) {
  test(name, async () => {
    await withWorld({}, async ({ sim, engine, rig }) => {
      if (setRole) {
        await setConfig(sim, 0, 0, K.faderRole, pot.role);
        await rig.run(50);
      }
      await rig.cmd("fader raw=40");
      await rig.run(200);
      engine.take();
      const t0 = rig.heard.length;
      const tStart = rig.heard.length ? rig.heard[rig.heard.length - 1].t : 0;
      await rig.ramp("fader", 40, 4055, 800);
      await rig.run(200);
      const up = engine.take("setParameter");
      assert.ok(up.length > 20, "a sweep produces many updates");
      assert.ok(up.every((c) => c.g === "[Channel1]" && c.k === pot.key), `the role is deck ${pot.key}, not the crossfader`);
      assert.equal(engine.getValue("[Channel1]", pot.key), pot.mixxx(16383), "top of travel is exactly the end of the control's range");

      // ordering as the firmware really sends it
      const msgs = rig.heard.slice(t0).filter((e) => e.bytes.length === 3 && e.bytes[0] === 0xb0 && (e.bytes[1] === pot.msb || e.bytes[1] === pot.lsb));
      let order = 0;
      for (let i = 0; i < msgs.length; i++) {
        const isLsb = msgs[i].bytes[1] === pot.lsb;
        if (isLsb) {
          const prev = msgs[i - 1];
          const sameStepMsb = prev && prev.t === msgs[i].t && prev.bytes[1] === pot.msb;
          if (!sameStepMsb) order++; // an LSB with no MSB in the same step is the "skipped unchanged half" case
        } else {
          const next = msgs[i + 1];
          assert.ok(!next || next.t > msgs[i].t || next.bytes[1] === pot.lsb, "MSB is followed by its LSB, never by another MSB in the same step");
        }
      }
      observe(`fader sweep (${pot.key}): ${msgs.length} CC messages, ${order} lone-LSB updates (unchanged MSB skipped)`);

      // transient overshoot of the MSB-then-LSB pair as a receiver sees it
      const trace = pairTrace(rig, { msb: pot.msb, lsb: pot.lsb, from: tStart + 1 });
      const finals = trace.filter((p, i) => i === trace.length - 1 || trace[i + 1].t !== p.t);
      for (let i = 1; i < finals.length; i++) assert.ok(finals[i].value >= finals[i - 1].value - 1, "settled values never go backwards");
      let worst = 0;
      for (let i = 0; i < trace.length; i++) {
        const settled = finals.find((f) => f.t === trace[i].t).value;
        worst = Math.max(worst, Math.abs(trace[i].value - settled));
      }
      observe(`fader sweep (${pot.key}): worst 14-bit transient between the MSB and LSB of one update = ${worst} counts (${(worst / 163.83).toFixed(2)}% of travel)`);

      const final = trace[trace.length - 1].value;
      assert.ok(Math.abs(final - expectedFader14(4055)) <= 2, `top of travel ${final}`);
      assert.ok(nearly(engine.getValue("[Channel1]", pot.key), pot.mixxx(final)), "the Mixxx parameter is exactly the 14-bit wire value / 16383 on the control's range");

      // and back down, to a mid position
      await rig.ramp("fader", 4055, 2048, 500);
      await rig.run(200);
      const mid = pairTrace(rig, { msb: pot.msb, lsb: pot.lsb }).pop().value;
      assert.ok(Math.abs(mid - expectedFader14(2048)) <= 45, `mid travel: wire ${mid}, spec ${expectedFader14(2048)}`);
      assert.ok(nearly(engine.getValue("[Channel1]", pot.key), pot.mixxx(mid)));
      await rig.cmd("fader raw=0");
      await rig.run(200);
      assert.ok(nearly(engine.getValue("[Channel1]", pot.key), pot.mixxx(0)), "bottom of travel is exactly the other end of the range");
    });
  });
}
faderSweepTest("fader sweep (default faderRole = PITCH): full range of the deck's rate, monotonic, MSB always before LSB, final value matches PROTOCOL 2.4", POT.pitch);
faderSweepTest("fader sweep (faderRole = CHANNEL_VOLUME over SysEx): full 0..1 range of the deck's volume, monotonic, MSB always before LSB", POT.volume, { setRole: true });

test("a real fader movement is on the wire within 2 ms (PROTOCOL.md 2.4), MSB before LSB, and a one-count wiggle is not", async () => {
  await withWorld({}, async ({ sim, rig }) => {
    await rig.cmd("fader raw=1000 noise=0");
    await rig.run(300);
    const tChange = (await sim.status(0)).t;
    await rig.cmd("fader raw=3000 noise=0");
    await rig.run(10);
    const burst = rig.messages().filter((m) => m.t >= tChange && (m.bytes[1] === POT.pitch.msb || m.bytes[1] === POT.pitch.lsb));
    assert.ok(burst.length >= 2, "the step produced a pair");
    const latencyUs = burst[0].t - tChange;
    observe(`fader step of 2000 counts: first CC ${latencyUs} us after the ADC changed`);
    assert.ok(latencyUs <= 2000, `latency ${latencyUs} us`);
    assert.equal(burst[0].bytes[1], POT.pitch.msb, "MSB first");
    assert.equal(burst[1].bytes[1], POT.pitch.lsb, "then LSB");
    assert.equal(burst[0].t, burst[1].t, "in the same engine tick");
  });
});

test("a still fader does not chatter: +-3 counts of ADC noise sends nothing for 5 s", async () => {
  await withWorld({}, async ({ rig }) => {
    await rig.cmd("fader raw=1500 noise=3");
    await rig.cmd("knob raw=3000 noise=3");
    await rig.run(500);
    const n = rig.heard.length;
    await rig.run(5000);
    const extra = rig.heard.slice(n).filter((e) => e.bytes[0] === 0xb0 && e.bytes[1] !== P.cc.jog);
    assert.equal(extra.length, 0, `${extra.length} fader/knob messages from a still pot`);
  });
});

test("faderRole over SysEx selects which CC pair the script sees: PITCH (the default) -> deck rate, CROSSFADER -> [Master],crossfader, CHANNEL_VOLUME -> deck volume", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    await rig.cmd("fader raw=4055");
    await rig.run(100);
    assert.equal(engine.getValue("[Channel1]", "rate"), 1, "the default role is the tempo fader: pot at top = rate slider at +1");
    await setConfig(sim, 0, 0, K.faderRole, 0);
    await rig.run(50);
    assert.equal(engine.getValue("[Master]", "crossfader"), 1, "CROSSFADER role: pot at top = crossfader fully right");
    await rig.cmd("fader raw=40");
    await rig.run(100);
    assert.equal(engine.getValue("[Master]", "crossfader"), -1);

    await setConfig(sim, 0, 0, K.faderRole, 2);
    await rig.run(50);
    assert.equal(engine.getValue("[Channel1]", "rate"), -1, "PITCH role: pot at bottom = rate slider at -1");
    await rig.cmd("fader raw=4055");
    await rig.run(100);
    assert.equal(engine.getValue("[Channel1]", "rate"), 1);

    await setConfig(sim, 0, 0, K.faderRole, 1);
    await rig.run(50);
    assert.equal(engine.getValue("[Channel1]", "volume"), 1, "CHANNEL_VOLUME: pot at top = volume 1");

    await setConfig(sim, 0, 0, K.faderRole, 2);
    await rig.run(50);
    assert.equal(engine.getValue("[Channel1]", "rate"), 1, "and back to the default PITCH");
  });
});

test("knob sweep: Quick Effect parameter spans 0..1 and ends exactly on the wire value", async () => {
  await withWorld({}, async ({ engine, rig }) => {
    await rig.cmd("knob raw=0");
    await rig.run(100);
    engine.take();
    await rig.ramp("knob", 0, 4095, 700);
    await rig.run(150);
    const calls = engine.take("setParameter");
    assert.ok(calls.length > 10 && calls.every((c) => c.g === "[QuickEffectRack1_[Channel1]]" && c.k === "super1"));
    const last = pairTrace(rig, { msb: P.cc.knobMsb, lsb: P.cc.knobLsb }).pop().value;
    assert.ok(last >= 16383 - 16, `knob at the top of travel sent ${last}`);
    assert.equal(engine.getValue("[QuickEffectRack1_[Channel1]]", "super1"), last / 16383);
  });
});

test("knobRole = BPM: knob turns send bpm x10 (CC 20/52), the script sets the deck tempo, and the value matches the unit's tempo", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    engine.mixxxSet("[Channel1]", "file_bpm", 100);
    engine.mixxxSet("[Channel1]", "bpm", 100);
    await setConfig(sim, 0, 0, K.knobRole, 1);
    await rig.cmd("knob raw=2048");
    await rig.run(200);
    engine.take();
    await rig.ramp("knob", 2048, 3500, 400);
    await rig.run(200);
    const st = await sim.status(0);
    const bpm = engine.getValue("[Channel1]", "bpm");
    assert.equal(Math.round(bpm * 10), st.tempoX10, `Mixxx bpm ${bpm} vs unit tempo ${st.tempoX10 / 10}`);
    const x10 = 600 + Math.round((expectedKnob14(3500) * 1200) / 16383);
    assert.ok(Math.abs(st.tempoX10 - x10) <= 5, `tempo ${st.tempoX10 / 10} BPM should be 60 + 120 x position (${x10 / 10})`);
  });
});

function expectedKnob14(raw) {
  return Math.round(Math.min(1, Math.max(0, raw / 4095)) * 16383);
}

// ---- buttons, tap, LEDs ----------------------------------------------------------------------------------------

test("CUE and REV buttons: debounced press/release reach Mixxx as cue_default and reverseroll; REV_BEHAVIOR toggle flips reverse", async () => {
  await withWorld({}, async ({ engine, rig, script }) => {
    await rig.cmd("buttons cue=1");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "cue_default"), 1);
    await rig.cmd("buttons cue=0");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "cue_default"), 0);

    await rig.cmd("buttons rev=1");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "reverseroll"), 1);
    await rig.cmd("buttons rev=0");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "reverseroll"), 0);

    script.obj.REV_BEHAVIOR = "toggle";
    await rig.cmd("buttons rev=1");
    await rig.run(60);
    await rig.cmd("buttons rev=0");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "reverse"), 1);
    await rig.cmd("buttons rev=1");
    await rig.run(60);
    await rig.cmd("buttons rev=0");
    await rig.run(60);
    assert.equal(engine.getValue("[Channel1]", "reverse"), 0);
  });
});

test("platter tap closes the loop: unit pulses play, Mixxx toggles play, the LED note comes back, the unit's LED shows playing", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    const tap = async () => {
      await rig.cmd("finger rev=0.3 pressure=1");
      await rig.run(80);
      await rig.cmd("finger pressure=0");
      await rig.run(120);
    };
    await tap();
    assert.equal(engine.getValue("[Channel1]", "play"), 1, "first tap starts the deck");
    assert.equal((await sim.status(0)).led.transport, P.enums.TransportState.playing, "unit LED follows Mixxx play via note 39 feedback");
    await tap();
    assert.equal(engine.getValue("[Channel1]", "play"), 0, "second tap pauses");
    assert.notEqual((await sim.status(0)).led.transport, P.enums.TransportState.playing);
    assert.equal(engine.isScratching(1), false, "a tap is not left as a scratch");
    const ticks = engine.take("scratchTick");
    assert.ok(Math.abs(sum(ticks.map((c) => c.interval))) <= 8, "a tap moved the platter by no more than tapMoveTicks");
  });
});

test("LED feedback bytes from the script are accepted by the engine: cue_indicator and play light the unit, and each unit ignores the other deck's channel", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    const settle = async () => { await rig.flush(); await rig.run(4); }; // LedStatus is recomputed by the next step
    engine.mixxxSet("[Channel1]", "cue_indicator", 1);
    await settle();
    assert.equal((await sim.status(0)).led.cueLit, true, "cue LED on");
    engine.mixxxSet("[Channel1]", "cue_indicator", 0);
    await settle();
    assert.equal((await sim.status(0)).led.cueLit, false, "cue LED off (Note On velocity 0)");

    engine.mixxxSet("[Channel2]", "cue_indicator", 1); // Deck B's LED, sent on Deck A's cable as well
    engine.mixxxSet("[Channel2]", "play", 1);
    await settle();
    const st = await sim.status(0);
    assert.equal(st.led.cueLit, false, "Deck A's LED must not follow Deck B's cue");
    assert.notEqual(st.led.transport, P.enums.TransportState.playing, "Deck A's LED must not follow Deck B's play");

    engine.mixxxSet("[Channel1]", "play", 1);
    await settle();
    assert.equal((await sim.status(0)).led.transport, P.enums.TransportState.playing);
    engine.mixxxSet("[Channel1]", "play", 0);
    await settle();
    assert.notEqual((await sim.status(0)).led.transport, P.enums.TransportState.playing);
  });
});

test("script init sends all-off LED feedback and the engine takes it: no LED lit at start, jog and faders unaffected", async () => {
  await withWorld({ settle: false }, async ({ sim, rig }) => {
    await rig.run(10);
    const st = await sim.status(0);
    assert.equal(st.led.cueLit, false);
    assert.equal(st.mode, P.enums.Mode.dj);
  });
});

// ---- SYNC_MASTER -----------------------------------------------------------------------------------------------

test("SYNC_MASTER: CUE taps tempo (note 41 -> tempo_tap), platter tap plays/pauses the unit's own clock and Mixxx follows through note 39", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    await setConfig(sim, 0, 0, K.mode, 1);
    await rig.run(100);
    engine.take();
    for (let i = 0; i < 3; i++) {
      await rig.cmd("buttons cue=1");
      await rig.run(20);
      await rig.cmd("buttons cue=0");
      await rig.run(480);
    }
    const taps = engine.take("setValue").filter((c) => c.k === "tempo_tap" && c.v === 1);
    assert.equal(taps.length, 3, "each CUE press is a tap-tempo press for Mixxx");
    const clocks = rig.heard.filter((e) => e.bytes.length === 1 && e.bytes[0] === 0xf8).length;
    assert.ok(clocks > 60, `the unit is sending MIDI clock (${clocks} ticks in 1.5 s at ~120 BPM)`);

    const heardBefore = rig.heard.length;
    await rig.cmd("finger rev=0.3 pressure=1");
    await rig.run(80);
    await rig.cmd("finger pressure=0");
    await rig.run(150);
    const sys = rig.heard.slice(heardBefore).filter((e) => e.bytes.length === 1 && e.bytes[0] >= 0xfa).map((e) => e.bytes[0]);
    assert.deepEqual(sys, [0xfa], "tap starts the unit's clock (MIDI Start)");
    assert.equal(engine.getValue("[Channel1]", "play"), 1, "and the play pulse starts Mixxx");
    assert.equal((await sim.status(0)).transport, P.enums.TransportState.playing);
  });
});

// ---- two units, one cable: the bridge ---------------------------------------------------------------------------

test("one-cable bridge (Deck B has no USB host): its scratch arrives on Deck A's cable 2 as channel 2, exact tick conservation under 15% radio loss", async () => {
  await withWorld({ init: "units=2 deck0=0 deck1=1 usb1=0 loss=0.15 dup=0.05", ports: [[0, 0], [0, 2]] }, async ({ sim, engine, rig }) => {
    await rig.run(1000); // link discovery
    assert.equal((await sim.status(0)).bridging, true, "link up and bridging");
    engine.take();
    await gesture(rig, { unit: 1, revs: 1, ms: 2000, dwell: 100, tail: 150, after: 1500 }); // the last heartbeats repair a lost trailing packet
    const calls = engine.take();
    const deckB = calls.filter((c) => c.fn === "scratchTick" && c.deck === 2);
    assert.ok(deckB.length > 0, "Deck B scratch ticks arrived");
    assert.equal(calls.filter((c) => c.fn === "scratchTick" && c.deck === 1).length, 0, "nothing on Deck A");
    const viaCable2 = jogSum(rig, { unit: 0, cable: 2, ch: 1 });
    const unitB = (await sim.status(1)).jogTotal;
    observe(`bridge under 15% loss + 5% duplication: Deck B counted ${unitB} ticks, Mixxx received ${sum(deckB.map((c) => c.interval))}`);
    assert.equal(sum(deckB.map((c) => c.interval)), viaCable2);
    assert.equal(viaCable2, unitB, "the cumulative jog counter makes the bridge lossless for motion");
    assert.equal(unitB, P.jogTicksPerRev, "one revolution of Deck B's encoder");

    // Deck B's fader as the bridge re-emits it on cable 2: last value wins, and Mixxx ends on the value the spec computes
    await rig.cmd("fader u=1 raw=500 noise=0");
    await rig.run(100);
    const mark = rig.heard.length;
    await rig.ramp("fader", 500, 3900, 1200, { unit: 1 });
    await rig.run(300);
    const pair = rig.heard.slice(mark).filter((e) => e.u === 0 && e.cable === 2 && e.bytes.length === 3 && e.bytes[0] === 0xb1 &&
      (e.bytes[1] === POT.pitch.msb || e.bytes[1] === POT.pitch.lsb));
    assert.ok(pair.length > 10, "Deck B's fader reached cable 2");
    const finalPair = pairTrace(rig, { unit: 0, cable: 2, ch: 1, msb: POT.pitch.msb, lsb: POT.pitch.lsb }).pop().value;
    assert.ok(Math.abs(finalPair - expectedFader14(3900)) <= 45, `bridged fader ${finalPair} vs spec ${expectedFader14(3900)}`);
    assert.ok(nearly(engine.getValue("[Channel2]", "rate"), POT.pitch.mixxx(finalPair)), "and Mixxx's Deck 2 rate is that value");

    // LEDs go back the other way: Mixxx -> cable 2 -> radio -> Deck B
    engine.mixxxSet("[Channel2]", "cue_indicator", 1);
    await rig.run(300);
    assert.equal((await sim.status(1)).led.cueLit, true, "Deck B's cue LED lit through the bridge");
    assert.equal((await sim.status(0)).led.cueLit, false, "and Deck A's did not");
  });
});

// ---- identity --------------------------------------------------------------------------------------------------

test("SET_DECK over SysEx restarts the unit as the other deck: it keeps working and now drives [Channel2] on MIDI channel 2", async () => {
  await withWorld({}, async ({ sim, engine, rig }) => {
    const { events } = await sim.midi(0, 0, sysexFrame(P.sysex.deckAny, P.sysex.cmd.setDeck, [1]));
    const ack = events.filter((e) => e.k === "sysex").map((e) => parseSysex(e.bytes)).find((r) => r.code === P.sysex.ack);
    assert.ok(ack, "SET_DECK is acknowledged before the restart");
    await rig.run(1200); // flush + re-enumeration + ring settle is not needed for a fader
    assert.equal((await sim.status(0)).reboots, 1);
    engine.take();
    await rig.cmd("fader raw=4055");
    await rig.run(100);
    const rate = engine.take("setParameter").filter((c) => c.k === "rate");
    assert.ok(rate.length > 0 && rate.every((c) => c.g === "[Channel2]"), "Deck B's fader moves [Channel2]");
    assert.ok(rig.messages().some((m) => m.bytes[0] === 0xb1 && m.bytes[1] === POT.pitch.msb), "on MIDI channel 2");
  });
});

// ---------------------------------------------------------------------------------------------------------------

const filter = process.argv[2];
let failed = 0, ran = 0;
for (const { name, fn } of tests) {
  if (filter && !name.includes(filter)) continue;
  ran++;
  try {
    await fn();
    console.log(`ok ${ran} - ${name}`);
  } catch (e) {
    failed++;
    console.log(`not ok ${ran} - ${name}\n    ${String(e.stack ?? e).split("\n").join("\n    ")}`);
  }
}
if (observations.length) console.log(`\nobservations:\n${observations.map((o) => `  - ${o}`).join("\n")}`);
console.log(`\n${ran - failed} passed, ${failed} failed (${ran} tests)`);
process.exit(failed ? 1 : 0);
