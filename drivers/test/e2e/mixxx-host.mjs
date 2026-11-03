// A stand-in for Mixxx around the REAL drivers/mixxx mapping: it loads the XML bindings and the script files the
// way Mixxx does, runs the script in a vm context against a strict mock engine, and wires the script's MIDI in/out to
// units of the end-to-end simulator.
//
// What it models: the (status, midino) -> handler dispatch of Script-Binding controls, `engine.*` / `midi.*` calls
// with Mixxx's argument shapes, control ranges, and control-change connections (the LED feedback path). What it does
// NOT model (docs/E2E.md): Mixxx's audio engine, soft takeover, scratch physics, the ramp after scratchDisable.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import vm from "node:vm";

import { P, REPO } from "./sim.mjs";

const MIXXX_DIR = join(REPO, "drivers/mixxx");
const PREFIX = "EPDJ3000Mixxx";

/** The Script-Binding table from the checked-in XML: Map("status:midino" -> [{key, group}]). */
function readBindings() {
  const xml = readFileSync(join(MIXXX_DIR, "EP-DJ3000.midi.xml"), "utf8");
  const files = [...xml.matchAll(/<file filename="([^"]+)"(?: functionprefix="([^"]+)")?\/>/g)]
    .map((m) => ({ filename: m[1], functionprefix: m[2] }));
  const table = new Map();
  for (const [, body] of xml.matchAll(/<control>([\s\S]*?)<\/control>/g)) {
    const field = (name) => body.match(new RegExp(`<${name}>([^<]*)</${name}>`))[1].trim();
    const k = `${Number(field("status"))}:${Number(field("midino"))}`;
    if (!table.has(k)) table.set(k, []);
    table.get(k).push({ key: field("key"), group: field("group") });
  }
  return { files, table };
}

// key -> [min, max]; every name is in the Mixxx 2.5 controls manual (same list as drivers/test/mixxx/run.mjs).
const DECK_CONTROLS = {
  play: [0, 1], cue_default: [0, 1], cue_indicator: [0, 1], reverse: [0, 1], reverseroll: [0, 1], jog: [-3, 3],
  rate: [-1, 1], volume: [0, 1], bpm: [0, 300], file_bpm: [0, 300], tempo_tap: [0, 1], start_stop: [0, 1],
};

function controlRange(group, key) {
  if (/^\[Channel[12]\]$/.test(group) && key in DECK_CONTROLS) return DECK_CONTROLS[key];
  if (group === "[Master]" && key === "crossfader") return [-1, 1];
  if (/^\[QuickEffectRack1_\[Channel[12]\]\]$/.test(group) && key === "super1") return [0, 1];
  throw new Error(`mock engine: unknown Mixxx control ${group},${key}`);
}

/** Shared by every script instance (Mixxx has one engine, and one script instance per MIDI port). */
export function makeEngine() {
  const values = new Map();
  const connections = [];
  const scratching = new Set();
  const calls = [];
  const kv = (g, k) => `${g}|${k}`;

  function write(group, key, value) {
    const [min, max] = controlRange(group, key);
    assert.ok(typeof value === "number" && !Number.isNaN(value), `non-numeric write to ${group},${key}`);
    assert.ok(value >= min - 1e-9 && value <= max + 1e-9, `${group},${key} written out of range: ${value}`);
    const old = values.get(kv(group, key)) ?? 0;
    values.set(kv(group, key), value);
    if (old !== value) {
      for (const c of connections.slice()) if (c.connected && c.group === group && c.key === key) c.cb(value, group, key);
    }
  }

  const engine = {
    calls,
    scratching,
    getValue(g, k) { controlRange(g, k); return values.get(kv(g, k)) ?? 0; },
    setValue(g, k, v) { calls.push({ fn: "setValue", g, k, v }); write(g, k, v); },
    getParameter(g, k) { const [min, max] = controlRange(g, k); return ((values.get(kv(g, k)) ?? 0) - min) / (max - min); },
    setParameter(g, k, p) {
      assert.ok(p >= 0 && p <= 1, `setParameter out of 0..1: ${p}`);
      const [min, max] = controlRange(g, k);
      calls.push({ fn: "setParameter", g, k, p });
      write(g, k, min + p * (max - min)); // linear taper: enough to check ends and ratios
    },
    makeConnection(g, k, cb) {
      controlRange(g, k);
      const c = {
        group: g, key: k, cb, connected: true,
        disconnect() { c.connected = false; },
        trigger() { cb(values.get(kv(g, k)) ?? 0, g, k); },
      };
      connections.push(c);
      return c;
    },
    softTakeover() {},
    scratchEnable(deck, intervalsPerRev, rpm, alpha, beta, ramp) {
      assert.ok([1, 2, 3, 4].includes(deck), "scratchEnable deck is 1-based");
      calls.push({ fn: "scratchEnable", deck, intervalsPerRev, rpm, alpha, beta, ramp });
      scratching.add(deck);
    },
    scratchTick(deck, interval) {
      assert.ok(Number.isInteger(interval), "scratchTick takes an int");
      calls.push({ fn: "scratchTick", deck, interval, whileScratching: scratching.has(deck) });
    },
    scratchDisable(deck, ramp) { calls.push({ fn: "scratchDisable", deck, ramp }); scratching.delete(deck); },
    isScratching(deck) { return scratching.has(deck); },
    /** Mixxx itself changing a control (deck loaded, GUI, sync...): drives connections. */
    mixxxSet(g, k, v) { write(g, k, v); },
    take(fn) {
      const out = calls.filter((c) => !fn || c.fn === fn);
      const rest = fn ? calls.filter((c) => c.fn !== fn) : [];
      calls.splice(0, calls.length, ...rest);
      return out;
    },
  };
  return new Proxy(engine, {
    get(t, prop) {
      if (typeof prop === "string" && !(prop in t)) throw new Error(`mock engine: unknown engine.${prop}`);
      return t[prop];
    },
  });
}

/** One script instance (one MIDI port) as Mixxx would create it from the XML. */
export function loadScript(engine) {
  const { files, table } = readBindings();
  const sent = [];
  const midi = new Proxy({
    sendShortMsg(a, b, c) {
      for (const n of [a, b, c]) assert.ok(Number.isInteger(n) && n >= 0 && n <= 255, `bad MIDI byte ${n}`);
      sent.push([a, b, c]);
    },
  }, { get(t, p) { if (!(p in t)) throw new Error(`mock midi: unknown midi.${String(p)}`); return t[p]; } });
  const logs = [];
  const ctx = vm.createContext({ engine, midi, console: { log: (m) => logs.push(String(m)) }, print: (m) => logs.push(String(m)) });
  for (const f of files) vm.runInContext(readFileSync(join(MIXXX_DIR, f.filename), "utf8"), ctx, { filename: f.filename });
  const obj = ctx[PREFIX];
  let unbound = 0;
  return {
    ctx, obj, sent, logs,
    get unbound() { return unbound; },
    init() { obj.init.call(obj, "EP-DJ3000", false); },
    shutdown() { obj.shutdown.call(obj); },
    /** Raw MIDI from the unit. Messages with no binding (clock, SysEx, unbound CCs) are dropped, as Mixxx does. */
    receive(bytes) {
      if (bytes.length !== 3) { unbound++; return; }
      const [status, control, value] = bytes;
      const hits = table.get(`${status}:${control}`);
      if (!hits) { unbound++; return; }
      for (const h of hits) {
        const fn = vm.runInContext(h.key, ctx);
        fn.call(ctx[h.key.split(".")[0]], status & 0x0f, control, value, status, h.group);
      }
    },
    takeSent() { return sent.splice(0); },
  };
}

/**
 * Couples a simulator to script instances. `attach(script, unit, cable)` routes the unit's USB messages on that
 * cable into the script, and the script's MIDI output back to the unit on that cable. Virtual time advances in
 * `slice`-ms steps; after each one the events are delivered and the script's replies injected, so a closed loop
 * (platter tap -> Mixxx play -> LED note -> unit) costs one slice per hop.
 */
export class Rig {
  constructor(sim, engine, { slice = 2 } = {}) {
    this.sim = sim;
    this.engine = engine;
    this.slice = slice;
    this.ports = [];
    /** Everything the units sent, in order, as the scripts saw it (system messages included). */
    this.heard = [];
    /** Platform effects the units asked for (save, reboot, set deck, tone). */
    this.platform = [];
    /** The hardware the simulator was built as: "as5600" (the default: encoder + one hold pad) or "ring" (`init platter=ring`). */
    this.platter = "as5600";
    /** Where each unit's AS5600 is, in revolutions. An encoder cannot teleport, so scratching on it is relative to this. */
    this.encoderRev = new Map();
  }

  attach(script, unit, cable) {
    this.ports.push({ script, unit, cable });
    return script;
  }

  async #deliver(events) {
    for (const ev of events) {
      if (ev.e) { this.platform.push(ev); continue; }
      if (ev.k !== "msg" || !(ev.ports & 1)) continue;
      this.heard.push(ev);
      for (const p of this.ports) if (p.unit === ev.u && p.cable === ev.cable) p.script.receive(ev.bytes);
    }
    // Replies produced by the scripts go back on the cable they came in on; they may provoke more output.
    for (const p of this.ports) {
      for (const m of p.script.takeSent()) {
        const r = await this.sim.midi(p.unit, p.cable, m);
        await this.#deliver(r.events);
      }
    }
  }

  /** Delivers whatever the simulator has produced and the scripts' replies, without advancing time. */
  async flush(events = []) { await this.#deliver(events); }

  async run(ms) {
    for (let left = ms; left > 0; left -= this.slice) {
      const r = await this.sim.run(Math.min(this.slice, left));
      await this.#deliver(r.events);
    }
  }

  async ramp(what, from, to, ms, { unit = 0 } = {}) {
    for (let done = 0; done < ms;) {
      const len = Math.min(this.slice, ms - done);
      const a = from + ((to - from) * done) / ms;
      const b = done + len === ms ? to : from + ((to - from) * (done + len)) / ms; // the end lands exactly
      const r = await this.sim.cmd(`ramp u=${unit} what=${what} from=${a} to=${b} ms=${len}`);
      await this.#deliver(r.events);
      done += len;
    }
    if (what === "as5600") this.encoderRev.set(unit, to);
  }

  /** Turns the AS5600 platter by `revs` (negative = counter-clockwise) over `ms`, from wherever it is. */
  async spin(revs, ms, { unit = 0 } = {}) {
    const from = this.encoderRev.get(unit) ?? 0;
    await this.ramp("as5600", from, from + revs, ms, { unit });
  }

  /** Puts the AS5600 at `rev` (revolutions) and remembers it. Extra `key=value` options go to the simulator's `as5600` command. */
  async setEncoder(rev, { unit = 0, noise, valid } = {}) {
    let line = `as5600 u=${unit} rev=${rev}`;
    if (noise !== undefined) line += ` noise=${noise}`;
    if (valid !== undefined) line += ` valid=${valid ? 1 : 0}`;
    await this.cmd(line);
    this.encoderRev.set(unit, rev);
  }

  async cmd(line) {
    const r = await this.sim.cmd(line);
    await this.#deliver(r.events);
    return r;
  }

  /** The 3-byte channel messages unit `u` put on `cable`, in order, with their virtual time. */
  messages(u = 0, cable = 0) {
    return this.heard.filter((e) => e.u === u && e.cable === cable && e.bytes.length === 3);
  }
}

export { P };
