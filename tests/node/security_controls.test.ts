import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, byId, events, runScenario } from "../helpers/aether.ts";

test("guardian pause blocks admission until an explicit resume", () => {
  const payload = runScenario("emergency-pause");
  assertCommon(payload, "emergency-pause");

  const blocked = byId(payload.plans, "plan-pause-blocked");
  const admitted = byId(payload.plans, "plan-pause-admitted");
  assert.equal(blocked.status, "rejected");
  assert.match(blocked.reason, /network_paused/);
  assert.equal(admitted.status, "executed");
  assert.equal(events(payload, "pause_changed").length, 2);
  assert.equal(payload.security.paused, false);
});

test("plan identifiers provide replay idempotency", () => {
  const payload = runScenario("replay-control");
  assertCommon(payload, "replay-control");

  assert.equal(payload.plans.length, 2);
  assert.equal(payload.plans[0].status, "executed");
  assert.equal(payload.plans[1].status, "rejected");
  assert.match(payload.plans[1].reason, /plan_duplicate/);
  assert.equal(payload.risk.visible_source_used, 300_000_000);
  assert.equal(payload.invariants.replays_rejected, true);
});

test("aggregate vault demand is rejected before state mutation", () => {
  const payload = runScenario("reserve-preflight");
  assertCommon(payload, "reserve-preflight");

  const plan = byId(payload.plans, "plan-reserve-preflight");
  const alice = byId(payload.accounts, "alice");
  const vault = byId(payload.accounts, "vault-eur");
  assert.equal(plan.status, "rejected");
  assert.match(plan.reason, /vault_liquidity/);
  assert.equal(alice.balances.aUSDC.available, 5_000_000_000);
  assert.equal(vault.balances.aEUR.available, 9_000_000_000);
  assert.equal(payload.exposures.length, 0);
  assert.equal(events(payload, "transfer").length, 0);
});
