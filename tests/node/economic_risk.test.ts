import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, runScenario } from "../helpers/aether.ts";

test("economic model publishes correlated reserve requirements", () => {
  const payload = runScenario("baseline");
  assertCommon(payload, "baseline");

  const risk = payload.economic_risk;
  assert.equal(risk.gross_source_notional, 500_000_000);
  assert.equal(risk.gross_target_notional, 507_600_000);
  assert.ok(risk.standalone_loss > 0);
  assert.ok(risk.correlated_loss > 0);
  assert.ok(risk.required_reserve > risk.correlated_loss);
  assert.ok(risk.coverage_bps > 10_000);
  assert.equal(risk.shortfall, 0);
  assert.equal(risk.solvent, true);
  assert.equal(risk.cells.length, 1);
});

test("diversified lanes reduce the correlated tail below standalone loss", () => {
  const payload = runScenario("operator-rotation");
  assertCommon(payload, "operator-rotation");

  const risk = payload.economic_risk;
  assert.equal(risk.cells.length, 2);
  assert.ok(risk.correlated_loss < risk.standalone_loss);
  assert.ok(risk.correlated_loss > Math.max(...risk.cells.map((cell: any) => cell.stressed_loss)));
  assert.equal(payload.security.largest_operator_bps, 5_000);
  assert.equal(payload.security.largest_lane_bps, 5_000);
  assert.equal(payload.security.healthy, true);
});

test("inactive settlement has no modeled execution requirement", () => {
  const payload = runScenario("cancellation");
  assertCommon(payload, "cancellation");

  assert.equal(payload.economic_risk.required_reserve, 0);
  assert.equal(payload.economic_risk.coverage_bps, 10_000);
  assert.equal(payload.economic_risk.solvent, true);
});
