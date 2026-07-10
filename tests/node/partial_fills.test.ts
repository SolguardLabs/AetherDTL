import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, byId, events, runScenario } from "../helpers/aether.ts";

test("partial fills accumulate under a visible strategy window", () => {
  const payload = runScenario("partial-fill");
  assertCommon(payload, "partial-fill");

  assert.equal(payload.plans.length, 3);
  assert.equal(payload.risk.executed_plans, 2);
  assert.equal(payload.risk.rejected_plans, 1);
  assert.equal(payload.exposures.length, 1);
  assert.equal(payload.exposures[0].strategy_id, "twap-4");
  assert.equal(payload.exposures[0].used_source, 900_000_000);
  assert.equal(payload.exposures[0].fills, 2);

  const first = byId(payload.plans, "plan-partial-001");
  const second = byId(payload.plans, "plan-partial-002");
  const third = byId(payload.plans, "plan-partial-003");
  assert.equal(first.status, "executed");
  assert.equal(second.status, "executed");
  assert.equal(third.status, "rejected");
  assert.match(third.reason, /source_window/);
  assert.equal(events(payload, "exposure_updated").length, 2);
});
