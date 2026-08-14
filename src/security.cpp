#include "aether.hpp"

namespace aether {
namespace {

Amount notional(const Ledger& ledger, const std::string& asset_id, Amount amount) {
    if (!ledger.has_asset(asset_id) || amount.is_zero()) {
        return Amount::zero();
    }
    return amount.checked_mul_ratio_floor(ledger.asset(asset_id).price, kPriceScale);
}

Units ratio_bps(Amount numerator, Amount denominator) {
    if (!denominator.is_positive()) {
        return kBasisPoints;
    }
    return numerator.checked_mul_ratio_floor(kBasisPoints, denominator.units).units;
}

}  // namespace

std::string to_string(SecuritySignalKind kind) {
    switch (kind) {
        case SecuritySignalKind::EmergencyPause:
            return "emergency_pause";
        case SecuritySignalKind::VaultFloor:
            return "vault_floor";
        case SecuritySignalKind::OperatorConcentration:
            return "operator_concentration";
        case SecuritySignalKind::LaneConcentration:
            return "lane_concentration";
        case SecuritySignalKind::RejectionRate:
            return "rejection_rate";
        case SecuritySignalKind::ReserveCoverage:
            return "reserve_coverage";
    }
    return "unknown";
}

void SecurityLimits::validate() const {
    for (const auto value : {
             maximum_operator_concentration_bps,
             maximum_lane_concentration_bps,
             maximum_rejection_rate_bps,
         }) {
        if (value < 0 || value > kBasisPoints) {
            throw std::runtime_error("security percentage is outside basis-point range");
        }
    }
    if (minimum_reserve_coverage_bps < kBasisPoints) {
        throw std::runtime_error("reserve coverage cannot be below 100 percent");
    }
}

std::string SecuritySignal::canonical() const {
    return join_fields({
        "security-signal",
        to_string(kind),
        subject,
        std::to_string(observed),
        std::to_string(threshold),
        unit,
        bool_json(critical),
    });
}

std::string SecuritySnapshot::canonical() const {
    std::vector<std::string> fields{
        "security-snapshot",
        bool_json(paused),
        bool_json(healthy),
        executed_source.str(),
        available_reserves.str(),
        std::to_string(reserve_coverage_bps),
        std::to_string(rejection_rate_bps),
        std::to_string(largest_operator_bps),
        std::to_string(largest_lane_bps),
    };
    for (const auto& signal : signals) {
        fields.push_back(signal.canonical());
    }
    return join_fields(fields);
}

SecuritySnapshot SecurityMonitor::evaluate(const SettlementEngine& engine,
                                           const SecurityLimits& limits) const {
    limits.validate();
    SecuritySnapshot snapshot;
    snapshot.paused = engine.paused();

    std::map<std::string, Amount> by_operator;
    std::map<std::string, Amount> by_lane;
    int rejected = 0;
    int observed = 0;

    for (const auto& plan : engine.plans()) {
        observed += 1;
        if (plan.status == PlanStatus::Rejected) {
            rejected += 1;
            continue;
        }
        if (plan.status != PlanStatus::Executed) {
            continue;
        }
        for (const auto& slice : plan.slices) {
            if (!engine.ledger().has_lane(slice.lane_id)) {
                continue;
            }
            const auto& lane = engine.ledger().lane(slice.lane_id);
            const auto value = notional(engine.ledger(), lane.source_asset, slice.source_amount);
            snapshot.executed_source = snapshot.executed_source.checked_add(value);
            by_operator[plan.operator_id] = by_operator[plan.operator_id].checked_add(value);
            by_lane[lane.id] = by_lane[lane.id].checked_add(value);
        }
    }

    for (const auto& account : engine.ledger().accounts()) {
        if (account.role != AccountRole::Vault) {
            continue;
        }
        for (const auto& line : account.lines()) {
            snapshot.available_reserves = snapshot.available_reserves.checked_add(
                notional(engine.ledger(), line.asset_id, line.available)
            );
            if (engine.ledger().has_asset(line.asset_id) &&
                line.available < engine.ledger().asset(line.asset_id).reserve_floor) {
                snapshot.signals.push_back(SecuritySignal{
                    SecuritySignalKind::VaultFloor,
                    account.id + ":" + line.asset_id,
                    line.available.units,
                    engine.ledger().asset(line.asset_id).reserve_floor.units,
                    "asset_units",
                    true,
                });
            }
        }
    }

    snapshot.reserve_coverage_bps = ratio_bps(snapshot.available_reserves, snapshot.executed_source);
    snapshot.rejection_rate_bps =
        observed == 0 ? 0 : static_cast<Units>(rejected) * kBasisPoints / observed;

    for (const auto& [operator_id, amount] : by_operator) {
        const auto concentration = ratio_bps(amount, snapshot.executed_source);
        snapshot.largest_operator_bps = std::max(snapshot.largest_operator_bps, concentration);
        if (concentration > limits.maximum_operator_concentration_bps) {
            snapshot.signals.push_back(SecuritySignal{
                SecuritySignalKind::OperatorConcentration,
                operator_id,
                concentration,
                limits.maximum_operator_concentration_bps,
                "bps",
                false,
            });
        }
    }

    for (const auto& [lane_id, amount] : by_lane) {
        const auto concentration = ratio_bps(amount, snapshot.executed_source);
        snapshot.largest_lane_bps = std::max(snapshot.largest_lane_bps, concentration);
        if (concentration > limits.maximum_lane_concentration_bps) {
            snapshot.signals.push_back(SecuritySignal{
                SecuritySignalKind::LaneConcentration,
                lane_id,
                concentration,
                limits.maximum_lane_concentration_bps,
                "bps",
                false,
            });
        }
    }

    if (snapshot.paused) {
        snapshot.signals.push_back(SecuritySignal{
            SecuritySignalKind::EmergencyPause,
            engine.pause_reason(),
            1,
            0,
            "boolean",
            true,
        });
    }
    if (snapshot.rejection_rate_bps > limits.maximum_rejection_rate_bps) {
        snapshot.signals.push_back(SecuritySignal{
            SecuritySignalKind::RejectionRate,
            "settlement",
            snapshot.rejection_rate_bps,
            limits.maximum_rejection_rate_bps,
            "bps",
            false,
        });
    }
    if (snapshot.executed_source.is_positive() &&
        snapshot.reserve_coverage_bps < limits.minimum_reserve_coverage_bps) {
        snapshot.signals.push_back(SecuritySignal{
            SecuritySignalKind::ReserveCoverage,
            "vault-network",
            snapshot.reserve_coverage_bps,
            limits.minimum_reserve_coverage_bps,
            "bps",
            true,
        });
    }

    snapshot.healthy = snapshot.signals.empty();
    return snapshot;
}

}  // namespace aether
