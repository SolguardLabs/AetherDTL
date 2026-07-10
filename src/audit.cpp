#include "aether.hpp"

namespace aether {

std::string AccountRiskLine::canonical() const {
    return join_fields({
        "account-risk",
        account_id,
        to_string(role),
        asset_id,
        available.str(),
        reserved.str(),
        notional_value.str(),
        bool_json(below_floor)
    });
}

std::string LaneRiskLine::canonical() const {
    return join_fields({
        "lane-risk",
        lane_id,
        source_asset,
        target_asset,
        observed_source.str(),
        observed_target.str(),
        operator_fees.str(),
        protocol_fees.str(),
        std::to_string(executed_slices),
        bool_json(enabled)
    });
}

std::string IntentAuditLine::canonical() const {
    return join_fields({
        "intent-audit",
        intent_id,
        owner,
        to_string(status),
        max_source.str(),
        visible_source.str(),
        visible_target.str(),
        std::to_string(bucket_count),
        std::to_string(plan_count),
        bool_json(signature_ok)
    });
}

std::string OperatorAuditLine::canonical() const {
    return join_fields({
        "operator-audit",
        operator_id,
        source_received.str(),
        target_fees.str(),
        std::to_string(accepted_plans),
        std::to_string(rejected_plans),
        std::to_string(slices)
    });
}

std::string ReconciliationFrame::canonical() const {
    return join_fields({
        "reconciliation-frame",
        digest,
        total_user_source.str(),
        total_user_target.str(),
        total_vault_source.str(),
        total_vault_target.str(),
        total_operator_source.str(),
        total_operator_target.str(),
        total_fee_target.str(),
        std::to_string(accounts),
        std::to_string(assets),
        std::to_string(lanes),
        std::to_string(intents),
        std::to_string(plans),
        std::to_string(events)
    });
}

Amount Reconciler::notional(const Ledger& ledger, const std::string& asset_id, Amount amount) const {
    if (!ledger.has_asset(asset_id) || amount.is_zero()) {
        return Amount::zero();
    }
    const auto& asset = ledger.asset(asset_id);
    return amount.checked_mul_ratio_floor(asset.price, kPriceScale);
}

Amount Reconciler::event_sum(const SettlementEngine& engine,
                             EventKind kind,
                             const std::string& asset_id,
                             const std::function<bool(const JournalEvent&)>& predicate) const {
    Amount total = Amount::zero();
    for (const auto& event : engine.ledger().journal().events()) {
        if (event.kind != kind) {
            continue;
        }
        if (!asset_id.empty() && event.asset_id != asset_id) {
            continue;
        }
        if (!predicate(event)) {
            continue;
        }
        total = total.checked_add(event.amount);
    }
    return total;
}

std::vector<AccountRiskLine> Reconciler::account_lines(const SettlementEngine& engine) const {
    std::vector<AccountRiskLine> result;
    for (const auto& account : engine.ledger().accounts()) {
        for (const auto& line : account.lines()) {
            AccountRiskLine risk;
            risk.account_id = account.id;
            risk.role = account.role;
            risk.asset_id = line.asset_id;
            risk.available = line.available;
            risk.reserved = line.reserved;
            risk.notional_value = notional(engine.ledger(), line.asset_id, line.available);
            if (account.role == AccountRole::Vault && engine.ledger().has_asset(line.asset_id)) {
                risk.below_floor = line.available < engine.ledger().asset(line.asset_id).reserve_floor;
            }
            result.push_back(risk);
        }
    }
    std::sort(result.begin(), result.end(), [](const AccountRiskLine& lhs, const AccountRiskLine& rhs) {
        if (lhs.account_id != rhs.account_id) {
            return lhs.account_id < rhs.account_id;
        }
        return lhs.asset_id < rhs.asset_id;
    });
    return result;
}

std::vector<LaneRiskLine> Reconciler::lane_lines(const SettlementEngine& engine) const {
    std::map<std::string, LaneRiskLine> lines;
    for (const auto& lane : engine.ledger().lanes()) {
        LaneRiskLine line;
        line.lane_id = lane.id;
        line.source_asset = lane.source_asset;
        line.target_asset = lane.target_asset;
        line.enabled = lane.policy.enabled;
        lines.insert({lane.id, line});
    }

    for (const auto& plan : engine.plans()) {
        if (plan.status != PlanStatus::Executed) {
            continue;
        }
        for (const auto& slice : plan.slices) {
            if (!engine.ledger().has_lane(slice.lane_id)) {
                continue;
            }
            const auto& lane = engine.ledger().lane(slice.lane_id);
            auto& line = lines[slice.lane_id];
            line.observed_source = line.observed_source.checked_add(slice.source_amount);
            line.observed_target = line.observed_target.checked_add(slice.quoted_target);
            line.operator_fees =
                line.operator_fees.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps));
            line.protocol_fees =
                line.protocol_fees.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.protocol_fee_bps));
            line.executed_slices += 1;
        }
    }

    std::vector<LaneRiskLine> result;
    for (const auto& [_, line] : lines) {
        result.push_back(line);
    }
    return result;
}

std::vector<IntentAuditLine> Reconciler::intent_lines(const SettlementEngine& engine) const {
    std::vector<IntentAuditLine> result;
    const auto exposures = engine.exposures();
    for (const auto& intent : engine.intents().intents()) {
        IntentAuditLine line;
        line.intent_id = intent.id;
        line.owner = intent.owner;
        line.status = intent.status;
        line.max_source = intent.max_source;
        for (const auto& bucket : exposures) {
            if (bucket.key.intent_id == intent.id) {
                line.visible_source = line.visible_source.checked_add(bucket.used_source);
                line.visible_target = line.visible_target.checked_add(bucket.delivered_target);
                line.bucket_count += 1;
            }
        }
        for (const auto& plan : engine.plans()) {
            if (plan.intent_id == intent.id) {
                line.plan_count += 1;
            }
        }
        try {
            line.signature_ok = engine.signer().verify(intent.owner, intent.canonical_public(), intent.signature);
        } catch (const std::exception&) {
            line.signature_ok = false;
        }
        result.push_back(line);
    }
    return result;
}

std::vector<OperatorAuditLine> Reconciler::operator_lines(const SettlementEngine& engine) const {
    std::map<std::string, OperatorAuditLine> lines;
    for (const auto& account : engine.ledger().accounts()) {
        if (account.role == AccountRole::Operator) {
            OperatorAuditLine line;
            line.operator_id = account.id;
            lines.insert({account.id, line});
        }
    }

    for (const auto& plan : engine.plans()) {
        auto& line = lines[plan.operator_id];
        line.operator_id = plan.operator_id;
        line.slices += static_cast<int>(plan.slices.size());
        if (plan.status == PlanStatus::Executed) {
            line.accepted_plans += 1;
            line.source_received = line.source_received.checked_add(plan.gross_source());
            for (const auto& slice : plan.slices) {
                if (!engine.ledger().has_lane(slice.lane_id)) {
                    continue;
                }
                const auto& lane = engine.ledger().lane(slice.lane_id);
                line.target_fees =
                    line.target_fees.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps));
            }
        } else if (plan.status == PlanStatus::Rejected) {
            line.rejected_plans += 1;
        }
    }

    std::vector<OperatorAuditLine> result;
    for (const auto& [_, line] : lines) {
        result.push_back(line);
    }
    return result;
}

ReconciliationFrame Reconciler::frame(const SettlementEngine& engine,
                                      const std::string& source_asset,
                                      const std::string& target_asset) const {
    ReconciliationFrame frame;
    frame.accounts = static_cast<int>(engine.ledger().accounts().size());
    frame.assets = static_cast<int>(engine.ledger().assets().size());
    frame.lanes = static_cast<int>(engine.ledger().lanes().size());
    frame.intents = static_cast<int>(engine.intents().intents().size());
    frame.plans = static_cast<int>(engine.plans().size());
    frame.events = static_cast<int>(engine.ledger().journal().events().size());

    for (const auto& account : engine.ledger().accounts()) {
        if (account.role == AccountRole::User) {
            frame.total_user_source =
                frame.total_user_source.checked_add(account.available(source_asset));
            frame.total_user_target =
                frame.total_user_target.checked_add(account.available(target_asset));
        }
        if (account.role == AccountRole::Vault) {
            frame.total_vault_source =
                frame.total_vault_source.checked_add(account.available(source_asset));
            frame.total_vault_target =
                frame.total_vault_target.checked_add(account.available(target_asset));
        }
        if (account.role == AccountRole::Operator) {
            frame.total_operator_source =
                frame.total_operator_source.checked_add(account.available(source_asset));
            frame.total_operator_target =
                frame.total_operator_target.checked_add(account.available(target_asset));
        }
        if (account.role == AccountRole::FeeCollector) {
            frame.total_fee_target =
                frame.total_fee_target.checked_add(account.available(target_asset));
        }
    }

    frame.digest = Digest::from_payload(join_fields({
        engine.digest(),
        source_asset,
        target_asset,
        frame.total_user_source.str(),
        frame.total_user_target.str(),
        frame.total_vault_source.str(),
        frame.total_vault_target.str(),
        frame.total_operator_source.str(),
        frame.total_operator_target.str(),
        frame.total_fee_target.str()
    })).str();
    return frame;
}

std::string Reconciler::digest(const SettlementEngine& engine) const {
    std::vector<std::string> fields{"reconciler", engine.digest()};
    for (const auto& line : account_lines(engine)) {
        fields.push_back(line.canonical());
    }
    for (const auto& line : lane_lines(engine)) {
        fields.push_back(line.canonical());
    }
    for (const auto& line : intent_lines(engine)) {
        fields.push_back(line.canonical());
    }
    for (const auto& line : operator_lines(engine)) {
        fields.push_back(line.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

std::map<std::string, Amount> Reconciler::source_by_strategy(const SettlementEngine& engine) const {
    std::map<std::string, Amount> result;
    for (const auto& bucket : engine.exposures()) {
        const auto current = result[bucket.key.strategy_id];
        result[bucket.key.strategy_id] = current.checked_add(bucket.used_source);
    }
    return result;
}

std::map<std::string, Amount> Reconciler::target_by_operator(const SettlementEngine& engine) const {
    std::map<std::string, Amount> result;
    for (const auto& line : operator_lines(engine)) {
        result[line.operator_id] = line.target_fees;
    }
    return result;
}

std::map<std::string, Amount> Reconciler::rejected_source_by_reason(const SettlementEngine& engine) const {
    std::map<std::string, Amount> result;
    for (const auto& plan : engine.plans()) {
        if (plan.status != PlanStatus::Rejected) {
            continue;
        }
        const auto key = plan.reason.empty() ? "rejected" : plan.reason;
        result[key] = result[key].checked_add(plan.gross_source());
    }
    return result;
}

bool Reconciler::account_floors_hold(const SettlementEngine& engine) const {
    for (const auto& line : account_lines(engine)) {
        if (line.below_floor) {
            return false;
        }
    }
    return true;
}

bool Reconciler::lane_activity_is_supported(const SettlementEngine& engine) const {
    for (const auto& plan : engine.plans()) {
        for (const auto& slice : plan.slices) {
            if (!engine.ledger().has_lane(slice.lane_id)) {
                return false;
            }
            const auto& lane = engine.ledger().lane(slice.lane_id);
            if (!lane.policy.enabled && plan.status == PlanStatus::Executed) {
                return false;
            }
            if (lane.policy.max_fragment_source.is_positive() &&
                slice.source_amount > lane.policy.max_fragment_source &&
                plan.status == PlanStatus::Executed) {
                return false;
            }
        }
    }
    return true;
}

bool Reconciler::intent_lifecycle_is_ordered(const SettlementEngine& engine) const {
    std::map<std::string, Timestamp> accepted_at;
    std::map<std::string, Timestamp> terminal_at;
    for (const auto& event : engine.ledger().journal().events()) {
        if (event.kind == EventKind::IntentAccepted) {
            accepted_at[event.subject] = event.time;
        }
        if (event.kind == EventKind::IntentCancelled || event.kind == EventKind::IntentExpired) {
            terminal_at[event.subject] = event.time;
        }
    }
    for (const auto& [intent_id, when] : terminal_at) {
        const auto accepted = accepted_at.find(intent_id);
        if (accepted == accepted_at.end()) {
            return false;
        }
        if (when < accepted->second) {
            return false;
        }
    }
    return true;
}

bool Reconciler::frame_is_consistent(const SettlementEngine& engine,
                                     const std::string& source_asset,
                                     const std::string& target_asset) const {
    const auto current = frame(engine, source_asset, target_asset);
    if (current.accounts <= 0 || current.assets <= 0 || current.lanes <= 0) {
        return false;
    }
    if (current.events < current.accounts + current.assets) {
        return false;
    }
    if (current.total_user_source.units < 0 || current.total_user_target.units < 0) {
        return false;
    }
    if (current.total_vault_source.units < 0 || current.total_vault_target.units < 0) {
        return false;
    }
    return true;
}

std::string RouteQuote::canonical() const {
    return join_fields({
        "route-quote",
        lane_id,
        operator_id,
        source_amount.str(),
        expected_target.str(),
        operator_fee.str(),
        protocol_fee.str(),
        rebate.str(),
        net_target.str(),
        std::to_string(quality_bps)
    });
}

std::string RouteCandidate::canonical() const {
    std::vector<std::string> fields{
        "route-candidate",
        route_id,
        gross_source.str(),
        gross_target.str(),
        total_fees.str(),
        std::to_string(blended_quality_bps)
    };
    for (const auto& quote : quotes) {
        fields.push_back(quote.canonical());
    }
    return join_fields(fields);
}

std::vector<std::string> RouteCatalog::operators(const SettlementEngine& engine) const {
    std::vector<std::string> result;
    for (const auto& account : engine.ledger().accounts()) {
        if (account.role == AccountRole::Operator && engine.ledger().is_operator(account.id)) {
            result.push_back(account.id);
        }
    }
    return result;
}

RouteQuote RouteCatalog::quote_lane(const Ledger& ledger,
                                    const Lane& lane,
                                    const std::string& operator_id,
                                    Amount source) const {
    RouteQuote quote;
    quote.lane_id = lane.id;
    quote.operator_id = operator_id;
    quote.source_amount = source;
    quote.expected_target = ledger.quote(lane.source_asset, lane.target_asset, source);
    quote.operator_fee = quote.expected_target.checked_mul_bps_floor(lane.policy.operator_fee_bps);
    quote.protocol_fee = quote.expected_target.checked_mul_bps_floor(lane.policy.protocol_fee_bps);
    quote.rebate = quote.expected_target.checked_mul_bps_floor(lane.policy.rebate_bps);
    quote.net_target =
        quote.expected_target.checked_sub(quote.operator_fee.checked_add(quote.protocol_fee).checked_add(quote.rebate));
    if (quote.expected_target.is_positive()) {
        quote.quality_bps =
            quote.net_target.checked_mul_ratio_floor(kBasisPoints, quote.expected_target.units).units;
    }
    return quote;
}

std::vector<RouteQuote> RouteCatalog::quotes_for(const SettlementEngine& engine,
                                                 const Intent& intent,
                                                 Amount slice_source) const {
    std::vector<RouteQuote> result;
    if (!slice_source.is_positive()) {
        return result;
    }
    const auto operator_ids = operators(engine);
    for (const auto& lane : engine.ledger().lanes()) {
        if (lane.source_asset != intent.source_asset || lane.target_asset != intent.target_asset) {
            continue;
        }
        if (!lane.policy.enabled) {
            continue;
        }
        if (lane.policy.max_fragment_source.is_positive() &&
            slice_source > lane.policy.max_fragment_source) {
            continue;
        }
        for (const auto& operator_id : operator_ids) {
            result.push_back(quote_lane(engine.ledger(), lane, operator_id, slice_source));
        }
    }
    std::sort(result.begin(), result.end(), [](const RouteQuote& lhs, const RouteQuote& rhs) {
        if (lhs.net_target != rhs.net_target) {
            return lhs.net_target > rhs.net_target;
        }
        if (lhs.quality_bps != rhs.quality_bps) {
            return lhs.quality_bps > rhs.quality_bps;
        }
        if (lhs.lane_id != rhs.lane_id) {
            return lhs.lane_id < rhs.lane_id;
        }
        return lhs.operator_id < rhs.operator_id;
    });
    return result;
}

std::vector<RouteCandidate> RouteCatalog::candidates_for(const SettlementEngine& engine,
                                                         const Intent& intent,
                                                         Amount total_source,
                                                         int parts) const {
    std::vector<RouteCandidate> candidates;
    if (!total_source.is_positive() || parts <= 0) {
        return candidates;
    }

    const auto base = Amount::of(total_source.units / parts);
    const auto remainder = total_source.units % parts;
    std::vector<Amount> slice_sources;
    for (int i = 0; i < parts; ++i) {
        const auto extra = i == parts - 1 ? remainder : 0;
        slice_sources.push_back(Amount::of(base.units + extra));
    }

    RouteCandidate greedy;
    greedy.route_id = "candidate-greedy";
    for (const auto& slice_source : slice_sources) {
        const auto quotes = quotes_for(engine, intent, slice_source);
        if (quotes.empty()) {
            continue;
        }
        greedy.quotes.push_back(quotes.front());
    }
    for (const auto& quote : greedy.quotes) {
        greedy.gross_source = greedy.gross_source.checked_add(quote.source_amount);
        greedy.gross_target = greedy.gross_target.checked_add(quote.expected_target);
        greedy.total_fees =
            greedy.total_fees.checked_add(quote.operator_fee.checked_add(quote.protocol_fee).checked_add(quote.rebate));
    }
    if (greedy.gross_target.is_positive()) {
        greedy.blended_quality_bps =
            greedy.gross_target.checked_sub(greedy.total_fees)
                .checked_mul_ratio_floor(kBasisPoints, greedy.gross_target.units)
                .units;
    }
    if (!greedy.quotes.empty()) {
        greedy.route_id = "candidate-" + Digest::from_payload(greedy.canonical()).str().substr(0, 12);
        candidates.push_back(greedy);
    }

    RouteCandidate diversified;
    diversified.route_id = "candidate-diversified";
    int cursor = 0;
    for (const auto& slice_source : slice_sources) {
        auto quotes = quotes_for(engine, intent, slice_source);
        if (quotes.empty()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(cursor % static_cast<int>(quotes.size()));
        diversified.quotes.push_back(quotes[index]);
        cursor += 1;
    }
    for (const auto& quote : diversified.quotes) {
        diversified.gross_source = diversified.gross_source.checked_add(quote.source_amount);
        diversified.gross_target = diversified.gross_target.checked_add(quote.expected_target);
        diversified.total_fees =
            diversified.total_fees.checked_add(quote.operator_fee.checked_add(quote.protocol_fee).checked_add(quote.rebate));
    }
    if (diversified.gross_target.is_positive()) {
        diversified.blended_quality_bps =
            diversified.gross_target.checked_sub(diversified.total_fees)
                .checked_mul_ratio_floor(kBasisPoints, diversified.gross_target.units)
                .units;
    }
    if (!diversified.quotes.empty()) {
        diversified.route_id = "candidate-" + Digest::from_payload(diversified.canonical()).str().substr(0, 12);
        candidates.push_back(diversified);
    }

    std::sort(candidates.begin(), candidates.end(), [](const RouteCandidate& lhs, const RouteCandidate& rhs) {
        if (lhs.blended_quality_bps != rhs.blended_quality_bps) {
            return lhs.blended_quality_bps > rhs.blended_quality_bps;
        }
        if (lhs.gross_target != rhs.gross_target) {
            return lhs.gross_target > rhs.gross_target;
        }
        return lhs.route_id < rhs.route_id;
    });

    return candidates;
}

RouteCandidate RouteCatalog::best_candidate(const SettlementEngine& engine,
                                            const Intent& intent,
                                            Amount total_source,
                                            int parts) const {
    const auto candidates = candidates_for(engine, intent, total_source, parts);
    if (candidates.empty()) {
        return RouteCandidate{};
    }
    return candidates.front();
}

std::string RouteCatalog::digest_candidates(const std::vector<RouteCandidate>& candidates) const {
    std::vector<std::string> fields{"route-candidates"};
    for (const auto& candidate : candidates) {
        fields.push_back(candidate.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

}  // namespace aether
