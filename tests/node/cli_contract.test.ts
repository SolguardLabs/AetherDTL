import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, assertDigest, byId, listScenarios, runBinary, runScenario } from "../helpers/aether.ts";

test("CLI lists stable audit scenarios", () => {
  assert.deepEqual(listScenarios(), [
    "baseline",
    "partial-fill",
    "expiration",
    "cancellation",
    "matching-controls",
    "operator-rotation",
  ]);
});

test("baseline scenario emits the public JSON contract", () => {
  const payload = runScenario("baseline");
  assertCommon(payload, "baseline");
  assert.equal(payload.intents.length, 1);
  assert.equal(payload.plans.length, 1);
  assert.equal(payload.risk.executed_plans, 1);
  assert.equal(payload.risk.rejected_plans, 0);
  assertDigest(payload.intents[0].digest);

  const alice = byId(payload.accounts, "alice");
  assert.ok(alice.balances.aUSDC.available < 5_000_000_000);
  assert.ok(alice.balances.aEUR.available > 25_000_000);
});

test("unknown scenarios fail without JSON output", () => {
  const result = runBinary(["scenario", "missing-scenario"], false);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /unknown scenario/);
  assert.equal(result.stdout.trim(), "");
});
