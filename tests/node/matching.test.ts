import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, byId, runScenario } from "../helpers/aether.ts";

test("matching rejects low quotes and unauthorized submitters", () => {
  const payload = runScenario("matching-controls");
  assertCommon(payload, "matching-controls");

  assert.equal(payload.plans.length, 3);
  assert.equal(payload.risk.executed_plans, 1);
  assert.equal(payload.risk.rejected_plans, 2);

  const lowQuote = byId(payload.plans, "plan-low-quote-001");
  assert.equal(lowQuote.status, "rejected");
  assert.match(lowQuote.reason, /quote_floor/);

  const unauthorized = byId(payload.plans, "plan-unauthorized-001");
  assert.equal(unauthorized.status, "rejected");
  assert.match(unauthorized.reason, /operator_auth/);

  const backup = byId(payload.plans, "plan-backup-001");
  assert.equal(backup.status, "executed");
  assert.equal(backup.slices[0].lane_id, "usdc-eur-backup");
});

test("operator rotation keeps fills under one schedule", () => {
  const payload = runScenario("operator-rotation");
  assertCommon(payload, "operator-rotation");

  assert.equal(payload.risk.executed_plans, 2);
  assert.equal(payload.exposures.length, 1);
  assert.equal(payload.exposures[0].strategy_id, "rotation-cycle");
  assert.equal(payload.exposures[0].used_source, 600_000_000);
  assert.deepEqual(
    payload.plans.map((plan) => plan.operator_id),
    ["operator-a", "operator-b"],
  );
});
