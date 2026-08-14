import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("..", import.meta.url)));
const executable = join(
  root,
  "build",
  process.platform === "win32" ? "aetherdtl.exe" : "aetherdtl",
);
const listed = spawnSync(executable, ["--list"], { cwd: root, encoding: "utf8" });
assert.equal(listed.status, 0, listed.stderr);
const scenarios = listed.stdout.trim().split(/\r?\n/).filter(Boolean);
assert.ok(scenarios.length >= 9);

for (const scenario of scenarios) {
  const execution = spawnSync(executable, ["scenario", scenario], {
    cwd: root,
    encoding: "utf8",
  });
  assert.equal(execution.status, 0, `${scenario}: ${execution.stderr}`);
  const payload = JSON.parse(execution.stdout);
  assert.equal(payload.protocol, "AetherDTL");
  assert.equal(payload.scenario, scenario);
  assert.equal(payload.invariants.ledger_non_negative, true);
  assert.equal(payload.invariants.signatures_valid, true);
  assert.equal(payload.invariants.replays_rejected, true);
  assert.equal(payload.invariants.reconciliation_consistent, true);
}

console.log(`smoke_scenarios=${scenarios.length}`);
