// Keeps the driver docs honest about what a factory-default unit sends. No dependencies:
//
//   node drivers/test/defaults/check.mjs           check; also proves the check can fail
//   node drivers/test/defaults/check.mjs --write   rewrite the generated table in drivers/README.md
//
// 1. The table between the GENERATED:defaults markers in drivers/README.md is rendered here from
//    protocol/epdj3000.json (config defaults, controls, default channels), and must equal a fresh render.
// 2. Every per-app README states the same default slide-pot role and its CC pair, so a change of
//    the contract default cannot leave one of them describing the old behaviour.
// 3. Every per-app README states the default `jogUntouched` policy (AUTO sends untouched motion until the hold pad has
//    registered a touch since power-up) and how to switch a platter that is not touched back on after that (REPORT,
//    with the config key and value), since all four mappings otherwise assume untouched motion arrives; and the djay
//    README points at the original-firmware reference document, which must exist.
//
// Does NOT prove the firmware really sends these; that is the firmware tests' job (they use the same contract).

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "../../..");
const CONTRACT = join(ROOT, "protocol/epdj3000.json");
const README = join(ROOT, "drivers/README.md");
const REFERENCE = join(ROOT, "docs/REFERENCE-ORIGINAL-FIRMWARE.md");
const APP_READMES = ["mixxx", "virtualdj", "djay", "rekordbox"].map((app) => join(ROOT, `drivers/${app}/README.md`));

export const BEGIN = "<!-- BEGIN GENERATED:defaults -->";
export const END = "<!-- END GENERATED:defaults -->";

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

const hex = (n) => "0x" + n.toString(16).toUpperCase().padStart(2, "0");
const camel = (snake) => snake.toLowerCase().replace(/_([a-z])/g, (_, ch) => ch.toUpperCase());

function lookup(contract) {
  const config = (name) => {
    const k = contract.config.keys.find((x) => x.name === name);
    if (!k) throw new Error(`contract has no config key ${name}`);
    return k;
  };
  const control = (id) => {
    const c = contract.midi.controls.find((x) => x.id === id);
    if (!c) throw new Error(`contract has no control ${id}`);
    return c;
  };
  const enumName = (enumType, value) => {
    const e = (contract.enums[enumType] ?? []).find(([, v]) => v === value);
    if (!e) throw new Error(`contract enum ${enumType} has no value ${value}`);
    return e[0];
  };
  const enumValue = (enumType, name) => {
    const e = (contract.enums[enumType] ?? []).find(([n]) => n === name);
    if (!e) throw new Error(`contract enum ${enumType} has no member ${name}`);
    return e[1];
  };
  // The text below describes one specific default. If the contract moves it, a person has to re-read
  // the row instead of the table quietly printing a sentence that no longer fits.
  const expectDefault = (key, name) => {
    const got = enumName(key.enum, key.default);
    if (got !== name) throw new Error(`${key.name} now defaults to ${got}; the defaults table text assumes ${name}: revise renderTable in drivers/test/defaults/check.mjs`);
  };
  return { config, control, enumName, enumValue, expectDefault };
}

/** The default slide-pot role: { role: "CHANNEL_VOLUME", msb: 7, lsb: 39 }. */
export function faderDefault(contract) {
  const { config, control, enumName } = lookup(contract);
  const role = enumName("FaderRole", config("faderRole").default);
  const c = control(camel(role));
  return { role, msb: c.number, lsb: c.lsb };
}

export function defaultFaderSentence(contract) {
  const f = faderDefault(contract);
  return `The unit's default \`faderRole\` is \`${f.role}\` (CC ${f.msb} / ${f.lsb})`;
}

/** The facts every per-app README must carry about untouched platter motion (contract v1.4). */
export function jogUntouchedSentences(contract) {
  const { config, enumValue, expectDefault } = lookup(contract);
  const key = config("jogUntouched");
  expectDefault(key, "AUTO");
  return [
    "The unit's default `jogUntouched` is `AUTO`",
    "until the hold pad has registered a touch since power-up",
    `set \`jogUntouched\` = \`REPORT\` (config key ${key.id}, value ${enumValue(key.enum, "REPORT")})`,
  ];
}

export function renderTable(contract) {
  const { config, control, enumName, enumValue, expectDefault } = lookup(contract);
  const midi = contract.midi;

  const channel = config("midiChannel");
  if (channel.default !== channel.max) {
    throw new Error(`midiChannel now defaults to ${channel.default}, not AUTO (${channel.max}); the defaults table text assumes AUTO`);
  }
  expectDefault(config("mode"), "DJ");
  expectDefault(config("knobRole"), "GENERIC");

  const decks = contract.enums.Deck.map(([name, value]) => {
    const ch = midi.defaultChannels[value];
    return `Deck ${name} on channel ${ch + 1} (status \`${hex(0x90 | ch)}\` / \`${hex(0xb0 | ch)}\`)`;
  });

  const fader = faderDefault(contract);
  const others = contract.enums.FaderRole.map(([name]) => name).filter((name) => name !== fader.role)
    .map((name) => {
      const c = control(camel(name));
      return `\`${name}\` CC ${c.number} / ${c.lsb}`;
    });
  const knob = control("knob");
  const jog = control("jog");
  const cc14 = (c) => `CC ${c.number} (MSB) / ${c.lsb} (LSB), 14-bit`;
  const note = (id) => `\`${id}\` Note ${control(id).number}`;
  const platter = enumName("PlatterSource", config("platterSource").default);
  const untouched = config("jogUntouched");
  expectDefault(untouched, "AUTO");
  const value = (name) => enumValue(untouched.enum, name);

  const rows = [
    ["MIDI channel", `\`midiChannel\` = AUTO: ${decks.join(", ")}`],
    ["Slide pot", `\`faderRole\` = \`${fader.role}\`: ${cc14(control(camel(fader.role)))}. Other roles: ${others.join(", ")}`],
    ["Orange knob", `\`knobRole\` = \`GENERIC\`: ${cc14(knob)}`],
    ["Platter", `\`platterSource\` = \`${platter}\`, \`jogInvert\` = ${config("jogInvert").default}: CC ${jog.number}, relative \`${midi.relativeCenter} + delta\`, ` +
      `**${midi.jogTicksPerRev} ticks per revolution**, clockwise positive; touch is Note ${control("touch").number} (a hand on the platter, from the hold pad when the platter is the AS5600)`],
    ["Untouched platter", `\`jogUntouched\` = \`AUTO\` (config key ${untouched.id}): motion with no hand on the platter is sent as jog until the hold pad has registered a touch since power-up, then **not** sent ` +
      `(a free-spinning platter coasts for seconds after release, and the unit cannot tell a dead pad from a working one, so it waits for the pad to report one hand; ` +
      `the proof is not stored, it restarts at reboot and when \`platterSource\` changes); always sent when the build has no hold pad. ` +
      `\`REPORT\` (${value("REPORT")}) always sends it, ` +
      `\`SUPPRESS\` (${value("SUPPRESS")}) never does; \`Telemetry.jogTotal\` counts it either way`],
    ["Buttons", `${note("cue")}, ${note("rev")}; a platter tap (\`tapPlay\` = ${config("tapPlay").default}) pulses ${note("play")}`],
    ["Mode", `\`mode\` = \`DJ\`: no clock, transport or timecode out, so \`bpm\`, \`bpmNudge\`, \`tap\` and \`transport\` stay silent`],
  ];
  return ["| What | Factory default |", "|---|---|", ...rows.map(([what, text]) => `| ${what} | ${text} |`)].join("\n");
}

// ---------------------------------------------------------------------------
// Checks. Each returns a list of problems; empty = fine.
// ---------------------------------------------------------------------------

function blockOf(readme) {
  const starts = readme.split(BEGIN).length - 1;
  const ends = readme.split(END).length - 1;
  if (starts !== 1 || ends !== 1) return { error: `drivers/README.md needs exactly one ${BEGIN} and one ${END} (found ${starts} and ${ends})` };
  const from = readme.indexOf(BEGIN) + BEGIN.length;
  const to = readme.indexOf(END);
  if (to < from) return { error: "the END marker comes before the BEGIN marker" };
  return { from, to, text: readme.slice(from, to) };
}

export function checkTable(readme, contract) {
  let want;
  try { want = renderTable(contract); } catch (e) { return [e.message]; }
  const block = blockOf(readme);
  if (block.error) return [block.error];
  const have = block.text.trim();
  if (have === want) return [];
  const a = have.split("\n");
  const b = want.split("\n");
  const i = a.findIndex((line, k) => line !== b[k]);
  const at = i < 0 ? Math.min(a.length, b.length) : i;
  return [`the defaults table in drivers/README.md is stale (first difference at table line ${at + 1}: README has "${a[at] ?? "(nothing)"}", contract gives "${b[at] ?? "(nothing)"}"); run: node drivers/test/defaults/check.mjs --write`];
}

export function checkAppReadme(md, contract, who) {
  let want;
  try { want = defaultFaderSentence(contract); } catch (e) { return [e.message]; }
  return md.replace(/\s+/g, " ").includes(want) ? [] : [`${who} does not say: ${want}`];
}

export function checkJogUntouchedReadme(md, contract, who) {
  let want;
  try { want = jogUntouchedSentences(contract); } catch (e) { return [e.message]; }
  const flat = md.replace(/\s+/g, " ");
  return want.filter((sentence) => !flat.includes(sentence)).map((sentence) => `${who} does not say: ${sentence}`);
}

/** The djay README must link the reference document, and the document must exist. */
export function checkReferencePointer(md, who, referenceExists) {
  const problems = [];
  if (!md.includes("REFERENCE-ORIGINAL-FIRMWARE.md")) problems.push(`${who} does not point at docs/REFERENCE-ORIGINAL-FIRMWARE.md`);
  if (!referenceExists) problems.push("docs/REFERENCE-ORIGINAL-FIRMWARE.md does not exist");
  return problems;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

function run() {
  const contract = JSON.parse(readFileSync(CONTRACT, "utf8"));
  const readme = readFileSync(README, "utf8");

  if (process.argv.includes("--write")) {
    const block = blockOf(readme);
    if (block.error) { console.error(block.error); process.exit(1); }
    writeFileSync(README, `${readme.slice(0, block.from)}\n${renderTable(contract)}\n${readme.slice(block.to)}`);
    console.log("rewrote the defaults table in drivers/README.md");
    return;
  }

  let failed = 0;
  const report = (name, problems) => {
    if (problems.length === 0) { console.log(`PASS  ${name}`); return; }
    failed++;
    console.log(`FAIL  ${name}`);
    for (const p of problems) console.log(`        ${p}`);
  };

  report("drivers/README.md defaults table equals a render of the contract", checkTable(readme, contract));
  for (const path of APP_READMES) {
    const who = path.slice(ROOT.length + 1);
    report(`${who} states the default fader role`, checkAppReadme(readFileSync(path, "utf8"), contract, who));
    report(`${who} states the jogUntouched default and how to get untouched jog`, checkJogUntouchedReadme(readFileSync(path, "utf8"), contract, who));
  }
  report("drivers/djay/README.md points at the original-firmware reference, which exists",
    checkReferencePointer(readFileSync(join(ROOT, "drivers/djay/README.md"), "utf8"), "drivers/djay/README.md", existsSync(REFERENCE)));

  // The checks above are only worth something if they fail on drift.
  const withConfig = (name, patch) => ({
    ...contract,
    config: { ...contract.config, keys: contract.config.keys.map((k) => (k.name === name ? { ...k, ...patch } : k)) },
  });
  const withMidi = (patch) => ({ ...contract, midi: { ...contract.midi, ...patch } });
  const swap = (text, from, to) => {
    if (!text.includes(from)) throw new Error(`mutation target not found: ${from}`);
    return text.replace(from, to);
  };
  const faderNow = faderDefault(contract);
  const otherRole = contract.enums.FaderRole.find(([n]) => n !== faderNow.role);

  const negatives = [
    ["contract default faderRole changes", checkTable(readme, withConfig("faderRole", { default: otherRole[1] })), /stale/],
    ["contract jog resolution changes", checkTable(readme, withMidi({ jogTicksPerRev: 2048 })), /stale/],
    ["contract default channels change", checkTable(readme, withMidi({ defaultChannels: [4, 5] })), /stale/],
    ["contract midiChannel default is no longer AUTO", checkTable(readme, withConfig("midiChannel", { default: 3 })), /not AUTO/],
    ["contract mode default is no longer DJ", checkTable(readme, withConfig("mode", { default: 1 })), /now defaults to SYNC_MASTER/],
    ["README cell edited by hand", checkTable(swap(readme, `CC ${faderNow.msb} (MSB)`, `CC ${faderNow.msb + 1} (MSB)`), contract), /stale/],
    ["README table row deleted", checkTable(swap(readme, "| Orange knob |", "| |"), contract), /stale/],
    ["README markers gone", checkTable(swap(readme, BEGIN, ""), contract), /exactly one/],
    ["app README names another default role", checkAppReadme(`The unit's default \`faderRole\` is \`${otherRole[0]}\` (CC 1 / 33)`, contract, "x"), /does not say/],
    ["app README says nothing about the default", checkAppReadme("faderRole can be set.", contract, "x"), /does not say/],
    ["contract jogUntouched default is no longer AUTO", checkTable(readme, withConfig("jogUntouched", { default: 1 })), /now defaults to REPORT/],
    ["contract moves the jogUntouched config key", checkTable(readme, withConfig("jogUntouched", { id: 40 })), /stale/],
    ["contract renumbers the JogUntouched enum", checkTable(readme, { ...contract, enums: { ...contract.enums, JogUntouched: [["AUTO", 0], ["REPORT", 2], ["SUPPRESS", 1]] } }), /stale/],
    ["README untouched row deleted", checkTable(swap(readme, "| Untouched platter |", "| |"), contract), /stale/],
    ["README row says untouched jog is still sent after the pad has worked", checkTable(swap(readme, ", then **not** sent", ", then sent"), contract), /stale/],
    ["README row goes back to the old rule (suppressed whenever a pad is wired)", checkTable(swap(readme, "is sent as jog until the hold pad has registered a touch since power-up, then **not** sent", "is **not** sent as jog when a hold pad is wired"), contract), /stale/],
    ["README row drops the reboot caveat", checkTable(swap(readme, "the proof is not stored, it restarts at reboot", "the proof is stored"), contract), /stale/],
    ["app README forgets the jogUntouched default", checkJogUntouchedReadme("Set faderRole as you like.", contract, "x"), /default `jogUntouched` is `AUTO`/],
    ["app README still describes the old rule (no jog once a pad is wired)", checkJogUntouchedReadme("The unit's default `jogUntouched` is `AUTO`: with a hold pad wired it sends jog only under a hand. To nudge, set `jogUntouched` = `REPORT` (config key 33, value 1).", contract, "x"), /registered a touch/],
    ["app README names the wrong value for REPORT", checkJogUntouchedReadme("The unit's default `jogUntouched` is `AUTO`, which sends jog until the hold pad has registered a touch since power-up. To nudge, set `jogUntouched` = `REPORT` (config key 33, value 2).", contract, "x"), /value 1/],
    ["app README gives no way to get untouched jog", checkJogUntouchedReadme("The unit's default `jogUntouched` is `AUTO`, which sends jog until the hold pad has registered a touch since power-up.", contract, "x"), /REPORT/],
    ["djay README without the reference pointer", checkReferencePointer("no link here", "x", true), /does not point/],
    ["reference document missing", checkReferencePointer("see docs/REFERENCE-ORIGINAL-FIRMWARE.md", "x", false), /does not exist/],
  ];
  for (const [name, problems, expect] of negatives) {
    report(`negative: ${name} is rejected`, problems.some((p) => expect.test(p)) ? [] : [`expected ${expect}, got: ${problems.join(" | ") || "no problems"}`]);
  }

  console.log(failed === 0 ? "\nall checks passed" : `\n${failed} check(s) FAILED`);
  process.exit(failed === 0 ? 0 : 1);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) run();
