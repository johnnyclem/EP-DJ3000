#!/usr/bin/env node
// Runs every driver test; exits 1 if any step fails.
//
//   node drivers/test/run-all.mjs            one line per step, full output only for failures
//   node drivers/test/run-all.mjs --verbose  print every step's output
//
// Needs node and python3 (override with PYTHON=...). xmllint is optional: without it that step is
// reported as SKIPPED, never as passed. Steps run from the repo root whatever the caller's cwd.

import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "../..");
const python = process.env.PYTHON || "python3";
const verbose = process.argv.includes("--verbose");
const TIMEOUT_MS = 120_000;

const STEPS = [
  {
    name: "contract: generated protocol files, incl. drivers/common/epdj3000-protocol.js, are current",
    cmd: python,
    args: ["tools/gen_protocol.py", "--check"],
  },
  {
    name: "Mixxx: XML and script against the contract, mock engine",
    cmd: process.execPath,
    args: ["drivers/test/mixxx/run.mjs"],
  },
  {
    name: "VirtualDJ: definition and mapper against the contract and VDJscript vocabulary",
    cmd: process.execPath,
    args: ["drivers/test/virtualdj/check.mjs"],
  },
  {
    name: "djay Pro + rekordbox: cheat sheet, CSVs and generator against the contract",
    cmd: process.execPath,
    args: ["drivers/test/djay-rekordbox/check.mjs"],
  },
  {
    name: "XML files parse with xmllint",
    cmd: "xmllint",
    args: [
      "--noout",
      "drivers/mixxx/EP-DJ3000.midi.xml",
      "drivers/virtualdj/Devices/EPDJ3000.xml",
      "drivers/virtualdj/Mappers/EP-DJ3000.xml",
    ],
    optional: true,
  },
];

const lastLine = (text) => text.trim().split("\n").filter(Boolean).pop();
const indent = (text) => text.replace(/\n$/, "").split("\n").map((l) => `    ${l}`).join("\n");

let passed = 0;
const failed = [];
const skipped = [];

for (const step of STEPS) {
  const t0 = performance.now();
  const r = spawnSync(step.cmd, step.args, {
    cwd: root,
    encoding: "utf8",
    timeout: TIMEOUT_MS,
    maxBuffer: 64 * 1024 * 1024,
  });
  const seconds = ((performance.now() - t0) / 1000).toFixed(1);
  const output = `${r.stdout ?? ""}${r.stderr ?? ""}`;

  if (r.error && r.error.code === "ENOENT" && step.optional) {
    skipped.push(step.name);
    console.log(`SKIP  ${step.name}  (${step.cmd} not installed)`);
    continue;
  }

  let problem = null;
  if (r.error) problem = r.error.code === "ETIMEDOUT" ? `timed out after ${TIMEOUT_MS / 1000}s` : `could not run ${step.cmd}: ${r.error.message}`;
  else if (r.status !== 0) problem = r.signal ? `killed by ${r.signal}` : `exit code ${r.status}`;

  if (problem) {
    failed.push(step.name);
    console.log(`FAIL  ${step.name}  (${problem}, ${seconds}s)`);
    console.log(indent(output || "(no output)"));
  } else {
    passed++;
    console.log(`PASS  ${step.name}  (${seconds}s)`);
    if (output.trim()) console.log(verbose ? indent(output) : `      ${lastLine(output)}`);
  }
}

const total = STEPS.length;
console.log(`\n${passed} passed, ${failed.length} failed, ${skipped.length} skipped (${total} steps)`);
if (failed.length) {
  for (const name of failed) console.log(`  failed: ${name}`);
  process.exit(1);
}
