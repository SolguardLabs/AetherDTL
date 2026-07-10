#include "aether.hpp"

namespace aether {
namespace {

constexpr Timestamp kStartTime = 1'720'000'000;

Account account(std::string id,
                std::string label,
                AccountRole role,
                std::string public_key,
                std::map<std::string, Amount> balances) {
    Account result;
    result.id = std::move(id);
    result.label = std::move(label);
    result.role = role;
    result.public_key = std::move(public_key);
    result.balances = std::move(balances);
    return result;
}

Asset asset(std::string id, std::string symbol, std::uint8_t decimals, Units price) {
    Asset result;
    result.id = std::move(id);
    result.symbol = std::move(symbol);
    result.decimals = decimals;
    result.price = price;
    result.reserve_floor = Amount::of(100'000);
    return result;
}

Lane lane(std::string id,
          std::string source,
          std::string target,
          std::string vault,
          Units max_fragment,
          Units operator_fee,
          Units protocol_fee,
          Units rebate) {
    Lane result;
    result.id = std::move(id);
    result.source_asset = std::move(source);
    result.target_asset = std::move(target);
    result.settlement_vault = std::move(vault);
    result.policy.min_output_bps = 9'850;
    result.policy.operator_fee_bps = operator_fee;
    result.policy.protocol_fee_bps = protocol_fee;
    result.policy.rebate_bps = rebate;
    result.policy.max_fragment_source = Amount::of(max_fragment);
    result.policy.allow_partial = true;
    result.policy.enabled = true;
    return result;
}

void seed_signers(SettlementEngine& engine) {
    engine.signer().register_secret("alice", "alice-local-aether-secret");
    engine.signer().register_secret("bob", "bob-local-aether-secret");
    engine.signer().register_secret("operator-a", "operator-a-local-aether-secret");
    engine.signer().register_secret("operator-b", "operator-b-local-aether-secret");
    engine.signer().register_secret("fee-collector", "fees-local-aether-secret");
}

}  // namespace

SettlementEngine ScenarioFactory::base_engine(Timestamp now) const {
    ScenarioAccounts accounts;
    SettlementEngine engine;
    engine.set_time(now);
    seed_signers(engine);

    engine.ledger().register_asset(asset("aUSDC", "Aether USD", 6, 1'000'000), now);
    engine.ledger().register_asset(asset("aEUR", "Aether Euro", 6, 1'080'000), now);
    engine.ledger().register_asset(asset("aGBP", "Aether Sterling", 6, 1'270'000), now);

    engine.ledger().register_account(
        account(
            accounts.alice,
            "Alice Treasury",
            AccountRole::User,
            engine.signer().public_key(accounts.alice),
            {
                {"aUSDC", Amount::of(5'000'000'000)},
                {"aEUR", Amount::of(25'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.bob,
            "Bob Market Maker",
            AccountRole::User,
            engine.signer().public_key(accounts.bob),
            {
                {"aUSDC", Amount::of(1'500'000'000)},
                {"aEUR", Amount::of(80'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.operator_a,
            "Northbridge Operator",
            AccountRole::Operator,
            engine.signer().public_key(accounts.operator_a),
            {
                {"aUSDC", Amount::of(250'000'000)},
                {"aEUR", Amount::of(90'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.operator_b,
            "Harbor Operator",
            AccountRole::Operator,
            engine.signer().public_key(accounts.operator_b),
            {
                {"aUSDC", Amount::of(250'000'000)},
                {"aEUR", Amount::of(90'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.fee_collector,
            "Protocol Fees",
            AccountRole::FeeCollector,
            engine.signer().public_key(accounts.fee_collector),
            {
                {"aUSDC", Amount::of(0)},
                {"aEUR", Amount::of(0)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.usdc_vault,
            "USDC Inventory Vault",
            AccountRole::Vault,
            "",
            {
                {"aUSDC", Amount::of(9'000'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.eur_vault,
            "EUR Settlement Vault",
            AccountRole::Vault,
            "",
            {
                {"aEUR", Amount::of(9'000'000'000)}
            }
        ),
        now
    );
    engine.ledger().register_account(
        account(
            accounts.gbp_vault,
            "GBP Settlement Vault",
            AccountRole::Vault,
            "",
            {
                {"aGBP", Amount::of(4'000'000'000)}
            }
        ),
        now
    );

    engine.ledger().register_lane(
        lane("usdc-eur-primary", "aUSDC", "aEUR", accounts.eur_vault, 800'000'000, 6, 2, 1),
        now
    );
    engine.ledger().register_lane(
        lane("usdc-eur-backup", "aUSDC", "aEUR", accounts.eur_vault, 500'000'000, 8, 2, 0),
        now
    );
    engine.ledger().register_lane(
        lane("usdc-gbp-primary", "aUSDC", "aGBP", accounts.gbp_vault, 600'000'000, 7, 2, 1),
        now
    );

    engine.ledger().authorize_operator(accounts.operator_a);
    engine.ledger().authorize_operator(accounts.operator_b);
    return engine;
}

Intent ScenarioFactory::base_intent(const ScenarioAccounts& accounts, Timestamp now) const {
    Intent intent;
    intent.id = "intent-alice-usdc-eur-001";
    intent.owner = accounts.alice;
    intent.source_asset = "aUSDC";
    intent.target_asset = "aEUR";
    intent.max_source = Amount::of(1'200'000'000);
    intent.min_target = Amount::of(1'090'000'000);
    intent.quote_floor_bps = 9'850;
    intent.valid_after = now;
    intent.expires_at = now + 3'600;
    intent.nonce = 42;
    intent.partial.allow_partial = true;
    intent.partial.mode = "adaptive";
    intent.partial.max_slices = 4;
    intent.partial.min_slice_source = Amount::of(100'000'000);
    intent.partial.max_slice_source = Amount::of(800'000'000);
    intent.partial.schedule = "rolling-window";
    intent.partial.strategy_ref = "twap-4";
    return intent;
}

FillSlice ScenarioFactory::slice(std::string id,
                                 std::string lane_id,
                                 Units source,
                                 Units target,
                                 int ordinal) const {
    FillSlice result;
    result.id = std::move(id);
    result.lane_id = std::move(lane_id);
    result.source_amount = Amount::of(source);
    result.quoted_target = Amount::of(target);
    result.price_bps = 9'900;
    result.ordinal = ordinal;
    result.maker_rebate = true;
    return result;
}

ExecutionPlan ScenarioFactory::plan_for(const Intent& intent,
                                        std::string plan_id,
                                        std::string operator_id,
                                        std::string strategy_id,
                                        std::vector<FillSlice> slices,
                                        Timestamp now) const {
    ExecutionPlan plan;
    plan.id = std::move(plan_id);
    plan.operator_id = std::move(operator_id);
    plan.intent_id = intent.id;
    plan.intent_digest = intent.digest;
    plan.strategy_id = std::move(strategy_id);
    plan.slices = std::move(slices);
    plan.submitted_at = now;
    return plan;
}

ScenarioResult ScenarioFactory::baseline() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "baseline";
    result.engine = base_engine(kStartTime);

    auto intent = result.engine.sign_intent(base_intent(accounts, kStartTime));
    result.engine.submit_intent(intent);

    auto plan = plan_for(
        intent,
        "plan-baseline-001",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-baseline-0", "usdc-eur-primary", 500'000'000, 470'000'000, 0)
        },
        kStartTime + 60
    );
    result.engine.set_time(kStartTime + 60);
    result.engine.execute_plan(plan);
    result.notes.push_back("single intent accepted and settled through primary lane");
    result.notes.push_back("operator and protocol fees are accrued in target asset");
    return result;
}

ScenarioResult ScenarioFactory::partial_fill() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "partial-fill";
    result.engine = base_engine(kStartTime);

    auto intent = result.engine.sign_intent(base_intent(accounts, kStartTime));
    result.engine.submit_intent(intent);

    auto first = plan_for(
        intent,
        "plan-partial-001",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-partial-0", "usdc-eur-primary", 400'000'000, 374'000'000, 0)
        },
        kStartTime + 30
    );
    result.engine.set_time(kStartTime + 30);
    result.engine.execute_plan(first);

    auto second = plan_for(
        intent,
        "plan-partial-002",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-partial-1", "usdc-eur-primary", 500'000'000, 470'000'000, 1)
        },
        kStartTime + 90
    );
    result.engine.set_time(kStartTime + 90);
    result.engine.execute_plan(second);

    auto third = plan_for(
        intent,
        "plan-partial-003",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-partial-2", "usdc-eur-primary", 400'000'000, 374'000'000, 2)
        },
        kStartTime + 120
    );
    result.engine.set_time(kStartTime + 120);
    result.engine.execute_plan(third);

    result.notes.push_back("two fills share one operating strategy");
    result.notes.push_back("the third fill is rejected by the visible source window");
    return result;
}

ScenarioResult ScenarioFactory::expiration() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "expiration";
    result.engine = base_engine(kStartTime);

    auto intent = base_intent(accounts, kStartTime);
    intent.id = "intent-expiring-001";
    intent.expires_at = kStartTime + 120;
    auto signed_intent = result.engine.sign_intent(intent);
    result.engine.submit_intent(signed_intent);

    result.engine.set_time(kStartTime + 121);
    auto plan = plan_for(
        signed_intent,
        "plan-expired-001",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-expired-0", "usdc-eur-primary", 200'000'000, 188'000'000, 0)
        },
        kStartTime + 121
    );
    result.engine.execute_plan(plan);

    result.notes.push_back("intent expires before the operator plan is evaluated");
    return result;
}

ScenarioResult ScenarioFactory::cancellation() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "cancellation";
    result.engine = base_engine(kStartTime);

    auto intent = result.engine.sign_intent(base_intent(accounts, kStartTime));
    result.engine.submit_intent(intent);
    result.engine.cancel_intent(intent.id, accounts.alice);

    auto plan = plan_for(
        intent,
        "plan-cancelled-001",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-cancelled-0", "usdc-eur-primary", 200'000'000, 188'000'000, 0)
        },
        kStartTime + 15
    );
    result.engine.set_time(kStartTime + 15);
    result.engine.execute_plan(plan);

    result.notes.push_back("owner cancellation transitions the intent out of the open set");
    return result;
}

ScenarioResult ScenarioFactory::matching_controls() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "matching-controls";
    result.engine = base_engine(kStartTime);

    auto intent = result.engine.sign_intent(base_intent(accounts, kStartTime));
    result.engine.submit_intent(intent);

    auto low_quote = plan_for(
        intent,
        "plan-low-quote-001",
        accounts.operator_a,
        "twap-4",
        {
            slice("slice-low-quote-0", "usdc-eur-primary", 300'000'000, 200'000'000, 0)
        },
        kStartTime + 20
    );
    result.engine.set_time(kStartTime + 20);
    result.engine.execute_plan(low_quote);

    auto unauthorized = plan_for(
        intent,
        "plan-unauthorized-001",
        accounts.bob,
        "twap-4",
        {
            slice("slice-unauth-0", "usdc-eur-primary", 300'000'000, 282'000'000, 0)
        },
        kStartTime + 25
    );
    result.engine.set_time(kStartTime + 25);
    result.engine.execute_plan(unauthorized);

    auto valid = plan_for(
        intent,
        "plan-backup-001",
        accounts.operator_b,
        "twap-4",
        {
            slice("slice-backup-0", "usdc-eur-backup", 300'000'000, 282'000'000, 0)
        },
        kStartTime + 35
    );
    result.engine.set_time(kStartTime + 35);
    result.engine.execute_plan(valid);

    result.notes.push_back("matching rejects stale economics and unauthorized submitters");
    result.notes.push_back("backup lane remains available for authorized operators");
    return result;
}

ScenarioResult ScenarioFactory::operator_rotation() const {
    ScenarioAccounts accounts;
    ScenarioResult result;
    result.name = "operator-rotation";
    result.engine = base_engine(kStartTime);

    auto intent = result.engine.sign_intent(base_intent(accounts, kStartTime));
    result.engine.submit_intent(intent);

    auto first = plan_for(
        intent,
        "plan-rotation-001",
        accounts.operator_a,
        "rotation-cycle",
        {
            slice("slice-rotation-0", "usdc-eur-primary", 300'000'000, 282'000'000, 0)
        },
        kStartTime + 45
    );
    result.engine.set_time(kStartTime + 45);
    result.engine.execute_plan(first);

    auto second = plan_for(
        intent,
        "plan-rotation-002",
        accounts.operator_b,
        "rotation-cycle",
        {
            slice("slice-rotation-1", "usdc-eur-backup", 300'000'000, 282'000'000, 1)
        },
        kStartTime + 105
    );
    result.engine.set_time(kStartTime + 105);
    result.engine.execute_plan(second);

    result.notes.push_back("two authorized operators can service one partial schedule");
    return result;
}

std::vector<std::string> scenario_names() {
    return {
        "baseline",
        "partial-fill",
        "expiration",
        "cancellation",
        "matching-controls",
        "operator-rotation"
    };
}

ScenarioResult run_scenario(const std::string& name) {
    ScenarioFactory factory;
    if (name == "baseline") {
        return factory.baseline();
    }
    if (name == "partial-fill") {
        return factory.partial_fill();
    }
    if (name == "expiration") {
        return factory.expiration();
    }
    if (name == "cancellation") {
        return factory.cancellation();
    }
    if (name == "matching-controls") {
        return factory.matching_controls();
    }
    if (name == "operator-rotation") {
        return factory.operator_rotation();
    }
    throw std::runtime_error("unknown scenario: " + name);
}

}  // namespace aether
