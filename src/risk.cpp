#include "aether.hpp"

#include <cmath>

namespace aether {
namespace {

Amount notional(const Ledger& ledger, const std::string& asset_id, Amount amount) {
    if (!ledger.has_asset(asset_id) || amount.is_zero()) {
        return Amount::zero();
    }
    return amount.checked_mul_ratio_floor(ledger.asset(asset_id).price, kPriceScale);
}

Amount bps(Amount value, Units rate) {
    return value.checked_mul_bps_floor(rate);
}

Amount add_many(std::initializer_list<Amount> values) {
    auto total = Amount::zero();
    for (const auto value : values) {
        total = total.checked_add(value);
    }
    return total;
}

}  // namespace

void StressParameters::validate() const {
    for (const auto value : {
             source_price_shock_bps,
             target_price_shock_bps,
             liquidity_haircut_bps,
             common_correlation_bps,
             operator_default_bps,
             concentration_charge_bps,
             operational_buffer_bps,
         }) {
        if (value < 0 || value > kBasisPoints) {
            throw std::runtime_error("stress parameter is outside basis-point range");
        }
    }
}

std::string EconomicRiskCell::canonical() const {
    return join_fields({
        "economic-risk-cell",
        key,
        source_notional.str(),
        target_notional.str(),
        stressed_loss.str(),
    });
}

bool EconomicRiskSnapshot::solvent() const {
    return shortfall.is_zero();
}

std::string EconomicRiskSnapshot::canonical() const {
    std::vector<std::string> fields{
        "economic-risk-snapshot",
        gross_source_notional.str(),
        gross_target_notional.str(),
        standalone_loss.str(),
        correlated_loss.str(),
        concentration_charge.str(),
        operational_buffer.str(),
        required_reserve.str(),
        available_reserve.str(),
        shortfall.str(),
        std::to_string(coverage_bps),
        std::to_string(largest_cell_bps),
        bool_json(solvent()),
    };
    for (const auto& cell : cells) {
        fields.push_back(cell.canonical());
    }
    return join_fields(fields);
}

EconomicRiskSnapshot EconomicRiskModel::assess(const SettlementEngine& engine,
                                               const StressParameters& parameters) const {
    parameters.validate();
    EconomicRiskSnapshot snapshot;
    std::map<std::string, EconomicRiskCell> cells;

    for (const auto& plan : engine.plans()) {
        if (plan.status != PlanStatus::Executed) {
            continue;
        }
        for (const auto& slice : plan.slices) {
            if (!engine.ledger().has_lane(slice.lane_id)) {
                continue;
            }
            const auto& lane = engine.ledger().lane(slice.lane_id);
            auto& cell = cells[lane.id];
            cell.key = lane.id;
            const auto source_value =
                notional(engine.ledger(), lane.source_asset, slice.source_amount);
            const auto target_value =
                notional(engine.ledger(), lane.target_asset, slice.quoted_target);
            const auto loss = add_many({
                bps(source_value, parameters.source_price_shock_bps),
                bps(target_value, parameters.target_price_shock_bps),
                bps(target_value, parameters.liquidity_haircut_bps),
                bps(source_value, parameters.operator_default_bps),
            });
            cell.source_notional = cell.source_notional.checked_add(source_value);
            cell.target_notional = cell.target_notional.checked_add(target_value);
            cell.stressed_loss = cell.stressed_loss.checked_add(loss);
        }
    }

    Amount largest_cell_notional = Amount::zero();
    Amount largest_cell_loss = Amount::zero();
    long double square_sum = 0.0L;
    for (const auto& [_, cell] : cells) {
        snapshot.cells.push_back(cell);
        snapshot.gross_source_notional =
            snapshot.gross_source_notional.checked_add(cell.source_notional);
        snapshot.gross_target_notional =
            snapshot.gross_target_notional.checked_add(cell.target_notional);
        snapshot.standalone_loss = snapshot.standalone_loss.checked_add(cell.stressed_loss);
        const auto cell_notional = cell.source_notional.checked_add(cell.target_notional);
        largest_cell_notional = std::max(largest_cell_notional, cell_notional);
        largest_cell_loss = std::max(largest_cell_loss, cell.stressed_loss);
        const auto loss = static_cast<long double>(cell.stressed_loss.units);
        square_sum += loss * loss;
    }

    if (!snapshot.cells.empty()) {
        const auto rho =
            static_cast<long double>(parameters.common_correlation_bps) / kBasisPoints;
        const auto aggregate = static_cast<long double>(snapshot.standalone_loss.units);
        const auto variance = (1.0L - rho) * square_sum + rho * aggregate * aggregate;
        const auto correlated = static_cast<Units>(std::floor(std::sqrt(variance)));
        snapshot.correlated_loss = Amount::of(correlated);
    }

    const auto gross_notional =
        snapshot.gross_source_notional.checked_add(snapshot.gross_target_notional);
    if (gross_notional.is_positive()) {
        snapshot.largest_cell_bps =
            largest_cell_notional.checked_mul_ratio_floor(kBasisPoints, gross_notional.units).units;
    }
    snapshot.concentration_charge =
        bps(largest_cell_loss, parameters.concentration_charge_bps);
    snapshot.operational_buffer = bps(gross_notional, parameters.operational_buffer_bps);
    snapshot.required_reserve = add_many({
        snapshot.correlated_loss,
        snapshot.concentration_charge,
        snapshot.operational_buffer,
    });

    for (const auto& account : engine.ledger().accounts()) {
        if (account.role != AccountRole::Vault) {
            continue;
        }
        for (const auto& line : account.lines()) {
            snapshot.available_reserve = snapshot.available_reserve.checked_add(
                notional(engine.ledger(), line.asset_id, line.available)
            );
        }
    }

    if (snapshot.available_reserve < snapshot.required_reserve) {
        snapshot.shortfall = snapshot.required_reserve.checked_sub(snapshot.available_reserve);
    }
    snapshot.coverage_bps = snapshot.required_reserve.is_zero()
                                ? kBasisPoints
                                : snapshot.available_reserve
                                      .checked_mul_ratio_floor(
                                          kBasisPoints,
                                          snapshot.required_reserve.units
                                      )
                                      .units;
    return snapshot;
}

}  // namespace aether
