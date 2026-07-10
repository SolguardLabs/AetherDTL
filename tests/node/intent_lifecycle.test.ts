import test from "node:test";
import assert from "node:assert/strict";
import { assertCommon, byId, events, runScenario } from "../helpers/aether.ts";

test("signed intents remain open after a normal partial settlement", () => {
  const payload = runScenario("baseline");
  assertCommon(payload, "baseline");

  const intent = byId(payload.intents, "intent-alice-usdc-eur-001");
  assert.equal(intent.status, "open");
  assert.equal(intent.signature_ok, true);
  assert.equal(intent.max_source, 1_200_000_000);
  assert.equal(intent.partial.allow_partial, true);

  const accepted = events(payload, "intent_accepted");
  assert.equal(accepted.length, 1);
  assert.equal(accepted[0].subject, intent.id);
});

test("expiration moves an intent out of the executable set", () => {
  const payload = runScenario("expiration");
  assertCommon(payload, "expiration");

  const intent = byId(payload.intents, "intent-expiring-001");
  assert.equal(intent.status, "expired");
  assert.equal(payload.risk.expired_intents, 1);
  assert.equal(payload.risk.rejected_plans, 1);
  assert.match(payload.plans[0].reason, /intent_expired/);
  assert.equal(events(payload, "intent_expired").length, 1);
});

test("owner cancellation prevents later execution", () => {
  const payload = runScenario("cancellation");
  assertCommon(payload, "cancellation");

  const intent = byId(payload.intents, "intent-alice-usdc-eur-001");
  assert.equal(intent.status, "cancelled");
  assert.equal(payload.risk.cancelled_intents, 1);
  assert.equal(payload.risk.rejected_plans, 1);
  assert.match(payload.plans[0].reason, /intent_status/);
  assert.equal(events(payload, "intent_cancelled").length, 1);
});
