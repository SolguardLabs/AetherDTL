#include "aether.hpp"

namespace aether {
namespace {

void comma(std::ostream& out, bool needed) {
    if (needed) {
        out << ",";
    }
}

std::string quote(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

std::pair<std::string, std::string> primary_assets(const SettlementEngine& engine) {
    const auto intents = engine.intents().intents();
    if (!intents.empty()) {
        return {intents.front().source_asset, intents.front().target_asset};
    }
    const auto assets = engine.ledger().assets();
    if (assets.size() >= 2) {
        return {assets[0].id, assets[1].id};
    }
    if (assets.size() == 1) {
        return {assets[0].id, assets[0].id};
    }
    return {"", ""};
}

}  // namespace

JsonWriter::JsonWriter(std::ostream& out) : out_(out) {}

void JsonWriter::write_scenario(const ScenarioResult& result) {
    out_ << "{";
    out_ << "\"lab\":\"AetherDTL\",";
    out_ << "\"scenario\":" << quote(result.name) << ",";
    out_ << "\"network_id\":\"aether-local-intentnet\",";
    out_ << "\"clock\":" << result.engine.now() << ",";
    out_ << "\"state_digest\":" << quote(result.engine.digest()) << ",";
    write_assets(result.engine);
    out_ << ",";
    write_lanes(result.engine);
    out_ << ",";
    write_accounts(result.engine);
    out_ << ",";
    write_intents(result.engine);
    out_ << ",";
    write_plans(result.engine);
    out_ << ",";
    write_exposures(result.engine);
    out_ << ",";
    write_events(result.engine);
    out_ << ",";
    write_totals(result.engine);
    out_ << ",";
    write_risk(result.engine);
    out_ << ",";
    write_invariants(result.engine);
    out_ << ",";
    write_notes(result);
    out_ << "}\n";
}

void JsonWriter::write_assets(const SettlementEngine& engine) {
    out_ << "\"assets\":[";
    const auto assets = engine.ledger().assets();
    for (std::size_t i = 0; i < assets.size(); ++i) {
        comma(out_, i != 0);
        const auto& asset = assets[i];
        out_ << "{";
        out_ << "\"id\":" << quote(asset.id) << ",";
        out_ << "\"symbol\":" << quote(asset.symbol) << ",";
        out_ << "\"decimals\":" << static_cast<int>(asset.decimals) << ",";
        out_ << "\"reserve_floor\":" << asset.reserve_floor.units << ",";
        out_ << "\"price\":" << asset.price;
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_lanes(const SettlementEngine& engine) {
    out_ << "\"lanes\":[";
    const auto lanes = engine.ledger().lanes();
    for (std::size_t i = 0; i < lanes.size(); ++i) {
        comma(out_, i != 0);
        const auto& lane = lanes[i];
        out_ << "{";
        out_ << "\"id\":" << quote(lane.id) << ",";
        out_ << "\"source_asset\":" << quote(lane.source_asset) << ",";
        out_ << "\"target_asset\":" << quote(lane.target_asset) << ",";
        out_ << "\"settlement_vault\":" << quote(lane.settlement_vault) << ",";
        out_ << "\"policy\":{";
        out_ << "\"min_output_bps\":" << lane.policy.min_output_bps << ",";
        out_ << "\"operator_fee_bps\":" << lane.policy.operator_fee_bps << ",";
        out_ << "\"protocol_fee_bps\":" << lane.policy.protocol_fee_bps << ",";
        out_ << "\"rebate_bps\":" << lane.policy.rebate_bps << ",";
        out_ << "\"max_fragment_source\":" << lane.policy.max_fragment_source.units << ",";
        out_ << "\"allow_partial\":" << bool_json(lane.policy.allow_partial) << ",";
        out_ << "\"enabled\":" << bool_json(lane.policy.enabled);
        out_ << "}";
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_accounts(const SettlementEngine& engine) {
    out_ << "\"accounts\":[";
    const auto accounts = engine.ledger().accounts();
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        comma(out_, i != 0);
        const auto& account = accounts[i];
        out_ << "{";
        out_ << "\"id\":" << quote(account.id) << ",";
        out_ << "\"label\":" << quote(account.label) << ",";
        out_ << "\"role\":" << quote(to_string(account.role)) << ",";
        out_ << "\"public_key\":" << quote(account.public_key) << ",";
        out_ << "\"active\":" << bool_json(account.active) << ",";
        out_ << "\"balances\":{";
        const auto lines = account.lines();
        bool first_balance = true;
        for (const auto& line : lines) {
            comma(out_, !first_balance);
            first_balance = false;
            out_ << quote(line.asset_id) << ":{";
            out_ << "\"available\":" << line.available.units << ",";
            out_ << "\"reserved\":" << line.reserved.units;
            out_ << "}";
        }
        out_ << "}";
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_intents(const SettlementEngine& engine) {
    out_ << "\"intents\":[";
    const auto intents = engine.intents().intents();
    for (std::size_t i = 0; i < intents.size(); ++i) {
        comma(out_, i != 0);
        const auto& intent = intents[i];
        bool signature_ok = false;
        try {
            signature_ok = engine.signer().verify(intent.owner, intent.canonical_public(), intent.signature);
        } catch (const std::exception&) {
            signature_ok = false;
        }
        out_ << "{";
        out_ << "\"id\":" << quote(intent.id) << ",";
        out_ << "\"owner\":" << quote(intent.owner) << ",";
        out_ << "\"source_asset\":" << quote(intent.source_asset) << ",";
        out_ << "\"target_asset\":" << quote(intent.target_asset) << ",";
        out_ << "\"max_source\":" << intent.max_source.units << ",";
        out_ << "\"min_target\":" << intent.min_target.units << ",";
        out_ << "\"quote_floor_bps\":" << intent.quote_floor_bps << ",";
        out_ << "\"valid_after\":" << intent.valid_after << ",";
        out_ << "\"expires_at\":" << intent.expires_at << ",";
        out_ << "\"nonce\":" << intent.nonce << ",";
        out_ << "\"status\":" << quote(to_string(intent.status)) << ",";
        out_ << "\"digest\":" << quote(intent.digest.str()) << ",";
        out_ << "\"signature_ok\":" << bool_json(signature_ok) << ",";
        out_ << "\"partial\":{";
        out_ << "\"allow_partial\":" << bool_json(intent.partial.allow_partial) << ",";
        out_ << "\"mode\":" << quote(intent.partial.mode) << ",";
        out_ << "\"max_slices\":" << intent.partial.max_slices << ",";
        out_ << "\"min_slice_source\":" << intent.partial.min_slice_source.units << ",";
        out_ << "\"max_slice_source\":" << intent.partial.max_slice_source.units << ",";
        out_ << "\"schedule\":" << quote(intent.partial.schedule) << ",";
        out_ << "\"strategy_ref\":" << quote(intent.partial.strategy_ref);
        out_ << "}";
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_plans(const SettlementEngine& engine) {
    out_ << "\"plans\":[";
    const auto& plans = engine.plans();
    for (std::size_t i = 0; i < plans.size(); ++i) {
        comma(out_, i != 0);
        const auto& plan = plans[i];
        out_ << "{";
        out_ << "\"id\":" << quote(plan.id) << ",";
        out_ << "\"operator_id\":" << quote(plan.operator_id) << ",";
        out_ << "\"intent_id\":" << quote(plan.intent_id) << ",";
        out_ << "\"intent_digest\":" << quote(plan.intent_digest.str()) << ",";
        out_ << "\"strategy_id\":" << quote(plan.strategy_id) << ",";
        out_ << "\"submitted_at\":" << plan.submitted_at << ",";
        out_ << "\"status\":" << quote(to_string(plan.status)) << ",";
        out_ << "\"reason\":" << quote(plan.reason) << ",";
        out_ << "\"gross_source\":" << plan.gross_source().units << ",";
        out_ << "\"gross_target\":" << plan.gross_target().units << ",";
        out_ << "\"slices\":[";
        for (std::size_t j = 0; j < plan.slices.size(); ++j) {
            comma(out_, j != 0);
            const auto& slice = plan.slices[j];
            out_ << "{";
            out_ << "\"id\":" << quote(slice.id) << ",";
            out_ << "\"lane_id\":" << quote(slice.lane_id) << ",";
            out_ << "\"source_amount\":" << slice.source_amount.units << ",";
            out_ << "\"quoted_target\":" << slice.quoted_target.units << ",";
            out_ << "\"price_bps\":" << slice.price_bps << ",";
            out_ << "\"ordinal\":" << slice.ordinal << ",";
            out_ << "\"maker_rebate\":" << bool_json(slice.maker_rebate);
            out_ << "}";
        }
        out_ << "]";
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_exposures(const SettlementEngine& engine) {
    out_ << "\"exposures\":[";
    const auto exposures = engine.exposures();
    for (std::size_t i = 0; i < exposures.size(); ++i) {
        comma(out_, i != 0);
        const auto& bucket = exposures[i];
        out_ << "{";
        out_ << "\"intent_id\":" << quote(bucket.key.intent_id) << ",";
        out_ << "\"strategy_id\":" << quote(bucket.key.strategy_id) << ",";
        out_ << "\"used_source\":" << bucket.used_source.units << ",";
        out_ << "\"delivered_target\":" << bucket.delivered_target.units << ",";
        out_ << "\"fills\":" << bucket.fills << ",";
        out_ << "\"first_seen\":" << bucket.first_seen << ",";
        out_ << "\"last_seen\":" << bucket.last_seen;
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_events(const SettlementEngine& engine) {
    out_ << "\"events\":[";
    const auto& events = engine.ledger().journal().events();
    for (std::size_t i = 0; i < events.size(); ++i) {
        comma(out_, i != 0);
        const auto& event = events[i];
        out_ << "{";
        out_ << "\"kind\":" << quote(to_string(event.kind)) << ",";
        out_ << "\"time\":" << event.time << ",";
        out_ << "\"subject\":" << quote(event.subject) << ",";
        out_ << "\"asset_id\":" << quote(event.asset_id) << ",";
        out_ << "\"amount\":" << event.amount.units << ",";
        out_ << "\"message\":" << quote(event.message);
        out_ << "}";
    }
    out_ << "]";
}

void JsonWriter::write_totals(const SettlementEngine& engine) {
    const auto [source_asset, target_asset] = primary_assets(engine);
    const auto totals = engine.ledger().totals(source_asset, target_asset);
    out_ << "\"totals\":{";
    out_ << "\"source_asset\":" << quote(source_asset) << ",";
    out_ << "\"target_asset\":" << quote(target_asset) << ",";
    out_ << "\"user_source_debited\":" << totals.user_source_debited.units << ",";
    out_ << "\"user_target_credited\":" << totals.user_target_credited.units << ",";
    out_ << "\"operator_fees\":" << totals.operator_fees.units << ",";
    out_ << "\"protocol_fees\":" << totals.protocol_fees.units << ",";
    out_ << "\"rebates\":" << totals.rebates.units << ",";
    out_ << "\"vault_reserves\":" << totals.vault_reserves.units << ",";
    out_ << "\"circulating_source\":" << totals.circulating_source.units << ",";
    out_ << "\"circulating_target\":" << totals.circulating_target.units;
    out_ << "}";
}

void JsonWriter::write_risk(const SettlementEngine& engine) {
    const auto risk = engine.risk_view();
    out_ << "\"risk\":{";
    out_ << "\"open_intents\":" << risk.open_intents << ",";
    out_ << "\"cancelled_intents\":" << risk.cancelled_intents << ",";
    out_ << "\"expired_intents\":" << risk.expired_intents << ",";
    out_ << "\"rejected_plans\":" << risk.rejected_plans << ",";
    out_ << "\"executed_plans\":" << risk.executed_plans << ",";
    out_ << "\"exposure_buckets\":" << risk.exposure_buckets << ",";
    out_ << "\"visible_source_used\":" << risk.visible_source_used.units << ",";
    out_ << "\"visible_target_delivered\":" << risk.visible_target_delivered.units;
    out_ << "}";
}

void JsonWriter::write_invariants(const SettlementEngine& engine) {
    const auto invariants = engine.invariants();
    out_ << "\"invariants\":{";
    out_ << "\"ledger_non_negative\":" << bool_json(invariants.ledger_non_negative) << ",";
    out_ << "\"signatures_valid\":" << bool_json(invariants.signatures_valid) << ",";
    out_ << "\"plans_have_intents\":" << bool_json(invariants.plans_have_intents) << ",";
    out_ << "\"local_limits_hold\":" << bool_json(invariants.local_limits_hold) << ",";
    out_ << "\"lifecycle_consistent\":" << bool_json(invariants.lifecycle_consistent);
    out_ << "}";
}

void JsonWriter::write_notes(const ScenarioResult& result) {
    out_ << "\"notes\":[";
    for (std::size_t i = 0; i < result.notes.size(); ++i) {
        comma(out_, i != 0);
        out_ << quote(result.notes[i]);
    }
    out_ << "]";
}

}  // namespace aether
