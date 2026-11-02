// djay Pro + rekordbox driver checks. No dependencies:  node drivers/test/djay-rekordbox/check.mjs
//
// Proves, against protocol/epdj3000.json:
//   * the two rekordbox CSVs are structurally what the community-documented layout says (15 fields on
//     every line, '@file,1,<name>' then the header row, '#' comment/disabled rows, ASCII, LF, no quotes);
//   * every row's status byte, data byte, channel, deck column, type and LED output agree with the
//     contract, decoded here independently of drivers/rekordbox/generate_csv.py;
//   * unverified and jog rows stay disabled, no two active rows share a MIDI code (including the
//     implied LSB of each 14-bit pair), every contract control is mapped or listed as unmapped;
//   * both READMEs' tables carry the contract's note/CC numbers and codes;
//   * the committed CSVs equal a fresh render (runs generate_csv.py --check when python3 exists);
//   * deliberately broken copies of the CSV and READMEs fail, each for the intended reason.
//
// Does NOT prove anything about how rekordbox or djay Pro behave: neither was available, and
// AlphaTheta documents no CSV layout. See drivers/rekordbox/README.md and drivers/djay/README.md.
// Layout sources: https://github.com/icherniukh/rb-midi-sniffer/blob/main/REKORDBOX-MIDI-CSV-SPEC.md
// and https://djtechtools.com/2019/06/24/hacking-rekordbox-fx-and-adding-rmx-1000-control/
// djay cheat-sheet sources: https://help.algoriddim.com/user-manual/djay-pro-mac/midi/mapping

import { readFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const ROOT = join(here, "../../..");
const RB = join(ROOT, "drivers/rekordbox");
const DJAY = join(ROOT, "drivers/djay");
const contract = JSON.parse(readFileSync(join(ROOT, "protocol/epdj3000.json"), "utf8"));

// ---------------------------------------------------------------------------
// What the files are supposed to contain, expressed against the contract
// ---------------------------------------------------------------------------

// Contract control -> rekordbox command. live=false rows must be disabled with a leading '#'.
// flag: text the row's comment must keep so nobody quietly drops the warning.
const BIND = [
  { id: "play", cmd: "PlayPause", type: "Button", live: true, scope: "deck", led: true },
  { id: "cue", cmd: "Cue", type: "Button", live: true, scope: "deck", led: true },
  { id: "rev", cmd: "Reverse", type: "Button", live: false, scope: "deck", flag: "UNVERIFIED" },
  { id: "pitch", cmd: "TempoSlider", type: "KnobSliderHiRes", live: true, scope: "deck" },
  { id: "crossfader", cmd: "CrossFader", type: "KnobSliderHiRes", live: true, scope: "global" },
  { id: "channelVolume", cmd: "ChannelFader", type: "KnobSliderHiRes", live: false, scope: "deck", flag: "UNVERIFIED" },
  { id: "knob", cmd: "Gain", type: "KnobSliderHiRes", live: true, scope: "deck" },
  { id: "touch", cmd: "JogTouch", type: "JogTouch", live: false, scope: "deck", opt: "RO" },
  { id: "jog", cmd: "JogScratch", type: "JogRotate", live: false, scope: "deck", opt: "RO" },
];

// Contract controls with no binding in either app, and why. A new contract control must land in
// BIND or here, or this check fails.
const UNMAPPED = {
  bpm: "only sent in SYNC_MASTER / knobRole=BPM; not a DJ-software mode",
  bpmNudge: "only sent in SYNC_MASTER",
  tap: "only sent in SYNC_MASTER",
  transport: "only sent in the unit's sync modes",
};

const HEADER = "#name,function,type,input,deck1,deck2,deck3,deck4,output,deck1,deck2,deck3,deck4,option,comment";
const FIELDS = 15;
const OPTION_TOKEN = /^(Fast|RO|Priority=\d+|Blink=\d+)$/;
const CSV_KIND = { Button: "note", JogTouch: "note", JogRotate: "ccRel", KnobSliderHiRes: "cc14" };

// ---------------------------------------------------------------------------
// Contract helpers
// ---------------------------------------------------------------------------

const ctl = (id) => contract.midi.controls.find((c) => c.id === id);
const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");
const unitName = (u) => `${contract.device.name} ${contract.device.deckNames[u]}`;
const channelOf = (u) => contract.midi.defaultChannels[u];
const statusHi = (c) => (c.type === "note" ? 0x9 : 0xb);

/** Expected 4-hex-digit code for control `c` on channel `ch`. */
const code = (c, ch) => `${hex2((statusHi(c) << 4) | ch)}${hex2(c.number)}`;

// ---------------------------------------------------------------------------
// rekordbox CSV validator. Returns a list of problems; empty = pass.
// ---------------------------------------------------------------------------

function validateCsv(text, unit) {
  const bad = [];
  const ch = channelOf(unit);

  if (text.charCodeAt(0) === 0xfeff) bad.push("starts with a BOM");
  if (text.includes("\r")) bad.push("contains CR");
  if (!text.endsWith("\n")) bad.push("does not end with LF");
  if (/[^\x20-\x7e\n]/.test(text)) bad.push("contains non-ASCII or control characters");
  if (text.includes('"')) bad.push("contains a double quote (rekordbox files never quote)");

  const lines = text.split("\n");
  lines.pop();
  const rows = lines.map((l) => l.split(","));
  rows.forEach((r, i) => {
    if (r.length !== FIELDS) bad.push(`line ${i + 1}: ${r.length} fields, want ${FIELDS}`);
  });
  if (bad.length) return bad;

  const f0 = rows[0];
  if (f0[0] !== "@file" || f0[1] !== "1" || f0[2] !== unitName(unit) || f0.slice(3).some((x) => x !== "")) {
    bad.push(`line 1 must be @file,1,${unitName(unit)} padded to ${FIELDS} fields, got ${lines[0]}`);
  }
  if (lines[1] !== HEADER) bad.push(`line 2 is not the header row: ${lines[1]}`);

  const seen = new Set();
  const activeCodes = new Map(); // "STATUS DATA" -> cmd, includes implied 14-bit LSBs
  let sawExperimental = false;
  let sawUntested = false;

  for (let i = 2; i < rows.length; i++) {
    const r = rows[i];
    const ln = i + 1;
    if (r.every((x) => x === "")) continue; // separator
    if (r[0].startsWith("#") && r.slice(1).every((x) => x === "")) {
      if (/EXPERIMENTAL/.test(r[0])) sawExperimental = true;
      if (/UNTESTED/.test(r[0])) sawUntested = true;
      continue; // comment / section title
    }

    const disabled = r[0].startsWith("#");
    const cmd = disabled ? r[0].slice(1) : r[0];
    const b = BIND.find((x) => x.cmd === cmd);
    if (!b) {
      bad.push(`line ${ln}: command ${cmd} is not in the expected set`);
      continue;
    }
    if (seen.has(cmd)) bad.push(`line ${ln}: ${cmd} appears twice`);
    seen.add(cmd);

    const [, fn, type, input, ...rest] = r;
    const inDecks = rest.slice(0, 4);
    const output = rest[4];
    const outDecks = rest.slice(5, 9);
    const option = rest[9];
    const comment = rest[10];

    if (fn !== cmd) bad.push(`line ${ln}: function column ${fn} != command ${cmd}`);
    if (type !== b.type) bad.push(`line ${ln}: ${cmd} type ${type}, want ${b.type}`);
    if (disabled === b.live) bad.push(`line ${ln}: ${cmd} must be ${b.live ? "active" : "disabled with a leading #"}`);
    if (comment === "") bad.push(`line ${ln}: ${cmd} has no comment`);
    if (b.flag && !comment.includes(b.flag)) bad.push(`line ${ln}: ${cmd} comment lost its ${b.flag} marker`);

    for (const o of option === "" ? [] : option.split(";")) {
      if (!OPTION_TOKEN.test(o)) bad.push(`line ${ln}: unknown option token ${o}`);
    }
    if (b.opt && option !== b.opt) bad.push(`line ${ln}: ${cmd} option ${option}, want ${b.opt}`);

    const c = ctl(b.id);
    if (!c) { bad.push(`line ${ln}: contract has no control ${b.id}`); continue; }
    if (c.type !== CSV_KIND[b.type]) bad.push(`line ${ln}: ${cmd} is csv type ${b.type} but contract ${b.id} is ${c.type}`);
    if (c.dir !== "out" && c.dir !== "both") bad.push(`line ${ln}: contract ${b.id} is not device -> host`);
    if (c.type === "cc14" && c.lsb !== c.number + 32) bad.push(`line ${ln}: ${b.id} LSB ${c.lsb} != MSB ${c.number} + 32`);

    // Decode the address. Deck rows: base code on channel 0 + channel offset in exactly this unit's deck
    // column. Global rows: channel baked into the code, no deck columns.
    const m = /^([0-9A-F]{2})([0-9A-F]{2})$/.exec(input);
    if (!m) { bad.push(`line ${ln}: input ${input} is not 4 upper-case hex digits`); continue; }
    const st = parseInt(m[1], 16);
    const data = parseInt(m[2], 16);
    if (st >> 4 !== statusHi(c)) bad.push(`line ${ln}: ${cmd} status ${m[1]}, want a ${hex2(statusHi(c) << 4)} message`);
    if (data !== c.number) bad.push(`line ${ln}: ${cmd} data byte ${data}, contract ${b.id} is ${c.number}`);

    let effective;
    if (b.scope === "deck") {
      if (st & 0xf) bad.push(`line ${ln}: deck row ${cmd} base channel nibble must be 0`);
      effective = (st & 0xf) + Number(inDecks[unit] === "" ? NaN : inDecks[unit]);
      inDecks.forEach((x, d) => {
        if (d === unit ? x !== String(ch) : x !== "") bad.push(`line ${ln}: ${cmd} deck${d + 1} column "${x}"`);
      });
    } else {
      effective = st & 0xf;
      if (inDecks.some((x) => x !== "")) bad.push(`line ${ln}: global row ${cmd} must have empty deck columns`);
    }
    if (effective !== ch) bad.push(`line ${ln}: ${cmd} resolves to channel ${effective}, contract default is ${ch}`);

    if (b.led) {
      if (output !== input || outDecks.join() !== inDecks.join()) bad.push(`line ${ln}: ${cmd} LED output must mirror input`);
    } else if (output !== "" || outDecks.some((x) => x !== "")) {
      bad.push(`line ${ln}: ${cmd} must have no output`);
    }

    if (!disabled) {
      const claim = (d2) => {
        const k = `${hex2((st & 0xf0) | effective)} ${d2}`;
        if (activeCodes.has(k)) bad.push(`line ${ln}: ${cmd} collides with ${activeCodes.get(k)} on status/data ${k}`);
        activeCodes.set(k, cmd);
      };
      claim(data);
      if (c.type === "cc14") claim(c.lsb);
    }
  }

  for (const b of BIND) if (!seen.has(b.cmd)) bad.push(`missing row for ${b.cmd}`);
  if (!sawExperimental) bad.push("lost the EXPERIMENTAL section marker");
  if (!sawUntested) bad.push("lost the UNTESTED banner");
  return bad;
}

// ---------------------------------------------------------------------------
// README table parsing
// ---------------------------------------------------------------------------

/** Markdown table rows (cells) under `## heading`, up to the next '## '. */
function sectionRows(md, heading) {
  const start = md.search(new RegExp(`^## ${heading}`, "m"));
  if (start < 0) return null;
  const rest = md.slice(start).split("\n").slice(1);
  const end = rest.findIndex((l) => l.startsWith("## "));
  return (end < 0 ? rest : rest.slice(0, end))
    .filter((l) => l.startsWith("|") && !/^\|[-| ]+\|$/.test(l))
    .map((l) => l.replace(/^\||\|$/g, "").split("|").map((c) => c.trim()));
}

const ticks = (cell) => [...cell.matchAll(/`([^`]+)`/g)].map((m) => m[1]);

/** Rows whose first cell is `<contract id>`, keyed by id. */
function byId(rows) {
  const out = new Map();
  for (const r of rows ?? []) {
    const m = /^`([A-Za-z]+)`$/.exec(r[0]);
    if (m) out.set(m[1], r);
  }
  return out;
}

function checkNotMapped(md, bad, who) {
  const m = /^Not mapped: (.+?)\./m.exec(md);
  if (!m) { bad.push(`${who}: no "Not mapped:" sentence`); return; }
  const got = new Set(ticks(m[1]));
  const want = new Set(Object.keys(UNMAPPED));
  for (const k of want) if (!got.has(k)) bad.push(`${who}: "Not mapped" omits ${k}`);
  for (const k of got) if (!want.has(k)) bad.push(`${who}: "Not mapped" lists ${k}, which the check treats as mapped`);
}

function checkCommon(md, bad, who) {
  for (let u = 0; u < 2; u++) {
    if (!md.includes(unitName(u))) bad.push(`${who}: never names the USB product "${unitName(u)}"`);
    if (!new RegExp(`channel ${channelOf(u) + 1}\\b`, "i").test(md)) bad.push(`${who}: never says channel ${channelOf(u) + 1}`);
  }
  // The README says how to reach every faderRole: the default needs nothing, the others are one SysEx frame away.
  const role = contract.config.keys.find((k) => k.name === "faderRole");
  const flat = md.replace(/\s+/g, " ");
  for (const [name, value] of contract.enums.FaderRole) {
    const m = new RegExp(`\`${name}\` \\(config key (\\d+), value (\\d+)\\)`).exec(flat);
    if (!m) bad.push(`${who}: faderRole advice for ${name} missing`);
    else if (+m[1] !== role.id || +m[2] !== value) bad.push(`${who}: faderRole ${name} key/value ${m[1]}/${m[2]}, contract ${role.id}/${value}`);
  }
  if (!new RegExp(`\`${contract.midi.relativeCenter} \\+ delta\``).test(md)) {
    bad.push(`${who}: does not state the relative encoding \`${contract.midi.relativeCenter} + delta\``);
  }
  checkNotMapped(md, bad, who);
  for (const c of contract.midi.controls) {
    if (!BIND.some((b) => b.id === c.id) && !(c.id in UNMAPPED)) {
      bad.push(`${who}: contract control ${c.id} (${c.type}) is neither bound nor listed as unmapped`);
    }
  }
}

function validateRekordboxReadme(md) {
  const bad = [];
  if (!md.includes("UNTESTED in rekordbox")) bad.push("rekordbox README lost its UNTESTED banner");
  if (!md.includes("community-documented, not official")) bad.push("rekordbox README lost its format caveat");
  checkCommon(md, bad, "rekordbox README");
  if (!new RegExp(`${contract.midi.jogTicksPerRev} ticks per\\s+revolution`).test(md)) bad.push("rekordbox README: jog resolution not stated");

  const mapped = byId(sectionRows(md, "What is mapped"));
  const learn = byId(sectionRows(md, "Manual MIDI Learn cheat sheet"));
  for (const b of BIND) {
    const c = ctl(b.id);
    const m = mapped.get(b.id);
    if (!m) { bad.push(`rekordbox README: "What is mapped" has no row for ${b.id}`); continue; }
    const kind = c.type === "note" ? "Note" : "CC";
    const mm = /^(Note|CC) (\d+)(?: \/ (\d+))?/.exec(m[1]);
    if (!mm || mm[1] !== kind || +mm[2] !== c.number || (c.type === "cc14" && +mm[3] !== c.lsb)) {
      bad.push(`rekordbox README: ${b.id} row says "${m[1]}", contract is ${kind} ${c.number}${c.type === "cc14" ? ` / ${c.lsb}` : ""}`);
    }
    if (!ticks(m[2]).some((t) => t === b.cmd)) bad.push(`rekordbox README: ${b.id} row does not name command ${b.cmd}`);
    if (b.live === /disabled/.test(m[3])) bad.push(`rekordbox README: ${b.id} state "${m[3]}" disagrees with the CSV`);

    const l = learn.get(b.id);
    if (!l) { bad.push(`rekordbox README: cheat sheet has no row for ${b.id}`); continue; }
    const want = [0, 1].map((u) => code(c, channelOf(u)));
    const got = ticks(l[l.length - 1]);
    if (got.join() !== want.join()) bad.push(`rekordbox README: ${b.id} cheat-sheet codes ${got.join("/")} != ${want.join("/")}`);
  }
  return bad;
}

function validateDjayReadme(md) {
  const bad = [];
  if (!md.includes("UNVERIFIED / manual")) bad.push("djay README lost its UNVERIFIED / manual banner");
  checkCommon(md, bad, "djay README");
  const rows = byId(sectionRows(md, "Manual MIDI Learn cheat sheet"));
  for (const b of BIND) {
    const c = ctl(b.id);
    const r = rows.get(b.id);
    if (!r) { bad.push(`djay README: cheat sheet has no row for ${b.id}`); continue; }
    const t = ticks(r[2]);
    const kind = c.type === "note" ? "Note" : "CC";
    if (t[0] !== `${kind} ${c.number}`) bad.push(`djay README: ${b.id} learns "${t[0]}", contract is ${kind} ${c.number}`);
    if (c.type === "cc14" && t[1] !== `CC ${c.lsb}`) bad.push(`djay README: ${b.id} should tell the user to ignore CC ${c.lsb} (LSB), got "${t[1]}"`);
    if (c.type !== "cc14" && t.length !== 1) bad.push(`djay README: ${b.id} lists extra numbers: ${t.join(", ")}`);
  }
  return bad;
}

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

let failed = 0;
function report(name, problems) {
  if (problems.length === 0) console.log(`PASS  ${name}`);
  else {
    failed++;
    console.log(`FAIL  ${name}`);
    for (const p of problems) console.log(`        ${p}`);
  }
}

const csvPath = (u) => join(RB, `${contract.device.name}-${contract.device.deckNames[u].replace(/ /g, "-")}.midi.csv`);
const csv = [0, 1].map((u) => readFileSync(csvPath(u), "utf8"));
const rbReadme = readFileSync(join(RB, "README.md"), "utf8");
const djayReadme = readFileSync(join(DJAY, "README.md"), "utf8");

for (let u = 0; u < 2; u++) report(`rekordbox CSV, ${unitName(u)}`, validateCsv(csv[u], u));

{
  const names = (t) => JSON.stringify(t.split("\n").slice(1).map((l) => l.split(",")[0]));
  report("rekordbox CSVs: same rows in the same order", names(csv[0]) === names(csv[1]) ? [] : ["Deck A and Deck B differ in row names or order"]);
}

report("rekordbox README vs contract", validateRekordboxReadme(rbReadme));
report("djay README vs contract", validateDjayReadme(djayReadme));

{
  const r = spawnSync("python3", [join(RB, "generate_csv.py"), "--check"], { encoding: "utf8" });
  const name = "committed CSVs equal a fresh render (generate_csv.py --check)";
  if (r.error && r.error.code === "ENOENT") console.log(`SKIP  ${name}: python3 not found, staleness NOT checked`);
  else report(name, r.status === 0 ? [] : [`exit ${r.status}: ${(r.stdout + r.stderr).trim()}`]);
}

// ---------------------------------------------------------------------------
// Negative tests: each broken copy must be rejected by the validator above, for the intended reason.
// ---------------------------------------------------------------------------

const csvMutants = [
  ["crossfader CC off by one", (t) => t.replace("B017", "B018"), /CrossFader data byte 24/],
  ["PlayPause note off by one", (t) => t.replace("9027", "9028"), /PlayPause data byte 40/],
  ["wrong channel on the global row", (t) => t.replace("B017", "B117"), /CrossFader resolves to channel 1/],
  ["PlayPause in the wrong deck column", (t) => t.replace("PlayPause,PlayPause,Button,9027,0,,,", "PlayPause,PlayPause,Button,9027,,0,,"), /PlayPause deck1 column/],
  ["unverified Reverse row enabled", (t) => t.replace("#Reverse", "Reverse"), /Reverse must be disabled/],
  ["jog row enabled", (t) => t.replace("#JogScratch", "JogScratch"), /JogScratch must be disabled/],
  ["live Cue row disabled", (t) => t.replace("\nCue,Cue", "\n#Cue,Cue"), /Cue must be active/],
  ["dropped comma", (t) => t.replace("Cue,Cue,Button,", "Cue,CueButton,"), /fields, want 15/],
  ["wrong header row", (t) => t.replace("#name,function", "#name,command"), /not the header row/],
  ["wrong @file name", (t) => t.replace("@file,1,EP-DJ3000 Deck A", "@file,1,EP-DJ3000"), /line 1 must be/],
  ["Gain on TempoSlider's code", (t) => t.replace("Gain,Gain,KnobSliderHiRes,B018", "Gain,Gain,KnobSliderHiRes,B019"), /Gain collides with TempoSlider/],
  ["Gain on the crossfader's implied LSB", (t) => t.replace("Gain,Gain,KnobSliderHiRes,B018", "Gain,Gain,KnobSliderHiRes,B037"), /Gain collides with CrossFader/],
  ["LED output removed from PlayPause", (t) => t.replace(",9027,0,,,,Fast", ",,,,,,Fast"), /PlayPause LED output must mirror input/],
  ["wrong CSV type for the fader", (t) => t.replace("TempoSlider,TempoSlider,KnobSliderHiRes", "TempoSlider,TempoSlider,KnobSlider"), /TempoSlider type KnobSlider, want KnobSliderHiRes/],
  ["UNVERIFIED marker removed", (t) => t.replace("UNVERIFIED command name - confirm", "command name - confirm"), /Reverse comment lost its UNVERIFIED marker/],
  ["BOM added", (t) => "﻿" + t, /starts with a BOM/],
  ["quote added", (t) => t.replace("Cue - LED", 'Cue "LED'), /double quote/],
  ["CRLF line endings", (t) => t.replace(/\n/g, "\r\n"), /contains CR/],
  ["unknown command", (t) => t.replace("Gain,Gain", "GainKnob,GainKnob"), /not in the expected set/],
  ["missing row", (t) => t.replace(/^#JogTouch.*\n/m, ""), /missing row for JogTouch/],
  ["lost UNTESTED banner", (t) => t.replace("UNTESTED", "TESTED"), /lost the UNTESTED banner/],
];
for (const [name, mutate, expect] of csvMutants) {
  const after = mutate(csv[0]);
  const problems = after === csv[0] ? ["mutation did not change the file (the test itself is broken)"] : validateCsv(after, 0);
  report(`negative: CSV with ${name} is rejected`, problems.some((p) => expect.test(p)) ? [] : [`expected ${expect}, got: ${problems.join(" | ") || "no problems"}`]);
}

const readmeMutants = [
  ["rekordbox README: wrong CC in the mapped table", validateRekordboxReadme, rbReadme, "| `jog` | CC 22 relative |", "| `jog` | CC 23 relative |", /jog row says/],
  ["rekordbox README: wrong cheat-sheet code", validateRekordboxReadme, rbReadme, "`B017` / `B117` |\n| `channelVolume`", "`B018` / `B117` |\n| `channelVolume`", /crossfader cheat-sheet codes/],
  ["rekordbox README: UNTESTED banner removed", validateRekordboxReadme, rbReadme, "UNTESTED in rekordbox", "Tested in rekordbox", /lost its UNTESTED banner/],
  ["rekordbox README: state flipped", validateRekordboxReadme, rbReadme, "| `rev` | Note 37 | `Reverse` / Button | disabled, UNVERIFIED name", "| `rev` | Note 37 | `Reverse` / Button | active", /rev state .* disagrees/],
  ["rekordbox README: wrong fader key", validateRekordboxReadme, rbReadme, "config key 11, value 2", "config key 12, value 2", /faderRole PITCH key\/value/],
  ["rekordbox README: wrong CHANNEL_VOLUME value", validateRekordboxReadme, rbReadme, "`CHANNEL_VOLUME` (config key 11, value 1)", "`CHANNEL_VOLUME` (config key 11, value 3)", /faderRole CHANNEL_VOLUME key\/value/],
  ["rekordbox README: no advice for CROSSFADER", validateRekordboxReadme, rbReadme, "`CROSSFADER` (config key 11, value 0)", "`CROSSFADER`", /advice for CROSSFADER missing/],
  ["djay README: wrong fader key", validateDjayReadme, djayReadme, "config key 11, value 2", "config key 12, value 2", /faderRole PITCH key\/value/],
  ["djay README: wrong CROSSFADER value", validateDjayReadme, djayReadme, "`CROSSFADER`\n   (config key 11, value 0)", "`CROSSFADER`\n   (config key 11, value 1)", /faderRole CROSSFADER key\/value/],
  ["djay README: wrong note number", validateDjayReadme, djayReadme, "| `Note 39` |", "| `Note 40` |", /play learns "Note 40"/],
  ["djay README: wrong LSB", validateDjayReadme, djayReadme, "ignore `CC 57`", "ignore `CC 58`", /pitch should tell the user to ignore CC 57/],
  ["djay README: banner removed", validateDjayReadme, djayReadme, "UNVERIFIED / manual", "verified", /lost its UNVERIFIED/],
  ["djay README: Not mapped drops a control", validateDjayReadme, djayReadme, "`bpm`, `bpmNudge`, `tap`, `transport`", "`bpm`, `bpmNudge`, `tap`", /omits transport/],
  ["djay README: row removed", validateDjayReadme, djayReadme, /^\| `knob` \|.*\n/m, "", /no row for knob/],
];
for (const [name, fn, md, from, to, expect] of readmeMutants) {
  const after = md.replace(from, to);
  const problems = after === md ? ["mutation did not change the file (the test itself is broken)"] : fn(after);
  report(`negative: ${name} is rejected`, problems.some((p) => expect.test(p)) ? [] : [`expected ${expect}, got: ${problems.join(" | ") || "no problems"}`]);
}

console.log(failed === 0 ? "\nall checks passed" : `\n${failed} check(s) FAILED`);
process.exit(failed === 0 ? 0 : 1);
