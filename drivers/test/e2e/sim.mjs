// Node client for the end-to-end simulator (firmware/core/tools/engine_sim.cpp, docs/E2E.md): spawns the real
// firmware Engine as a child process, sends it text commands and collects the JSON events it prints.
//
// Also a tiny SysEx helper written from docs/PROTOCOL.md section 3 (not from the firmware or the Swift
// library), so configuring a unit from a Node test does not borrow either implementation.

import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
export const P = require(join(here, "../../common/epdj3000-protocol.js"));
export const REPO = join(here, "../../..");

/** Path of the simulator binary, or null if it has not been built (tools/e2e.sh builds it). */
export function simBinary() {
  const candidates = [
    process.env.EPDJ_ENGINE_SIM,
    join(REPO, "firmware/core/build-e2e/epdj_engine_sim"),
  ].filter(Boolean);
  return candidates.find((p) => existsSync(p)) ?? null;
}

export const toHex = (bytes) => Buffer.from(bytes).toString("hex");
export const fromHex = (s) => [...Buffer.from(s, "hex")];

export class Sim {
  #child;
  #pending = null;
  #queue = Promise.resolve();
  #events = [];
  #stderr = "";
  #exited = null;
  #exitCode = null;
  /** Everything the units sent since `start()`: {t, u, k, ports, cable, bytes, usb}. */
  log = [];

  static async start(initArgs = "") {
    const bin = simBinary();
    if (!bin) throw new Error("epdj_engine_sim is not built (run tools/e2e.sh)");
    const sim = new Sim(bin);
    await sim.#ready;
    await sim.cmd(`init ${initArgs}`);
    return sim;
  }

  #ready;
  constructor(bin) {
    this.#child = spawn(bin, [], { stdio: ["pipe", "pipe", "pipe"] });
    this.#child.stderr.on("data", (d) => { this.#stderr += d; });
    this.#child.on("exit", (code, signal) => {
      this.#exitCode = signal ?? code;
      this.#exited = `exited (${signal ?? code})${this.#stderr ? `\n${this.#stderr}` : ""}`;
      if (this.#pending) this.#pending.reject(new Error(`simulator ${this.#exited}`));
    });
    let readyResolve;
    this.#ready = new Promise((r) => { readyResolve = r; });
    createInterface({ input: this.#child.stdout }).on("line", (line) => {
      let e;
      try { e = JSON.parse(line); } catch { throw new Error(`simulator printed a non-JSON line: ${line}`); }
      if (e.e === "ready") return readyResolve(e);
      if (e.e === "out") {
        const ev = { t: e.t, u: e.u, k: e.k, ports: e.ports, cable: e.cable, bytes: fromHex(e.hex), usb: e.usb ? fromHex(e.usb) : null };
        this.#events.push(ev);
        this.log.push(ev);
      } else if (e.e === "done" || e.e === "error") {
        const p = this.#pending;
        this.#pending = null;
        const events = this.#events;
        this.#events = [];
        if (e.e === "error") p.reject(new Error(`simulator: ${e.msg}`));
        else p.resolve({ events, done: e });
      } else {
        this.#events.push(e); // platform effects and the like
      }
    });
  }

  /** Sends one command line; resolves {events, done} when the simulator answers it. Commands never overlap. */
  cmd(line) {
    const run = () => new Promise((resolve, reject) => {
      if (this.#exited) return reject(new Error(`simulator ${this.#exited}`));
      this.#pending = { resolve, reject };
      this.#child.stdin.write(`${line}\n`);
    });
    const next = this.#queue.then(run, run);
    this.#queue = next.catch(() => {});
    return next;
  }

  run(ms) { return this.cmd(`run ms=${ms}`); }
  midi(u, port, bytes) { return this.cmd(`midi u=${u} port=${port} hex=${toHex(bytes)}`); }
  status(u = 0) { return this.cmd(`status u=${u}`).then((r) => r.done); }

  /** Quits the simulator; throws if it died or exited non-zero (a sanitizer report on stderr is included). */
  async close() {
    if (!this.#exited) {
      this.#child.stdin.end("quit\n");
      await new Promise((r) => this.#child.once("exit", r));
    }
    if (this.#exitCode !== 0) throw new Error(`simulator ${this.#exited}`);
  }
}

// ---- SysEx (PROTOCOL.md section 3) -------------------------------------------------------------------------------

export function pack8to7(bytes) {
  const out = [];
  for (let i = 0; i < bytes.length; i += 7) {
    const group = bytes.slice(i, i + 7);
    out.push(group.reduce((h, b, j) => h | ((b >> 7) << j), 0), ...group.map((b) => b & 0x7f));
  }
  return out;
}

export function unpack7to8(septets) {
  const out = [];
  for (let i = 0; i < septets.length; i += 8) {
    const hdr = septets[i];
    septets.slice(i + 1, i + 8).forEach((b, j) => out.push(b | (((hdr >> j) & 1) << 7)));
  }
  return out;
}

export function sysexFrame(deck, code, payload = []) {
  const body = [P.sysex.version, deck, code, ...pack8to7(payload)];
  const sum = body.reduce((a, b) => a + b, 0);
  return [0xf0, ...P.sysex.header, ...body, (-sum) & 0x7f, 0xf7];
}

/** Splits a reply frame into {deck, code, payload}; throws if the frame is not a well-formed EP-DJ3000 reply. */
export function parseSysex(bytes) {
  const hdr = P.sysex.header;
  if (bytes[0] !== 0xf0 || bytes[bytes.length - 1] !== 0xf7) throw new Error("sysex framing");
  if (hdr.some((b, i) => bytes[1 + i] !== b)) throw new Error("sysex header");
  const body = bytes.slice(4, -2);
  const sum = body.reduce((a, b) => a + b, 0);
  if (((-sum) & 0x7f) !== bytes[bytes.length - 2]) throw new Error("sysex checksum");
  return { deck: body[1], code: body[2], payload: unpack7to8(body.slice(3)) };
}

export const i32le = (v) => [v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff];

/** SET_CONFIG on `unit`, port 0; returns the reply frames. Throws unless the unit ACKs. */
export async function setConfig(sim, unit, deck, key, value) {
  const { events } = await sim.midi(unit, 0, sysexFrame(deck, P.sysex.cmd.setConfig, [key, ...i32le(value)]));
  const replies = events.filter((e) => e.k === "sysex" && e.u === unit).map((e) => parseSysex(e.bytes));
  const ack = replies.find((r) => r.code === P.sysex.ack && r.payload[0] === P.sysex.cmd.setConfig);
  if (!ack) throw new Error(`SET_CONFIG ${key}=${value} was not acknowledged: ${JSON.stringify(replies)}`);
  return replies;
}

/** Config key ids by name, read from the contract so a renumbering cannot hide behind a literal. */
export const CONFIG_KEYS = (() => {
  const contract = require(join(REPO, "protocol/epdj3000.json"));
  const keys = {};
  for (const k of contract.config.keys) keys[k.name] = k.id;
  return keys;
})();

// ---- wire rules that must hold for everything any unit ever sends (PROTOCOL.md 2 and 3) ---------------------------------

const PAIRS = [
  [P.cc.channelVolumeMsb, P.cc.channelVolumeLsb], [P.cc.crossfaderMsb, P.cc.crossfaderLsb], [P.cc.pitchMsb, P.cc.pitchLsb],
  [P.cc.knobMsb, P.cc.knobLsb], [P.cc.bpmMsb, P.cc.bpmLsb],
];

/**
 * Checks a simulator log against the contract's wire rules and returns the violations (empty = all hold):
 *  - a unit's own 14-bit pairs go MSB before LSB within one engine tick (the radio bridge is skipped: it may deliver two
 *    updates in one tick);
 *  - relative jog values are never 0 or 64, and a channel gets at most one jog message per 2 ms;
 *  - note-ons of the unit's controls have velocity 127, and a note is never turned on twice or off twice in a row;
 *  - every data byte is below 0x80, every SysEx frame is a well-formed EP-DJ3000 frame (checksum and 8->7 packing).
 */
export function wireRuleViolations(log) {
  const bad = [];
  const add = (ev, why) => { if (bad.length < 10) bad.push(`t=${ev.t} unit ${ev.u} cable ${ev.cable} ${toHex(ev.bytes)}: ${why}`); };
  const lastJog = new Map(), noteOn = new Map(), lastPairHalf = new Map();
  for (const ev of log) {
    if (ev.k === "sysex" && ev.bytes[1] === P.sysex.header[0]) {
      try { parseSysex(ev.bytes); } catch (e) { add(ev, `malformed EP-DJ3000 SysEx (${e.message})`); }
    }
    // Cable 1 is the TRS jack passing external gear through; its traffic is not the unit's own.
    if (ev.k !== "msg" || !(ev.ports & 1) || ev.cable === 1 || ev.bytes.length !== 3 || ev.bytes[0] >= 0xf0) continue;
    const [status, d1, d2] = ev.bytes;
    if (d1 > 0x7f || d2 > 0x7f) add(ev, "data byte >= 0x80");
    const ch = status & 0x0f, type = status & 0xf0, key = `${ev.u}:${ev.cable}:${ch}`;
    if (type === 0xb0 && d1 === P.cc.jog) {
      if (d2 === 0 || d2 === 64) add(ev, `jog value ${d2} must never be sent`);
      const prev = lastJog.get(key);
      if (prev !== undefined && ev.t - prev < 2000) add(ev, `jog messages ${ev.t - prev} us apart (>= 2000)`);
      lastJog.set(key, ev.t);
    }
    if (type === 0xb0 && ev.cable !== 2) {
      for (const [msb, lsb] of PAIRS) {
        if (d1 !== msb && d1 !== lsb) continue;
        const pk = `${key}:${msb}`;
        const last = lastPairHalf.get(pk);
        if (d1 === msb && last && last.t === ev.t && last.half === "lsb") add(ev, "MSB after LSB in one tick");
        lastPairHalf.set(pk, { t: ev.t, half: d1 === msb ? "msb" : "lsb" });
      }
    }
    if (type === 0x90 || type === 0x80) {
      const nk = `${key}:${d1}`;
      const on = type === 0x90 && d2 > 0;
      if (on && [P.note.cue, P.note.rev, P.note.touch, P.note.play, P.note.tap].includes(d1) && d2 !== 127) add(ev, `control note-on velocity ${d2}`);
      if (noteOn.get(nk) === on) add(ev, on ? "note turned on twice" : "note turned off twice");
      noteOn.set(nk, on);
    }
  }
  return bad;
}
