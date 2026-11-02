// VirtualDJ driver checks. No dependencies:  node drivers/test/virtualdj/check.mjs
//
// Proves: both XML files are well-formed; they use only elements and attributes from VirtualDJ's
// official MIDI definition reference; every MIDI number, channel, the jog resolution and the USB
// vid/pid agree with protocol/epdj3000.json; every contract control is either mapped or listed
// below as deliberately unmapped; every mapper action starts with a verb from the official verb
// list; the README mentions what the files contain. It also feeds deliberately broken copies to
// the same validator to show the checks can fail.
//
// Does NOT prove anything about how VirtualDJ behaves with these files. VirtualDJ is closed source
// and was not available; see drivers/virtualdj/README.md.

import { readFileSync } from "node:fs";
import assert from "node:assert/strict";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const VDJ = join(here, "../../virtualdj");
const PATHS = {
  def: join(VDJ, "Devices/EPDJ3000.xml"),
  map: join(VDJ, "Mappers/EP-DJ3000.xml"),
  readme: join(VDJ, "README.md"),
  verbs: join(here, "vdj-verbs.txt"),
  contract: join(here, "../../../protocol/epdj3000.json"),
};

// ---------------------------------------------------------------------------
// What the files are supposed to contain, expressed against the contract
// ---------------------------------------------------------------------------

// Contract control -> VirtualDJ element. `led` adds an LED that follows the button's own action.
// `global` omits deck="" (the crossfader is not a deck control).
const BIND = [
  { id: "cue", name: "CUE", action: "cue_stop", led: "LED_CUE" },
  { id: "rev", name: "REV", action: "reverse" },
  { id: "touch", name: "JOG_TOUCH", action: "touchwheel_touch" },
  { id: "play", name: "PLAY", action: "play_pause", led: "LED_PLAY" },
  { id: "jog", name: "JOG", action: "touchwheel" },
  { id: "crossfader", name: "CROSSFADER", action: "crossfader", global: true },
  { id: "channelVolume", name: "LEVEL", action: "level" },
  { id: "pitch", name: "PITCH", action: "pitch" },
  { id: "knob", name: "KNOB", action: "filter" },
];

// Contract controls with no VirtualDJ binding, and why. A new contract control must land in one
// of the two tables or this check fails.
const UNMAPPED = {
  bpm: "only sent in SYNC_MASTER/knobRole=BPM; VirtualDJ has no BPM-from-controller verb that is not a track edit",
  bpmNudge: "only sent in SYNC_MASTER; clock-master mode is not a DJ-software mode",
  tap: "only sent in SYNC_MASTER; clock-master mode is not a DJ-software mode",
  transport: "only sent in the unit's sync modes; the play note already carries deck state",
};

// Units default to MIDI channel 1 / 2 (contract midi.defaultChannels). Second one gets this suffix.
const SUFFIX = ["", "_CH2"];

// ---------------------------------------------------------------------------
// Minimal strict XML parser (throws on malformed input)
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
      const node = { name: readName(), attrs: {}, children: [] };
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
      if (stack.length === 0 && text.trim() !== "") fail("text outside the root element");
      if (stack.length > 0 && text.trim() !== "") fail("unexpected text inside an element (VirtualDJ files have none)");
      i = end < 0 ? src.length : end;
    }
  }
  if (stack.length) fail(`unclosed <${stack[stack.length - 1].name}>`);
  if (!root) fail("no root element");
  return root;
}

// ---------------------------------------------------------------------------
// Official vocabulary (https://www.virtualdj.com/wiki/ControllerDefinitionMIDI.html)
// ---------------------------------------------------------------------------

const DEVICE_ATTRS = new Set(["name", "type", "description", "author", "version", "decks", "singledeck",
  "padColumns", "padRows", "padSides", "motor", "platform", "vid", "pid", "sysexid", "drivername",
  "drivernameout", "macdrivername", "macdrivernameout", "identifier", "sysexquery", "sysexanswer"]);
const COMMON = ["name", "deck", "channel"];
const ELEMENT_ATTRS = {
  audio: new Set(["name", "description", "image", "input", "output", "mic", "linein", "timecode", "record",
    "sampler", "booth", "mixer", "asio", "coreAudio", "vid", "pid", "mid", "vid2", "pid2", "mid2",
    "identifier", "asioOnly", "controllerIdentifier", "outputChannels", "inputChannels"]),
  button: new Set([...COMMON, "note", "cc", "value", "off", "inverted", "autoled", "nbdecks"]),
  slider: new Set([...COMMON, "cc", "ccmsb", "note", "aftertouch", "pitch", "min", "max", "zero",
    "zerorange", "inverted", "ghost", "nozero"]),
  jog: new Set([...COMMON, "cc", "note", "ccback", "inverted", "zero", "full"]),
  led: new Set([...COMMON, "note", "noteoff", "default", "zero", "min", "max", "cc", "value", "ccoff", "sendstatic"]),
};
const NUMERIC = new Set(["note", "cc", "ccmsb", "zero", "full", "channel", "deck", "decks", "version", "vid", "pid", "output"]);
const MAPPER_ATTRS = new Set(["device", "author", "version", "date", "priority"]);

// "0x24" or "36" -> number, else NaN
const num = (v) => (/^0x[0-9a-f]+$/i.test(v) ? parseInt(v, 16) : /^\d+$/.test(v) ? parseInt(v, 10) : NaN);

// ---------------------------------------------------------------------------
// Validator: returns a list of problems (empty = fine)
// ---------------------------------------------------------------------------

function verbOf(action, verbs, bad, where) {
  for (const part of action.split(/[&?:]+/)) {
    let cmd = part.trim().replace(/^[(\s]+/, "");
    cmd = cmd.replace(/^deck\s+\S+\s+/, "");
    const verb = cmd.split(/\s+/)[0];
    if (!verb) bad(`${where}: empty command in action "${action}"`);
    else if (!verbs.has(verb)) bad(`${where}: "${verb}" is not in the official VDJscript verb list`);
  }
}

function expectedElements(contract, bad) {
  const byId = new Map(contract.midi.controls.map((c) => [c.id, c]));
  const out = new Map(); // name -> { tag, attrs, mapAction? }
  contract.midi.defaultChannels.forEach((channel, k) => {
    const sfx = SUFFIX[k];
    if (sfx === undefined) { bad(`contract has more default channels than SUFFIX entries (${k})`); return; }
    for (const b of BIND) {
      const c = byId.get(b.id);
      if (!c) { bad(`BIND refers to ${b.id}, which is not in the contract`); continue; }
      const name = b.name + sfx;
      const common = { name, channel, ...(b.global ? {} : { deck: 1 }) };
      if (c.type === "note") {
        if (c.dir === "in") bad(`${b.id}: contract direction is "in", but a button reads controls the unit sends`);
        out.set(name, { tag: "button", attrs: { note: c.number, ...common }, action: b.action });
        if (b.led) {
          if (c.dir === "out") bad(`${b.id}: contract direction is "out", the unit does not accept it as LED feedback`);
          out.set(b.led + sfx, {
            tag: "led",
            attrs: { note: c.number, name: b.led + sfx, default: name, channel, deck: 1 },
          });
        }
      } else if (c.type === "ccRel") {
        out.set(name, {
          tag: "jog",
          attrs: { cc: c.number, zero: contract.midi.relativeCenter, full: contract.midi.jogTicksPerRev, ...common },
          action: b.action,
        });
      } else if (c.type === "cc14") {
        // VirtualDJ's convention (EQ_LOW example): cc = LSB controller, ccmsb = MSB controller.
        out.set(name, { tag: "slider", attrs: { cc: c.lsb, ccmsb: c.number, ...common }, action: b.action });
      } else {
        bad(`${b.id}: contract type ${c.type} has no VirtualDJ element in this check`);
      }
    }
  });
  const classified = new Set([...BIND.map((b) => b.id), ...Object.keys(UNMAPPED)]);
  for (const c of contract.midi.controls) {
    if (!classified.has(c.id)) bad(`contract control "${c.id}" is neither bound nor listed as unmapped`);
  }
  for (const id of Object.keys(UNMAPPED)) {
    if (!byId.has(id)) bad(`UNMAPPED lists "${id}", which is not in the contract`);
    if (BIND.some((b) => b.id === id)) bad(`"${id}" is both bound and unmapped`);
  }
  return out;
}

function validate({ defXml, mapXml, contract, verbs }) {
  const problems = [];
  const bad = (m) => problems.push(m);

  let def, map;
  try { def = parseXml(defXml); } catch (e) { return [`definition: ${e.message}`]; }
  try { map = parseXml(mapXml); } catch (e) { return [`mapper: ${e.message}`]; }

  const vid = num(contract.device.usbVendorId);
  const pid = num(contract.device.usbProductId);
  const expected = expectedElements(contract, bad);

  // --- definition root
  if (def.name !== "device") bad(`definition root is <${def.name}>, expected <device>`);
  for (const a of Object.keys(def.attrs)) if (!DEVICE_ATTRS.has(a)) bad(`<device> attribute "${a}" is not in the official reference`);
  if (def.attrs.type !== "MIDI") bad(`<device type> must be "MIDI", is "${def.attrs.type}"`);
  if (!/^[A-Za-z0-9_]+$/.test(def.attrs.name ?? "")) bad(`<device name> "${def.attrs.name}" is not a short identifier`);
  if (def.attrs.decks !== "1") bad(`<device decks> must be "1" (one unit = one single-deck controller), is "${def.attrs.decks}"`);
  if (Number.isNaN(num(def.attrs.version ?? ""))) bad(`<device version> "${def.attrs.version}" is not a number`);
  if (num(def.attrs.vid ?? "") !== vid) bad(`<device vid> ${def.attrs.vid} != contract ${contract.device.usbVendorId}`);
  if (num(def.attrs.pid ?? "") !== pid) bad(`<device pid> ${def.attrs.pid} != contract ${contract.device.usbProductId}`);

  // --- definition children
  const seen = new Map();
  let audio = 0;
  for (const el of def.children) {
    const allowed = ELEMENT_ATTRS[el.name];
    if (!allowed) { bad(`<${el.name}> is not an element this driver uses`); continue; }
    for (const [a, v] of Object.entries(el.attrs)) {
      if (!allowed.has(a)) bad(`<${el.name} name="${el.attrs.name}"> attribute "${a}" is not in the official reference`);
      if (NUMERIC.has(a) && Number.isNaN(num(v))) bad(`<${el.name} name="${el.attrs.name}"> ${a}="${v}" is not a number`);
    }
    for (const a of ["note", "cc", "ccmsb", "zero"]) {
      const v = num(el.attrs[a] ?? "0");
      if (!(v >= 0 && v <= 127)) bad(`<${el.name} name="${el.attrs.name}"> ${a} out of MIDI range 0..127`);
    }
    if (el.attrs.channel !== undefined && !(num(el.attrs.channel) >= 0 && num(el.attrs.channel) <= 15)) {
      bad(`<${el.name} name="${el.attrs.name}"> channel out of range 0..15`);
    }
    if (el.name === "audio") {
      audio++;
      if (num(el.attrs.vid ?? "") !== vid || num(el.attrs.pid ?? "") !== pid) bad("<audio> vid/pid differ from the contract");
      continue;
    }
    const n = el.attrs.name;
    if (!/^[A-Z0-9_]+$/.test(n ?? "")) bad(`<${el.name}> name "${n}" must be UPPER_SNAKE`);
    if (seen.has(n)) bad(`duplicate element name ${n}`);
    seen.set(n, el);
  }
  if (audio !== 1) bad(`expected exactly one <audio>, found ${audio}`);

  // --- definition vs contract
  for (const [name, want] of expected) {
    const got = seen.get(name);
    if (!got) { bad(`definition is missing ${want.tag} ${name}`); continue; }
    if (got.name !== want.tag) bad(`${name}: is <${got.name}>, expected <${want.tag}>`);
    for (const key of new Set([...Object.keys(want.attrs), ...Object.keys(got.attrs)])) {
      const w = want.attrs[key];
      const g = got.attrs[key];
      if (w === undefined) bad(`${name}: unexpected attribute ${key}="${g}"`);
      else if (g === undefined) bad(`${name}: missing attribute ${key} (contract says ${w})`);
      else if ((typeof w === "number" ? num(g) : g) !== w) bad(`${name}: ${key}="${g}" but the contract says ${w}`);
    }
  }
  for (const name of seen.keys()) if (!expected.has(name)) bad(`definition has ${name}, which no contract binding explains`);

  // --- mapper
  if (map.name !== "mapper") bad(`mapper root is <${map.name}>, expected <mapper>`);
  for (const a of Object.keys(map.attrs)) if (!MAPPER_ATTRS.has(a)) bad(`<mapper> attribute "${a}" is not in the official reference`);
  if (map.attrs.device !== def.attrs.name) bad(`<mapper device="${map.attrs.device}"> does not match definition name "${def.attrs.name}"`);
  if (Number.isNaN(num(map.attrs.version ?? ""))) bad(`<mapper version> "${map.attrs.version}" is not a number`);

  const mapped = new Map();
  for (const m of map.children) {
    if (m.name !== "map") { bad(`<${m.name}> in mapper (only <map> is used here)`); continue; }
    const keys = Object.keys(m.attrs).sort().join(",");
    if (keys !== "action,value") bad(`<map value="${m.attrs.value}"> has attributes {${keys}}, expected {action,value}`);
    if (mapped.has(m.attrs.value)) bad(`mapper maps ${m.attrs.value} twice`);
    mapped.set(m.attrs.value, m.attrs.action ?? "");
    if (!seen.has(m.attrs.value)) bad(`mapper refers to ${m.attrs.value}, which the definition does not define`);
    verbOf(m.attrs.action ?? "", verbs, bad, `map ${m.attrs.value}`);
  }
  for (const [name, want] of expected) {
    if (!want.action) continue; // LEDs follow their button
    if (!mapped.has(name)) bad(`mapper has no action for ${name}`);
    else if (mapped.get(name) !== want.action) bad(`${name}: action "${mapped.get(name)}", expected "${want.action}"`);
  }
  return problems;
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

const read = (p) => readFileSync(p, "utf8");
const real = {
  defXml: read(PATHS.def),
  mapXml: read(PATHS.map),
  contract: JSON.parse(read(PATHS.contract)),
  verbs: new Set(read(PATHS.verbs).split("\n").map((l) => l.trim()).filter((l) => l && !l.startsWith("#"))),
};
const withProblems = (over) => validate({ ...real, ...over });

// Replace exactly one occurrence, so a mutation can never silently do nothing.
function mutate(src, from, to) {
  const n = src.split(from).length - 1;
  assert.equal(n, 1, `mutation target must occur exactly once (found ${n}): ${from}`);
  return src.replace(from, to);
}

test("XML parser rejects malformed documents (the well-formedness check is real)", () => {
  const bad = [
    '<?xml version="1.0"?><a><b></a>',
    '<?xml version="1.0"?><a x="1" x="2"/>',
    '<?xml version="1.0"?><a>&bogus;</a>',
    '<?xml version="1.0"?><a x=1/>',
    '<?xml version="1.0"?><a/><b/>',
    '<?xml version="1.0"?><a><!-- a -- b --></a>',
    '<?xml version="1.0"?><a>',
    '<a/>',
  ];
  for (const b of bad) assert.throws(() => parseXml(b), /not well-formed/, b);
  assert.doesNotThrow(() => parseXml('<?xml version="1.0"?><a x="1"><b/><!-- ok --></a>'));
});

test("both XML files are well-formed", () => {
  parseXml(real.defXml);
  parseXml(real.mapXml);
});

test("definition and mapper agree with the contract, the official vocabulary and the verb list", () => {
  assert.deepEqual(withProblems({}), []);
});

test("contract coverage: every control is bound or deliberately unmapped", () => {
  const ids = real.contract.midi.controls.map((c) => c.id).sort();
  const classified = [...BIND.map((b) => b.id), ...Object.keys(UNMAPPED)].sort();
  assert.deepEqual(classified, ids);
});

test("verb snapshot is a real list and contains every verb the mapper uses", () => {
  assert.ok(real.verbs.size > 500, `only ${real.verbs.size} verbs`);
  for (const b of BIND) assert.ok(real.verbs.has(b.action), `${b.action} missing from vdj-verbs.txt`);
  assert.ok(!real.verbs.has("tochwheel"));
});

test("README documents the install paths, every mapped name and action, and every unmapped control", () => {
  const readme = read(PATHS.readme);
  const code = (s) => `\`${s}\``;
  const mustSay = [
    "Devices/EPDJ3000.xml", "Mappers/EP-DJ3000.xml", "UNTESTED",
    ...BIND.flatMap((b) => [code(b.name), code(b.action)]),
    ...Object.keys(UNMAPPED).map(code),
  ];
  const missing = mustSay.filter((s) => !readme.includes(s));
  assert.deepEqual(missing, []);
});

// Mutation tests: each broken copy must be reported, so a passing run above means something.
const D = real.defXml;
const M = real.mapXml;
const mutations = [
  ["definition: CUE note number changed", { defXml: mutate(D, '<button note="0x24" name="CUE" deck="1" channel="0" />', '<button note="0x23" name="CUE" deck="1" channel="0" />') }, /CUE: note="0x23" but the contract says 36/],
  ["definition: jog resolution changed", { defXml: mutate(D, '<jog cc="0x16" zero="0x40" full="1024" name="JOG" deck="1" channel="0" />', '<jog cc="0x16" zero="0x40" full="512" name="JOG" deck="1" channel="0" />') }, /JOG: full="512" but the contract says 1024/],
  ["definition: jog centre changed on the channel-2 twin", { defXml: mutate(D, '<jog cc="0x16" zero="0x40" full="1024" name="JOG_CH2" deck="1" channel="1" />', '<jog cc="0x16" zero="0x00" full="1024" name="JOG_CH2" deck="1" channel="1" />') }, /JOG_CH2: zero="0x00" but the contract says 64/],
  ["definition: channel-2 twin moved onto channel 0", { defXml: mutate(D, '<button note="0x25" name="REV_CH2" deck="1" channel="1" />', '<button note="0x25" name="REV_CH2" deck="1" channel="0" />') }, /REV_CH2: channel="0" but the contract says 1/],
  ["definition: PITCH LSB and MSB swapped", { defXml: mutate(D, '<slider cc="0x39" ccmsb="0x19" name="PITCH" deck="1" channel="0" />', '<slider cc="0x19" ccmsb="0x39" name="PITCH" deck="1" channel="0" />') }, /PITCH: cc="0x19" but the contract says 57/],
  ["definition: attribute not in the official reference", { defXml: mutate(D, '<slider cc="0x38" ccmsb="0x18" name="KNOB" deck="1" channel="0" />', '<slider cc="0x38" cclsb="0x18" name="KNOB" deck="1" channel="0" />') }, /attribute "cclsb" is not in the official reference/],
  ["definition: element removed", { defXml: mutate(D, '  <button note="0x25" name="REV_CH2" deck="1" channel="1" />\n', "") }, /definition is missing button REV_CH2/],
  ["definition: stray element", { defXml: mutate(D, "</device>", '  <button note="0x30" name="EXTRA" deck="1" channel="0" />\n</device>') }, /EXTRA, which no contract binding explains/],
  ["definition: LED follows the wrong button", { defXml: mutate(D, 'name="LED_PLAY" default="PLAY"', 'name="LED_PLAY" default="CUE"') }, /LED_PLAY: default="CUE"/],
  ["definition: USB product id changed", { defXml: mutate(D, '<device name="EPDJ3000" type="MIDI" description="EP-DJ3000 Deck" author="EP-DJ3000 project" version="800" decks="1" vid="0x1209" pid="0x0001">', '<device name="EPDJ3000" type="MIDI" description="EP-DJ3000 Deck" author="EP-DJ3000 project" version="800" decks="1" vid="0x1209" pid="0x0002">') }, /<device pid> 0x0002 != contract 0x0001/],
  ["definition: not well-formed", { defXml: mutate(D, "</device>", "</devize>") }, /not well-formed/],
  ["mapper: jog and touch actions swapped", { mapXml: mutate(mutate(M, '<map value="JOG" action="touchwheel" />', '<map value="JOG" action="touchwheel_touch" />'), '<map value="JOG_TOUCH" action="touchwheel_touch" />', '<map value="JOG_TOUCH" action="touchwheel" />') }, /JOG: action "touchwheel_touch", expected "touchwheel"/],
  ["mapper: verb typo", { mapXml: mutate(M, '<map value="JOG_CH2" action="touchwheel" />', '<map value="JOG_CH2" action="tochwheel" />') }, /"tochwheel" is not in the official VDJscript verb list/],
  ["mapper: row removed", { mapXml: mutate(M, '  <map value="KNOB_CH2" action="filter" />\n', "") }, /mapper has no action for KNOB_CH2/],
  ["mapper: maps a name the definition lacks", { mapXml: mutate(M, "</mapper>", '  <map value="SHIFT" action="shift" />\n</mapper>') }, /refers to SHIFT, which the definition does not define/],
  ["mapper: wrong device name", { mapXml: mutate(M, 'device="EPDJ3000"', 'device="EPDJ3001"') }, /does not match definition name "EPDJ3000"/],
  ["mapper: attribute outside value/action", { mapXml: mutate(M, '<map value="REV" action="reverse" />', '<map value="REV" action="reverse" shift="1" />') }, /has attributes \{action,shift,value\}/],
  ["contract: a new control nobody classified", {
    contract: { ...real.contract, midi: { ...real.contract.midi, controls: [...real.contract.midi.controls, { id: "newThing", type: "note", number: 50, dir: "out", desc: "x" }] } },
  }, /"newThing" is neither bound nor listed as unmapped/],
  ["contract: jog resolution changes", {
    contract: { ...real.contract, midi: { ...real.contract.midi, jogTicksPerRev: 2048 } },
  }, /JOG: full="1024" but the contract says 2048/],
];
for (const [name, over, expect] of mutations) {
  test(`validator catches: ${name}`, () => {
    const problems = withProblems(over);
    assert.ok(problems.some((p) => expect.test(p)), `expected a problem matching ${expect}; got:\n  ${problems.join("\n  ") || "(none)"}`);
  });
}

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
const elems = parseXml(real.defXml).children.length;
const maps = parseXml(real.mapXml).children.length;
console.log(`\n${tests.length - failed} passed, ${failed} failed (${tests.length} tests); checked ${elems} definition elements and ${maps} mappings against protocol/epdj3000.json`);
process.exit(failed ? 1 : 0);
