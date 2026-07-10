#include "aether.hpp"

namespace aether {

std::string OperatorScore::canonical() const {
    return join_fields({
        "operator-score",
        operator_id,
        source_handled.str(),
        target_fees.str(),
        target_inventory.str(),
        std::to_string(accepted_plans),
        std::to_string(rejected_plans),
        std::to_string(total_slices),
        std::to_string(reliability_bps),
        std::to_string(inventory_bps),
        std::to_string(score_bps)
    });
}

std::string OperatorScore::tier() const {
    if (score_bps >= 9'000) {
        return "prime";
    }
    if (score_bps >= 7'000) {
        return "standard";
    }
    if (score_bps > 0) {
        return "limited";
    }
    return "idle";
}

bool OperatorScore::active() const {
    return accepted_plans > 0 || target_inventory.is_positive();
}

Units OperatorScorer::reliability(int accepted, int rejected) const {
    const int total = accepted + rejected;
    if (total <= 0) {
        return 0;
    }
    return static_cast<Units>((static_cast<long double>(accepted) * kBasisPoints) /
                              static_cast<long double>(total));
}

Units OperatorScorer::inventory_depth(const Ledger& ledger,
                                      const std::string& operator_id,
                                      const std::string& target_asset) const {
    if (!ledger.has_account(operator_id) || !ledger.has_asset(target_asset)) {
        return 0;
    }
    const auto balance = ledger.balance_of(operator_id, target_asset);
    const auto total = ledger.total_for_asset(target_asset);
    if (!balance.is_positive() || !total.is_positive()) {
        return 0;
    }
    return balance.checked_mul_ratio_floor(kBasisPoints, total.units).units;
}

OperatorScore OperatorScorer::score(const SettlementEngine& engine,
                                    const std::string& operator_id,
                                    const std::string& target_asset) const {
    OperatorScore result;
    result.operator_id = operator_id;

    if (engine.ledger().has_account(operator_id)) {
        result.target_inventory = engine.ledger().balance_of(operator_id, target_asset);
    }

    for (const auto& plan : engine.plans()) {
        if (plan.operator_id != operator_id) {
            continue;
        }

        result.total_slices += static_cast<int>(plan.slices.size());
        if (plan.status == PlanStatus::Executed) {
            result.accepted_plans += 1;
            result.source_handled = result.source_handled.checked_add(plan.gross_source());
            for (const auto& slice : plan.slices) {
                if (!engine.ledger().has_lane(slice.lane_id)) {
                    continue;
                }
                const auto& lane = engine.ledger().lane(slice.lane_id);
                if (lane.target_asset != target_asset) {
                    continue;
                }
                result.target_fees =
                    result.target_fees.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps));
            }
        }

        if (plan.status == PlanStatus::Rejected) {
            result.rejected_plans += 1;
        }
    }

    result.reliability_bps = reliability(result.accepted_plans, result.rejected_plans);
    result.inventory_bps = inventory_depth(engine.ledger(), operator_id, target_asset);
    result.score_bps = static_cast<Units>(
        (static_cast<long double>(result.reliability_bps) * 70.0L +
         static_cast<long double>(result.inventory_bps) * 30.0L) /
        100.0L
    );

    return result;
}

std::vector<OperatorScore> OperatorScorer::score_all(const SettlementEngine& engine,
                                                     const std::string& target_asset) const {
    std::vector<OperatorScore> result;
    for (const auto& account : engine.ledger().accounts()) {
        if (account.role != AccountRole::Operator) {
            continue;
        }
        result.push_back(score(engine, account.id, target_asset));
    }

    std::sort(result.begin(), result.end(), [](const OperatorScore& lhs, const OperatorScore& rhs) {
        if (lhs.score_bps != rhs.score_bps) {
            return lhs.score_bps > rhs.score_bps;
        }
        if (lhs.reliability_bps != rhs.reliability_bps) {
            return lhs.reliability_bps > rhs.reliability_bps;
        }
        if (lhs.target_inventory != rhs.target_inventory) {
            return lhs.target_inventory > rhs.target_inventory;
        }
        return lhs.operator_id < rhs.operator_id;
    });

    return result;
}

std::optional<OperatorScore> OperatorScorer::best_operator(const SettlementEngine& engine,
                                                           const std::string& target_asset) const {
    const auto scores = score_all(engine, target_asset);
    if (scores.empty()) {
        return std::nullopt;
    }
    return scores.front();
}

std::string OperatorScorer::digest(const std::vector<OperatorScore>& scores) const {
    std::vector<std::string> fields{"operator-scores"};
    for (const auto& score : scores) {
        fields.push_back(score.canonical());
    }
    return Digest::from_payload(join_fields(fields)).str();
}

}  // namespace aether
