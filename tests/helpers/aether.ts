import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const root = resolve(fileURLToPath(new URL("../../", import.meta.url)));
const exeName = process.platform === "win32" ? "aetherdtl.exe" : "aetherdtl";
const defaultBin = join(root, "build", exeName);

export type ScenarioPayload = {
  lab: string;
  scenario: string;
  network_id: string;
  clock: number;
  state_digest: string;
  assets: Array<Record<string, unknown>>;
  lanes: Array<Record<string, unknown>>;
  accounts: Array<Record<string, any>>;
  intents: Array<Record<string, any>>;
  plans: Array<Record<string, any>>;
  exposures: Array<Record<string, any>>;
  events: Array<Record<string, any>>;
  totals: Record<string, any>;
  risk: Record<string, any>;
  invariants: Record<string, any>;
  notes: string[];
};

export function binaryPath(): string {
  return process.env.AETHER_BIN ?? defaultBin;
}

export function ensureBuilt(): void {
  if (process.env.AETHER_BIN) return;
  if (existsSync(defaultBin)) return;
  const result = spawnSync(process.execPath, ["scripts/build.mjs"], {
    cwd: root,
    encoding: "utf8",
    stdio: "pipe",
  });
  if (result.status !== 0) {
    throw new Error(["build failed", result.stdout.trim(), result.stderr.trim()].filter(Boolean).join("\n"));
  }
}

export function runBinary(args: string[], expectSuccess = true) {
  ensureBuilt();
  const result = spawnSync(binaryPath(), args, {
    cwd: root,
    encoding: "utf8",
    stdio: "pipe",
  });
  if (expectSuccess && result.status !== 0) {
    throw new Error(
      [`aetherdtl ${args.join(" ")} failed`, result.stdout.trim(), result.stderr.trim()]
        .filter(Boolean)
        .join("\n"),
    );
  }
  return result;
}

export function listScenarios(): string[] {
  return runBinary(["--list"]).stdout.trim().split(/\r?\n/).filter(Boolean);
}

export function runScenario(name: string): ScenarioPayload {
  const result = runBinary(["scenario", name]);
  return JSON.parse(result.stdout) as ScenarioPayload;
}

export function byId<T extends Record<string, any>>(items: T[], id: string): T {
  const found = items.find((item) => item.id === id);
  assert.ok(found, `missing id ${id}`);
  return found;
}

export function events(payload: ScenarioPayload, kind: string): Array<Record<string, any>> {
  return payload.events.filter((event) => event.kind === kind);
}

export function assertDigest(value: unknown): void {
  assert.equal(typeof value, "string");
  assert.match(value as string, /^[0-9a-f]{32}$/);
}

export function assertCommon(payload: ScenarioPayload, scenario: string): void {
  assert.equal(payload.lab, "AetherDTL");
  assert.equal(payload.scenario, scenario);
  assert.equal(payload.network_id, "aether-local-intentnet");
  assertDigest(payload.state_digest);
  assert.ok(payload.assets.length >= 3);
  assert.ok(payload.lanes.length >= 3);
  assert.ok(payload.accounts.length >= 7);
  assert.ok(Array.isArray(payload.intents));
  assert.ok(Array.isArray(payload.plans));
  assert.ok(Array.isArray(payload.exposures));
  assert.ok(Array.isArray(payload.events));
  assert.equal(payload.invariants.ledger_non_negative, true);
  assert.equal(payload.invariants.signatures_valid, true);
  assert.equal(payload.invariants.plans_have_intents, true);
  assert.equal(payload.invariants.local_limits_hold, true);
  assert.equal(payload.invariants.lifecycle_consistent, true);
}
