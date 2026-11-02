// Mixxx mapping tests. No dependencies:  node drivers/test/mixxx/run.mjs
//
// Loads the XML + scripts the way Mixxx does (script files listed in <scriptfiles>,
// in order, from the mapping's folder, into one global scope; init() called on the
// functionprefix object), then feeds raw MIDI bytes exactly as the unit sends them
// through the XML's (status, midino) bindings to the script handlers, against a
// mock engine. The mock is strict: it throws on any Mixxx control or API name that
// is not in the lists below (taken from the Mixxx 2.5 controls manual and
// controllerscriptinterfacelegacy.h), so a typo or invented name fails here.
//
// This proves the mapping logic and the XML <-> contract consistency. It does NOT
// prove anything about real Mixxx (see drivers/mixxx/README.md).

import { createRequire } from "node:module";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import assert from "node:assert/strict";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { buildXml, XML_PATH, PREFIX } from "./gen-xml.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const MIXXX_DIR = join(here, "../../mixxx");
const COMMON_JS = join(here, "../../common/epdj3000-protocol.js");
const CONTRACT_JSON = join(here, "../../../protocol/epdj3000.json");
const P = require(COMMON_JS);

// ---------------------------------------------------------------------------
// Minimal XML well-formedness parser (builds a tree; throws on malformed input)
// ---------------------------------------------------------------------------

function parseXml(src) {
  let i = 0;
  const fail = (m) => { throw new Error(`XML not well-formed at offset ${i}: ${m}`); };
  const nameRe = /[A-Za-z_:][-A-Za-z0-9_.:]*/y;
  const entityOk = (s) => !/&(?!(?:amp|lt|gt|quot|apos|#\d+|#x[0-9a-fA-F]+);)/.test(s);
  const readName = () => {
    nameRe.lastIndex = i;
    const m = nameRe.exec(src);
    if (!m) fail("expected a name");
    i += m[0].length;
    return m[0];
  };
  const skipWs = () => { while (/\s/.test(src[i] ?? "")) i++; };

  if (!src.startsWith("<?xml ")) fail("missing XML declaration");
  const declEnd = src.indexOf("?>");
  if (declEnd < 0) fail("unterminated XML declaration");
  i = declEnd + 2;

  const stack = [];
  let root = null;
  while (i < src.length) {
    if (src.startsWith("<!--", i)) {
      const end = src.indexOf("-->", i + 4);
      if (end < 0) fail("unterminated comment");
      if (src.slice(i + 4, end).includes("--")) fail('"--" inside a comment');
      i = end + 3;
    } else if (src.startsWith("<?", i)) {
      const end = src.indexOf("?>", i);
      if (end < 0) fail("unterminated processing instruction");
      i = end + 2;
    } else if (src.startsWith("</", i)) {
      i += 2;
      const name = readName();
      skipWs();
      if (src[i++] !== ">") fail("expected '>' in end tag");
      const open = stack.pop();
      if (!open || open.name !== name) fail(`end tag </${name}> does not match <${open ? open.name : "nothing"}>`);
    } else if (src[i] === "<") {
      i++;
      const node = { name: readName(), attrs: {}, children: [], text: "" };
      for (;;) {
        skipWs();
        if (src[i] === ">" || src.startsWith("/>", i)) break;
        const attr = readName();
        skipWs();
        if (src[i++] !== "=") fail("expected '=' after attribute name");
        skipWs();
        const q = src[i++];
        if (q !== '"' && q !== "'") fail("attribute value must be quoted");
        const end = src.indexOf(q, i);
        if (end < 0) fail("unterminated attribute value");
        const v = src.slice(i, end);
        if (v.includes("<") || !entityOk(v)) fail(`bad attribute value for ${attr}`);
        if (attr in node.attrs) fail(`duplicate attribute ${attr}`);
        node.attrs[attr] = v;
        i = end + 1;
      }
      if (stack.length === 0) {
        if (root) fail("more than one root element");
        root = node;
      } else {
        stack[stack.length - 1].children.push(node);
      }
      if (src.startsWith("/>", i)) { i += 2; } else { i++; stack.push(node); }
    } else {
      const end = src.indexOf("<", i);
      const text = src.slice(i, end < 0 ? src.length : end);
      if (!entityOk(text)) fail("unknown entity reference");
      if (stack.length === 0) {
        if (text.trim() !== "") fail("text outside the root element");
      } else {
        stack[stack.length - 1].text += text;
      }
      i = end < 0 ? src.length : end;
    }
  }
  if (stack.length) fail(`unclosed <${stack[stack.length - 1].name}>`);
  if (!root) fail("no root element");
  return root;
}

const child = (n, name) => n.children.find((c) => c.name === name);
const childText = (n, name) => (child(n, name)?.text ?? "").trim();

// ---------------------------------------------------------------------------
// Mapping as Mixxx would read it from the XML
// ---------------------------------------------------------------------------

function readMapping() {
  const xml = readFileSync(XML_PATH, "utf8");
  const root = parseXml(xml);
  const controller = child(root, "controller");
  const scriptFiles = child(controller, "scriptfiles").children.map((f) => ({
    filename: f.attrs.filename,
    functionprefix: f.attrs.functionprefix,
  }));
  const inputs = new Map(); // (status << 8 | midino) -> [{key, group}]
  const list = [];
  for (const c of child(controller, "controls").children) {
    const entry = {
      group: childText(c, "group"),
      key: childText(c, "key"),
      status: Number(childText(c, "status")), // Mixxx parses with toInt(&ok, 0): "0xB0" is hex
      midino: Number(childText(c, "midino")),
      options: child(c, "options").children.map((o) => o.name.toLowerCase()),
    };
    list.push(entry);
    const k = (entry.status << 8) | entry.midino;
    if (!inputs.has(k)) inputs.set(k, []);
    inputs.get(k).push(entry);
  }
  return { root, scriptFiles, inputs, list };
}

// ---------------------------------------------------------------------------
// Strict mock engine
// ---------------------------------------------------------------------------

const ENGINE_API = new Set([
  "getValue", "setValue", "getParameter", "setParameter", "makeConnection", "trigger",
  "softTakeover", "softTakeoverIgnoreNextValue", "scratchEnable", "scratchTick",
  "scratchDisable", "isScratching", "beginTimer", "stopTimer", "log",
]);
const MIDI_API = new Set(["sendShortMsg", "sendSysexMsg"]);

// key -> [min, max]; every name below is in the Mixxx 2.5 controls manual.
const DECK_CONTROLS = {
  play: [0, 1], cue_default: [0, 1], cue_indicator: [0, 1], reverse: [0, 1],
  reverseroll: [0, 1], jog: [-3, 3], rate: [-1, 1], volume: [0, 1], bpm: [0, 300],
  file_bpm: [0, 300], tempo_tap: [0, 1], start_stop: [0, 1],
};

function controlRange(group, key) {
  if (/^\[Channel[12]\]$/.test(group) && key in DECK_CONTROLS) return DECK_CONTROLS[key];
  if (group === "[Master]" && key === "crossfader") return [-1, 1];
  if (/^\[QuickEffectRack1_\[Channel[12]\]\]$/.test(group) && key === "super1") return [0, 1];
  throw new Error(`mock engine: unknown Mixxx control ${group},${key}`);
}

function makeEngine() {
  const values = new Map();
  const connections = [];
  const scratching = new Set();
  const calls = [];
  const kv = (g, k) => `${g}|${k}`;
  const rec = (fn, ...args) => calls.push({ fn, args });

  function write(group, key, value) {
    const [min, max] = controlRange(group, key);
    if (typeof value !== "number" || Number.isNaN(value)) throw new Error(`non-numeric write to ${group},${key}`);
    // A real Mixxx control clamps; the script must not rely on that for jog.
    assert.ok(value >= min - 1e-9 && value <= max + 1e-9, `${group},${key} written out of range: ${value}`);
    const old = values.get(kv(group, key)) ?? 0;
    values.set(kv(group, key), value);
    if (old !== value) {
      for (const c of connections.slice()) {
        if (c.connected && c.group === group && c.key === key) c.cb(value, group, key);
      }
    }
  }

  const engine = {
    calls,
    scratching,
    getValue(g, k) { controlRange(g, k); return values.get(kv(g, k)) ?? 0; },
    setValue(g, k, v) { rec("setValue", g, k, v); write(g, k, v); },
    getParameter(g, k) {
      const [min, max] = controlRange(g, k);
      return ((values.get(kv(g, k)) ?? 0) - min) / (max - min);
    },
    setParameter(g, k, p) {
      rec("setParameter", g, k, p);
      assert.ok(p >= 0 && p <= 1, `setParameter out of 0..1: ${p}`);
      const [min, max] = controlRange(g, k);
      write(g, k, min + p * (max - min)); // linear taper is enough to check the end points
    },
    makeConnection(g, k, cb) {
      controlRange(g, k);
      assert.equal(typeof cb, "function");
      rec("makeConnection", g, k);
      const c = {
        group: g, key: k, cb, connected: true,
        get isConnected() { return c.connected; },
        disconnect() { const was = c.connected; c.connected = false; rec("disconnect", g, k); return was; },
        trigger() { cb(values.get(kv(g, k)) ?? 0, g, k); },
      };
      connections.push(c);
      return c;
    },
    softTakeover(g, k, on) { controlRange(g, k); rec("softTakeover", g, k, on); },
    scratchEnable(deck, intervalsPerRev, rpm, alpha, beta, ramp) {
      assert.ok([1, 2, 3, 4].includes(deck), "scratchEnable deck is 1-based");
      for (const n of [intervalsPerRev, rpm, alpha, beta]) assert.equal(typeof n, "number");
      assert.equal(typeof ramp, "boolean");
      rec("scratchEnable", deck, intervalsPerRev, rpm, alpha, beta, ramp);
      scratching.add(deck);
    },
    scratchTick(deck, interval) {
      assert.ok(Number.isInteger(interval), "scratchTick takes an int");
      rec("scratchTick", deck, interval);
    },
    scratchDisable(deck, ramp) {
      assert.equal(typeof ramp, "boolean");
      rec("scratchDisable", deck, ramp);
      scratching.delete(deck);
    },
    isScratching(deck) { return scratching.has(deck); },
    // Mixxx itself changing a control (not a script call): drives connections.
    mixxxSet(g, k, v) { write(g, k, v); },
    connectionCount() { return connections.filter((c) => c.connected).length; },
    // Removes and returns the recorded calls (only those of one kind if `fn` is given).
    take(fn) {
      const out = calls.filter((c) => !fn || c.fn === fn);
      const rest = calls.filter((c) => fn && c.fn !== fn);
      calls.splice(0, calls.length, ...rest);
      return out;
    },
  };
  return new Proxy(engine, {
    get(t, prop) {
      if (typeof prop === "string" && !(prop in t) ) throw new Error(`mock engine: unknown engine.${prop}`);
      return t[prop];
    },
  });
}

// ---------------------------------------------------------------------------
// Loader / dispatcher (what Mixxx does around the script)
// ---------------------------------------------------------------------------

let mapping;
try {
  mapping = readMapping();
} catch (e) {
  console.log(`not ok 1 - EP-DJ3000.midi.xml could not be read as a well-formed Mixxx mapping\n    ${e.message}`);
  console.log("\n0 passed, 1 failed (aborted before running the other tests)");
  process.exit(1);
}

function loadMapping(engine) {
  const sent = [];
  const logs = [];
  const midi = new Proxy({
    sendShortMsg(a, b, c) {
      for (const n of [a, b, c]) assert.ok(Number.isInteger(n) && n >= 0 && n <= 255, `bad MIDI byte ${n}`);
      sent.push([a, b, c]);
    },
  }, { get(t, p) { if (!(p in t)) throw new Error(`mock midi: unknown midi.${String(p)}`); return t[p]; } });

  // Deliberately no `script` and no `components`: the mapping must not need them.
  const ctx = vm.createContext({
    engine, midi,
    console: { log: (m) => logs.push(String(m)) },
    print: (m) => logs.push(String(m)),
  });
  const prefixes = [];
  for (const f of mapping.scriptFiles) {
    vm.runInContext(readFileSync(join(MIXXX_DIR, f.filename), "utf8"), ctx, { filename: f.filename });
    if (f.functionprefix) prefixes.push(f.functionprefix);
  }
  assert.deepEqual(prefixes, [PREFIX]);
  const obj = ctx[PREFIX];
  let dropped = 0;

  const m = {
    ctx, obj, sent, logs,
    get dropped() { return dropped; },
    init(debugging = false) { obj.init.call(obj, "EP-DJ3000", debugging); },
    shutdown() { obj.shutdown.call(obj); },
    // One raw 3-byte channel-voice message from the unit.
    receive(bytes) {
      const [status, control, value] = bytes;
      const hits = mapping.inputs.get((status << 8) | control);
      if (!hits) { dropped++; return; }
      for (const h of hits) {
        const fn = vm.runInContext(h.key, ctx);
        fn.call(ctx[h.key.split(".")[0]], status & 0x0F, control, value, status, h.group);
      }
    },
    takeSent() { return sent.splice(0); },
  };
  return m;
}

// What the unit puts on the wire (PROTOCOL.md section 2), built from the contract constants.
const Unit = {
  cc: (ch, n, v) => [0xB0 | ch, n, v],
  jog: (ch, delta) => Unit.cc(ch, P.cc.jog, P.relativeCenter + Math.max(-63, Math.min(63, delta))),
  // MSB first, then LSB; `skip` leaves out a half (allowed when unchanged).
  pair: (ch, name, v14, skip = {}) => [
    ...(skip.msb ? [] : [Unit.cc(ch, P.cc[name + "Msb"], v14 >> 7)]),
    ...(skip.lsb ? [] : [Unit.cc(ch, P.cc[name + "Lsb"], v14 & 0x7F)]),
  ],
  noteOn: (ch, name) => [0x90 | ch, P.note[name], 127],
  noteOffVel0: (ch, name) => [0x90 | ch, P.note[name], 0],
  noteOff: (ch, name) => [0x80 | ch, P.note[name], 0],
};
const feed = (m, msgs) => msgs.forEach((x) => m.receive(x));

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

function fresh() {
  const engine = makeEngine();
  const m = loadMapping(engine);
  m.init();
  engine.take();
  m.takeSent();
  return { engine, m };
}

const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} !~ ${b}`);

// ---- XML ----

test("XML parser rejects malformed documents (the well-formedness check is real)", () => {
  const bad = [
    '<?xml version="1.0"?><a><b></a>',
    '<?xml version="1.0"?><a x="1" x="2"/>',
    '<?xml version="1.0"?><a>&bogus;</a>',
    '<?xml version="1.0"?><a x=1/>',
    '<?xml version="1.0"?><a/><b/>',
    '<?xml version="1.0"?><a><!-- a -- b --></a>',
    '<?xml version="1.0"?><a>',
    "<a/>",
  ];
  for (const b of bad) assert.throws(() => parseXml(b), /not well-formed/, b);
  assert.doesNotThrow(() => parseXml('<?xml version="1.0"?><a x="1"><b/>&amp;<!-- ok --></a>'));
});

test("XML is well-formed and has the Mixxx MIDI mapping structure", () => {
  assert.equal(mapping.root.name, "MixxxMIDIPreset");
  assert.equal(mapping.root.attrs.schemaVersion, "1");
  assert.ok(childText(child(mapping.root, "info"), "name"));
  const controller = child(mapping.root, "controller");
  assert.ok(controller.attrs.id);
  assert.ok(child(controller, "controls").children.length > 0);
});

test("checked-in XML is exactly what the generator produces from the contract constants", () => {
  assert.equal(readFileSync(XML_PATH, "utf8"), buildXml());
});

test("epdj3000-protocol.js next to the mapping is identical to drivers/common's (tools/gen_protocol.py writes both)", () => {
  assert.equal(
    readFileSync(join(MIXXX_DIR, "epdj3000-protocol.js"), "utf8"),
    readFileSync(COMMON_JS, "utf8"),
    "the two generated copies differ: run python3 tools/gen_protocol.py"
  );
});

test("<scriptfiles>: protocol constants load before the mapping script; files exist; one functionprefix", () => {
  assert.deepEqual(mapping.scriptFiles.map((f) => f.filename), ["epdj3000-protocol.js", "EP-DJ3000-scripts.js"]);
  assert.equal(mapping.scriptFiles[0].functionprefix, undefined);
  assert.equal(mapping.scriptFiles[1].functionprefix, PREFIX);
  for (const f of mapping.scriptFiles) assert.ok(readFileSync(join(MIXXX_DIR, f.filename), "utf8").length > 0);
  // The constants object must not be the init/shutdown object.
  assert.notEqual(PREFIX, "EPDJ3000");
});

test("every <status>/<midino> matches the contract (protocol/epdj3000.json), nothing missing, nothing extra", () => {
  const contract = JSON.parse(readFileSync(CONTRACT_JSON, "utf8"));
  const expected = new Set();
  for (const c of contract.midi.controls) {
    if (c.dir === "in") continue; // host -> device only; nothing to bind
    for (const ch of contract.midi.defaultChannels) {
      if (c.type === "note") {
        expected.add(`${0x90 | ch}:${c.number}`);
        expected.add(`${0x80 | ch}:${c.number}`);
      } else {
        expected.add(`${0xB0 | ch}:${c.number}`);
        if (c.type === "cc14") expected.add(`${0xB0 | ch}:${c.lsb}`);
      }
    }
  }
  const actual = new Set(mapping.list.map((c) => `${c.status}:${c.midino}`));
  assert.equal(actual.size, mapping.list.length, "duplicate (status, midino) pair");
  assert.deepEqual([...actual].sort(), [...expected].sort());
  // and the JS constants agree with the JSON the XML was checked against
  assert.deepEqual(P.defaultChannels, contract.midi.defaultChannels);
});

test("every binding is a Script-Binding to an existing handler, in the group of its channel", () => {
  const { ctx } = loadMapping(makeEngine());
  for (const c of mapping.list) {
    assert.deepEqual(c.options, ["script-binding"]);
    assert.match(c.key, new RegExp(`^${PREFIX}\\.[A-Za-z0-9]+$`));
    assert.equal(typeof vm.runInContext(c.key, ctx), "function", c.key);
    const ch = c.status & 0x0F;
    assert.equal(c.group, `[Channel${P.defaultChannels.indexOf(ch) + 1}]`);
  }
  assert.equal(P.defaultChannels.indexOf(0), 0);
  assert.equal(P.defaultChannels.indexOf(1), 1);
});

test("script uses only documented engine/midi functions and no script./components", () => {
  const code = readFileSync(join(MIXXX_DIR, "EP-DJ3000-scripts.js"), "utf8")
    .split("\n").filter((l) => !l.trim().startsWith("//")).join("\n");
  for (const [, name] of code.matchAll(/\bengine\.(\w+)/g)) assert.ok(ENGINE_API.has(name), `engine.${name}`);
  for (const [, name] of code.matchAll(/\bmidi\.(\w+)/g)) assert.ok(MIDI_API.has(name), `midi.${name}`);
  assert.ok(!/\bscript\.|\bcomponents\b/.test(code), "must not depend on script.* or components.js");
  assert.ok(!/=>|\bconst\b|\blet\b/.test(code), "keep ES5 style");
});

// ---- lifecycle / LEDs ----

test("init: soft takeover on deck volume/rate/filter (not crossfader), LED connections, LEDs start off", () => {
  const engine = makeEngine();
  const m = loadMapping(engine);
  m.init();
  const st = engine.take("softTakeover").map((c) => c.args.join("|")).sort();
  const want = [];
  for (const g of ["[Channel1]", "[Channel2]"]) {
    want.push(`${g}|volume|true`, `${g}|rate|true`, `[QuickEffectRack1_${g}]|super1|true`);
  }
  assert.deepEqual(st, want.sort());
  assert.equal(engine.connectionCount(), 4);
  assert.deepEqual(m.takeSent().sort(), [[0x90, 36, 0], [0x90, 39, 0], [0x91, 36, 0], [0x91, 39, 0]].sort());
  assert.ok(m.logs.some((l) => l.includes("init")));
});

test("init with a bad FADER_ROLE / REV_BEHAVIOR fails loudly", () => {
  for (const [prop, val] of [["FADER_ROLE", "filter"], ["REV_BEHAVIOR", "hold"]]) {
    const m = loadMapping(makeEngine());
    m.obj[prop] = val;
    assert.throws(() => m.init(), new RegExp(prop));
  }
});

test("LED feedback: Mixxx cue_indicator / play -> Note On vel 127/0 on the deck's channel", () => {
  const { engine, m } = fresh();
  engine.mixxxSet("[Channel1]", "cue_indicator", 1);
  engine.mixxxSet("[Channel2]", "play", 1);
  engine.mixxxSet("[Channel1]", "cue_indicator", 0);
  engine.mixxxSet("[Channel2]", "play", 0);
  assert.deepEqual(m.takeSent(), [[0x90, 36, 127], [0x91, 39, 127], [0x90, 36, 0], [0x91, 39, 0]]);
});

test("LED feedback for both decks reaches every enabled port on each deck's own channel, and is only ever notes 36/39", () => {
  // Two ports (two units on their own cables). Each unit ignores the other's channel (PROTOCOL.md 2, channel filter).
  const engine = makeEngine();
  const portA = loadMapping(engine);
  const portB = loadMapping(engine);
  portA.init();
  portB.init();
  engine.take();
  portA.takeSent();
  portB.takeSent();
  engine.mixxxSet("[Channel1]", "cue_indicator", 1);
  engine.mixxxSet("[Channel2]", "play", 1);
  engine.mixxxSet("[Channel2]", "play", 0);
  const expected = [[0x90, 36, 127], [0x91, 39, 127], [0x91, 39, 0]];
  const sentA = portA.takeSent();
  const sentB = portB.takeSent();
  assert.deepEqual(sentA, expected);
  assert.deepEqual(sentB, expected);
  // The play note is feedback only; the mapping never sends the transport CC or anything but Note On to the unit.
  for (const [status, data] of [...sentA, ...sentB]) {
    assert.equal(status & 0xF0, 0x90);
    assert.ok(data === P.note.cue || data === P.note.play, `unexpected note ${data}`);
  }
});

test("shutdown: LEDs off, connections released, scratch ended, held buttons released", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "touch"), Unit.noteOn(1, "rev"), Unit.noteOn(0, "cue")]);
  engine.take();
  m.takeSent();
  m.shutdown();
  assert.deepEqual(engine.take("scratchDisable").map((c) => c.args), [[1, false]]);
  const sets = engine.take("setValue").map((c) => c.args.join("|"));
  assert.ok(sets.includes("[Channel2]|reverseroll|0"));
  assert.ok(sets.includes("[Channel1]|cue_default|0"));
  assert.equal(engine.connectionCount(), 0);
  assert.deepEqual(m.takeSent().sort(), [[0x90, 36, 0], [0x90, 39, 0], [0x91, 36, 0], [0x91, 39, 0]].sort());
  engine.mixxxSet("[Channel1]", "play", 1); // no longer connected: no LED output
  assert.deepEqual(m.takeSent(), []);
});

// ---- jog / touch ----

test("touch -> scratchEnable with the right deck and 1024 intervals/rev; Note On vel 0 or Note Off -> scratchDisable", () => {
  for (const release of [Unit.noteOffVel0, Unit.noteOff]) {
    const { engine, m } = fresh();
    feed(m, [Unit.noteOn(0, "touch")]);
    assert.deepEqual(engine.take("scratchEnable").map((c) => c.args),
      [[1, P.jogTicksPerRev, 33 + 1 / 3, 1 / 8, (1 / 8) / 32, true]]);
    feed(m, [release(0, "touch")]);
    assert.deepEqual(engine.take("scratchDisable").map((c) => c.args), [[1, true]]);
    assert.equal(engine.isScratching(1), false);
  }
  assert.equal(P.jogTicksPerRev, 1024);
});

test("N jog ticks while touched -> scratchTick sum equals the ticks sent (both directions, clamp edges)", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "touch")]);
  engine.take();
  const deltas = [1, 1, 5, -3, 63, -63, 2, -1, 40, -40, 7];
  feed(m, deltas.map((d) => Unit.jog(0, d)));
  const ticks = engine.take("scratchTick");
  assert.deepEqual(ticks.map((c) => c.args), deltas.map((d) => [1, d]));
  assert.equal(ticks.reduce((s, c) => s + c.args[1], 0), deltas.reduce((s, d) => s + d, 0));
  // one full revolution clockwise = 1024 ticks
  const rev = Array(Math.ceil(P.jogTicksPerRev / 63)).fill(63);
  rev[rev.length - 1] = P.jogTicksPerRev - 63 * (rev.length - 1);
  feed(m, rev.map((d) => Unit.jog(0, d)));
  assert.equal(engine.take("scratchTick").reduce((s, c) => s + c.args[1], 0), 1024);
  assert.equal(engine.take("setValue").length, 0);
});

test("jog value 64 means no motion: no engine call in either mode", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.cc(0, P.cc.jog, 64)]);
  feed(m, [Unit.noteOn(0, "touch"), Unit.cc(0, P.cc.jog, 64)]);
  assert.deepEqual(engine.take().map((c) => c.fn), ["scratchEnable"]);
});

test("jog while untouched -> pitch bend via [ChannelN],jog; never scratchTick; clamped to Mixxx's -3..3", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.jog(0, 2)]);
  assert.deepEqual(engine.take().map((c) => [c.fn, ...c.args]), [["setValue", "[Channel1]", "jog", 2]]);
  engine.mixxxSet("[Channel1]", "jog", 0); // Mixxx consumed the accumulator in its audio callback
  feed(m, [Unit.jog(0, -1)]);
  assert.deepEqual(engine.take().map((c) => [c.fn, ...c.args]), [["setValue", "[Channel1]", "jog", -1]]);
  engine.mixxxSet("[Channel1]", "jog", 0);
  feed(m, [Unit.jog(0, 63)]);
  engine.mixxxSet("[Channel1]", "jog", 0);
  feed(m, [Unit.jog(0, -63)]);
  assert.deepEqual(engine.take().map((c) => c.args[2]), [3, -3]);
  engine.mixxxSet("[Channel1]", "jog", 0);
  m.obj.BEND_SCALE = 0.25;
  feed(m, [Unit.jog(0, 8)]);
  assert.deepEqual(engine.take().map((c) => c.args[2]), [2]);
});

test("jog while untouched: messages arriving before Mixxx consumes [ChannelN],jog add up (it is an accumulator); clamp holds", () => {
  const { engine, m } = fresh();
  // 500 Hz messages vs ~10 ms audio callbacks: several land before Mixxx reads and zeroes the control.
  feed(m, [Unit.jog(0, 2), Unit.jog(0, 1), Unit.jog(0, -1)]);
  assert.deepEqual(engine.take().map((c) => c.args[2]), [2, 3, 2]);
  assert.equal(engine.getValue("[Channel1]", "jog"), 2);
  engine.mixxxSet("[Channel1]", "jog", 0);
  feed(m, [Unit.jog(0, 2)]);
  assert.deepEqual(engine.take().map((c) => c.args[2]), [2]); // starts again from zero after consumption
  feed(m, [Unit.jog(0, 5)]);
  assert.deepEqual(engine.take().map((c) => c.args[2]), [3]); // 2 + 5 clamps to Mixxx's limit
  engine.mixxxSet("[Channel1]", "jog", 0);
  feed(m, [Unit.jog(1, -2)]);
  assert.deepEqual(engine.take().map((c) => [c.args[0], c.args[2]]), [["[Channel2]", -2]]); // decks do not share it
});

test("after release, jog goes back to pitch bend; a stray release without a touch does nothing", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOffVel0(0, "touch")]);
  assert.equal(engine.take().length, 0);
  feed(m, [Unit.noteOn(0, "touch"), Unit.jog(0, 4), Unit.noteOff(0, "touch"), Unit.jog(0, 4)]);
  assert.deepEqual(engine.take().map((c) => c.fn), ["scratchEnable", "scratchTick", "scratchDisable", "setValue"]);
});

test("a platter tap (touch down, up, then play pulse) leaves the deck toggled and not scratching", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "touch"), Unit.noteOffVel0(0, "touch"), Unit.noteOn(0, "play"), Unit.noteOffVel0(0, "play")]);
  assert.equal(engine.isScratching(1), false);
  assert.equal(engine.getValue("[Channel1]", "play"), 1);
});

// ---- buttons ----

test("play (note 39): press toggles [ChannelN],play; release (vel 0 or Note Off) ignored; LED follows", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "play"), Unit.noteOffVel0(0, "play")]);
  assert.equal(engine.getValue("[Channel1]", "play"), 1);
  assert.deepEqual(m.takeSent(), [[0x90, 39, 127]]);
  feed(m, [Unit.noteOn(0, "play"), Unit.noteOff(0, "play")]);
  assert.equal(engine.getValue("[Channel1]", "play"), 0);
  assert.deepEqual(m.takeSent(), [[0x90, 39, 0]]);
  assert.equal(engine.getValue("[Channel2]", "play"), 0);
});

test("a Note Off (0x8n) is a release whatever its release velocity (touch, cue, rev, play)", () => {
  const { engine, m } = fresh();
  const offWithVelocity = (ch, name) => [0x80 | ch, P.note[name], 64];
  feed(m, [Unit.noteOn(0, "touch"), offWithVelocity(0, "touch")]);
  assert.equal(engine.isScratching(1), false);
  feed(m, [Unit.noteOn(0, "cue"), offWithVelocity(0, "cue")]);
  assert.equal(engine.getValue("[Channel1]", "cue_default"), 0);
  feed(m, [Unit.noteOn(0, "rev"), offWithVelocity(0, "rev")]);
  assert.equal(engine.getValue("[Channel1]", "reverseroll"), 0);
  feed(m, [Unit.noteOn(0, "play"), offWithVelocity(0, "play")]);
  assert.equal(engine.getValue("[Channel1]", "play"), 1, "release must not toggle play back");
});

test("cue (note 36): cue_default follows the button (press 1, release 0)", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "cue")]);
  assert.equal(engine.getValue("[Channel1]", "cue_default"), 1);
  feed(m, [Unit.noteOffVel0(0, "cue")]);
  assert.equal(engine.getValue("[Channel1]", "cue_default"), 0);
  feed(m, [Unit.noteOn(1, "cue"), Unit.noteOff(1, "cue")]);
  assert.deepEqual(engine.take("setValue").map((c) => c.args.join("|")),
    ["[Channel1]|cue_default|1", "[Channel1]|cue_default|0", "[Channel2]|cue_default|1", "[Channel2]|cue_default|0"]);
});

test("rev (note 37), default \"roll\": reverseroll while held, released with the button", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(0, "rev")]);
  assert.equal(engine.getValue("[Channel1]", "reverseroll"), 1);
  feed(m, [Unit.noteOff(0, "rev")]);
  assert.equal(engine.getValue("[Channel1]", "reverseroll"), 0);
  assert.equal(engine.getValue("[Channel1]", "reverse"), 0);
});

test("rev with REV_BEHAVIOR = \"toggle\": reverse flips on each press, release ignored", () => {
  const { engine, m } = fresh();
  m.obj.REV_BEHAVIOR = "toggle";
  feed(m, [Unit.noteOn(0, "rev"), Unit.noteOffVel0(0, "rev")]);
  assert.equal(engine.getValue("[Channel1]", "reverse"), 1);
  feed(m, [Unit.noteOn(0, "rev"), Unit.noteOffVel0(0, "rev")]);
  assert.equal(engine.getValue("[Channel1]", "reverse"), 0);
  assert.equal(engine.take("setValue").filter((c) => c.args[1] === "reverseroll").length, 0);
});

test("tap (note 41) pulses tempo_tap; transport CC 29: 1 play, 2 pause, 0 start_stop", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(1, "tap"), Unit.noteOffVel0(1, "tap")]);
  assert.deepEqual(engine.take("setValue").map((c) => c.args.join("|")),
    ["[Channel2]|tempo_tap|1", "[Channel2]|tempo_tap|0"]);
  const T = P.enums.TransportState;
  feed(m, [Unit.cc(0, P.cc.transport, T.playing)]);
  assert.equal(engine.getValue("[Channel1]", "play"), 1);
  feed(m, [Unit.cc(0, P.cc.transport, T.paused)]);
  assert.equal(engine.getValue("[Channel1]", "play"), 0);
  engine.take();
  feed(m, [Unit.cc(0, P.cc.transport, T.stopped)]);
  assert.deepEqual(engine.take("setValue").map((c) => c.args.join("|")),
    ["[Channel1]|start_stop|1", "[Channel1]|start_stop|0"]);
  feed(m, [Unit.cc(0, P.cc.transport, 99)]);
  assert.equal(engine.take().length, 0);
});

// ---- 14-bit controls ----

test("slide pot set to CROSSFADER (CC 23/55, FADER_ROLE default): 14-bit 0 -> crossfader -1, 16383 -> +1, mid ~0, exact ratio in between", () => {
  const { engine, m } = fresh();
  feed(m, Unit.pair(0, "crossfader", 0));
  assert.equal(engine.getValue("[Master]", "crossfader"), -1);
  feed(m, Unit.pair(0, "crossfader", 16383));
  assert.equal(engine.getValue("[Master]", "crossfader"), 1);
  feed(m, Unit.pair(0, "crossfader", 8192));
  near(engine.getValue("[Master]", "crossfader"), 8192 / 16383 * 2 - 1);
  engine.take();
  feed(m, Unit.pair(0, "crossfader", 12345));
  const [lastMsb, lastLsb] = engine.take("setParameter");
  assert.equal(lastMsb.args[0], "[Master]");
  assert.equal(lastMsb.args[1], "crossfader");
  near(lastLsb.args[2], 12345 / 16383);
});

test("14-bit sweep with unchanged halves skipped reproduces every value exactly", () => {
  const { engine, m } = fresh();
  let prev = -1;
  for (let v = 0; v <= 16383; v += 13) {
    const skip = prev < 0 ? {} : { msb: (prev >> 7) === (v >> 7), lsb: (prev & 127) === (v & 127) };
    feed(m, Unit.pair(0, "channelVolume", v, skip));
    if (prev < 0 || !(skip.msb && skip.lsb)) {
      near(engine.getParameter("[Channel1]", "volume"), v / 16383, 1e-12);
    }
    prev = v;
  }
  feed(m, Unit.pair(0, "channelVolume", 16383, { msb: false, lsb: false }));
  assert.equal(engine.getValue("[Channel1]", "volume"), 1);
});

test("14-bit: LSB-only or MSB-only updates use the stored other half; nothing applied before both halves were seen", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.cc(0, P.cc.pitchLsb, 50)]);
  assert.equal(engine.take("setParameter").length, 0, "LSB alone must not move the control");
  feed(m, [Unit.cc(0, P.cc.pitchMsb, 10)]);
  assert.deepEqual(engine.take("setParameter").map((c) => c.args[2]), [((10 << 7) | 50) / 16383]);
  feed(m, [Unit.cc(0, P.cc.pitchLsb, 60)]);
  feed(m, [Unit.cc(0, P.cc.pitchMsb, 11)]);
  assert.deepEqual(engine.take("setParameter").map((c) => c.args[2]),
    [((10 << 7) | 60) / 16383, ((11 << 7) | 60) / 16383]);
  // other decks keep separate halves
  feed(m, [Unit.cc(1, P.cc.pitchMsb, 99)]);
  assert.equal(engine.take("setParameter").length, 0);
});

test("the unit's default faderRole CHANNEL_VOLUME (CC 7/39) -> deck volume; PITCH (CC 25/57) -> deck rate; ends map to the control's range", () => {
  const { engine, m } = fresh();
  feed(m, Unit.pair(1, "channelVolume", 0));
  assert.equal(engine.getValue("[Channel2]", "volume"), 0);
  feed(m, Unit.pair(1, "channelVolume", 16383));
  assert.equal(engine.getValue("[Channel2]", "volume"), 1);
  feed(m, Unit.pair(0, "pitch", 0));
  assert.equal(engine.getValue("[Channel1]", "rate"), -1);
  feed(m, Unit.pair(0, "pitch", 16383));
  assert.equal(engine.getValue("[Channel1]", "rate"), 1);
  assert.equal(engine.getValue("[Channel1]", "volume"), 0, "deck 1 volume untouched by deck 2");
});

test("FADER_ROLE constant redirects the CROSSFADER CC pair 23/55 (volume, rate)", () => {
  for (const [role, key, lo, hi] of [["volume", "volume", 0, 1], ["rate", "rate", -1, 1]]) {
    const { engine, m } = fresh();
    m.obj.FADER_ROLE = role;
    feed(m, Unit.pair(1, "crossfader", 0));
    assert.equal(engine.getValue("[Channel2]", key), lo);
    feed(m, Unit.pair(1, "crossfader", 16383));
    assert.equal(engine.getValue("[Channel2]", key), hi);
    assert.equal(engine.getValue("[Master]", "crossfader"), 0);
  }
});

test("knob (CC 24/56) -> deck Quick Effect knob super1 0..1", () => {
  const { engine, m } = fresh();
  feed(m, Unit.pair(0, "knob", 0));
  assert.equal(engine.getValue("[QuickEffectRack1_[Channel1]]", "super1"), 0);
  feed(m, Unit.pair(0, "knob", 16383));
  assert.equal(engine.getValue("[QuickEffectRack1_[Channel1]]", "super1"), 1);
  feed(m, Unit.pair(1, "knob", 8192));
  near(engine.getValue("[QuickEffectRack1_[Channel2]]", "super1"), 8192 / 16383);
});

test("bpm CC 20/52 sets deck tempo (BPM x 10); range-checked; needs an analysed track; echo within 0.05 BPM ignored", () => {
  const { engine, m } = fresh();
  feed(m, Unit.pair(0, "bpm", 1285));
  assert.equal(engine.take("setValue").length, 0, "no track loaded: file_bpm is 0");
  engine.mixxxSet("[Channel1]", "file_bpm", 120);
  engine.mixxxSet("[Channel1]", "bpm", 120);
  feed(m, Unit.pair(0, "bpm", 1285));
  assert.equal(engine.getValue("[Channel1]", "bpm"), 128.5);
  engine.take();
  feed(m, Unit.pair(0, "bpm", 1285, { msb: true }));
  assert.equal(engine.take("setValue").length, 0, "same tempo: no rewrite");
  engine.mixxxSet("[Channel1]", "bpm", 128.46); // rate-adjusted value Mixxx reports back
  feed(m, Unit.pair(0, "bpm", 1285));
  assert.equal(engine.take("setValue").length, 0, "within 0.05 BPM of the unit's rounded value");
  feed(m, Unit.pair(0, "bpm", P.bpmMinX10 - 1));
  feed(m, Unit.pair(0, "bpm", P.bpmMaxX10 + 1));
  assert.equal(engine.take("setValue").length, 0, "outside 20.0..300.0");
  feed(m, Unit.pair(0, "bpm", P.bpmMaxX10));
  assert.equal(engine.getValue("[Channel1]", "bpm"), 300);
});

test("bpmNudge CC 21: 0.1 BPM per step relative to 64; 64 = no change; clamped; ignored without a track", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.cc(0, P.cc.bpmNudge, 65)]);
  assert.equal(engine.take("setValue").length, 0);
  engine.mixxxSet("[Channel1]", "file_bpm", 120);
  engine.mixxxSet("[Channel1]", "bpm", 120);
  feed(m, [Unit.cc(0, P.cc.bpmNudge, 65)]);
  near(engine.getValue("[Channel1]", "bpm"), 120.1);
  feed(m, [Unit.cc(0, P.cc.bpmNudge, 64 - 3)]);
  near(engine.getValue("[Channel1]", "bpm"), 119.8);
  engine.take();
  feed(m, [Unit.cc(0, P.cc.bpmNudge, 64)]);
  assert.equal(engine.take().filter((c) => c.fn === "setValue").length, 0);
  engine.mixxxSet("[Channel1]", "bpm", 299.95);
  feed(m, [Unit.cc(0, P.cc.bpmNudge, 64 + 63)]);
  assert.equal(engine.getValue("[Channel1]", "bpm"), 300);
});

// ---- decks / cables ----

test("deck by channel: ch1 -> [Channel1], ch2 -> [Channel2]; other channels are not bound and handlers ignore them", () => {
  const { engine, m } = fresh();
  feed(m, [Unit.noteOn(1, "touch"), Unit.jog(1, 5)]);
  assert.deepEqual(engine.take().map((c) => [c.fn, c.args[0]]), [["scratchEnable", 2], ["scratchTick", 2]]);
  assert.equal(engine.isScratching(1), false);
  const before = m.dropped;
  feed(m, [Unit.noteOn(2, "touch"), Unit.jog(2, 5), Unit.noteOn(15, "play")]);
  assert.equal(m.dropped - before, 3);
  assert.equal(engine.take().length, 0);
  for (const name of ["jog", "touch", "cue", "rev", "play", "tap", "crossfader", "channelVolume", "pitch", "knob", "bpm", "bpmNudge", "transport"]) {
    m.obj[name](5, 22, 70, 0xB5, "[Channel1]"); // direct call as if a mis-bound channel arrived
  }
  assert.equal(engine.take().length, 0);
});

test("every deck-targeting control lands on its own deck only", () => {
  const { engine, m } = fresh();
  const per = (ch) => {
    feed(m, [Unit.noteOn(ch, "play"), Unit.noteOn(ch, "cue"), Unit.noteOn(ch, "rev"), Unit.noteOn(ch, "touch"), Unit.jog(ch, 3)]);
    feed(m, Unit.pair(ch, "channelVolume", 16383));
    feed(m, Unit.pair(ch, "pitch", 16383));
    feed(m, Unit.pair(ch, "knob", 16383));
    // scratch calls carry a deck number; setValue/setParameter carry a group
    return engine.take().map((c) => (typeof c.args[0] === "number" ? `[Channel${c.args[0]}]` : c.args[0]));
  };
  const groupsOf = (arr) => new Set(arr.map((g) => g.replace(/^\[QuickEffectRack1_(\[Channel\d\])\]$/, "$1")));
  assert.deepEqual([...groupsOf(per(0))], ["[Channel1]"]);
  assert.deepEqual([...groupsOf(per(1))], ["[Channel2]"]);
});

test("two units on their own cables, default fader role: each pot cuts only its own deck's volume; the master crossfader is never touched", () => {
  const engine = makeEngine();
  const unitA = loadMapping(engine);
  const unitB = loadMapping(engine);
  unitA.init();
  unitB.init();
  engine.take();

  // A sweeps up while B sweeps down, interleaved, the way two hands would.
  for (const v of [0, 4096, 8192, 12288, 16383]) {
    feed(unitA, Unit.pair(0, "channelVolume", v));
    feed(unitB, Unit.pair(1, "channelVolume", 16383 - v));
  }
  const writes = engine.take();
  assert.ok(writes.length > 0);
  assert.deepEqual([...new Set(writes.map((c) => `${c.fn}|${c.args[0]}|${c.args[1]}`))].sort(),
    ["setParameter|[Channel1]|volume", "setParameter|[Channel2]|volume"]);
  assert.equal(writes.filter((c) => String(c.args[0]).startsWith("[Master]")).length, 0);
  assert.equal(engine.getValue("[Channel1]", "volume"), 1);
  assert.equal(engine.getValue("[Channel2]", "volume"), 0);
  assert.equal(engine.getValue("[Master]", "crossfader"), 0);
});

test("linked deck on a second cable: a second script instance sharing the engine drives deck 2 independently", () => {
  const engine = makeEngine();
  const cable0 = loadMapping(engine); // Deck A's own port
  const cable2 = loadMapping(engine); // "Linked deck" port carrying Deck B on channel 2
  cable0.init();
  cable2.init();
  engine.take();
  cable0.takeSent();
  cable2.takeSent();

  feed(cable0, [Unit.noteOn(0, "touch")]);
  feed(cable2, [Unit.noteOn(1, "touch")]);
  assert.deepEqual([...engine.scratching].sort(), [1, 2]);
  feed(cable0, [Unit.jog(0, 4)]);
  feed(cable2, [Unit.jog(1, -6)]);
  assert.deepEqual(engine.take("scratchTick").map((c) => c.args), [[1, 4], [2, -6]]);

  // 14-bit state is per instance: B's halves arrive only on cable 2
  feed(cable2, Unit.pair(1, "channelVolume", 16383));
  assert.equal(engine.getValue("[Channel2]", "volume"), 1);
  assert.equal(engine.getValue("[Channel1]", "volume"), 0);

  // LEDs reach the port that owns the instance
  feed(cable2, [Unit.noteOn(1, "play")]);
  assert.ok(cable2.takeSent().some((x) => x[0] === 0x91 && x[1] === 39 && x[2] === 127));

  // shutting one instance down leaves the other connected and scratching state alone
  cable2.shutdown();
  assert.equal(engine.connectionCount(), 4);
  assert.equal(engine.isScratching(1), true);
  assert.equal(engine.isScratching(2), false);
});

// ---------------------------------------------------------------------------

let failed = 0;
tests.forEach(({ name, fn }, i) => {
  try {
    fn();
    console.log(`ok ${i + 1} - ${name}`);
  } catch (e) {
    failed++;
    console.log(`not ok ${i + 1} - ${name}\n    ${String(e.stack ?? e).split("\n").join("\n    ")}`);
  }
});
console.log(`\n${tests.length - failed} passed, ${failed} failed (${tests.length} tests)`);
process.exit(failed ? 1 : 0);
