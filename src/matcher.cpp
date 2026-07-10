#include "aether.hpp"

namespace aether {

std::optional<ValidationIssue> Matcher::check_slice_assets(const Ledger& ledger,
                                                           const Intent& intent,
                                                           const FillSlice& slice) const {
    if (!ledger.has_lane(slice.lane_id)) {
        return ValidationIssue::of("lane_missing", "settlement lane is not registered");
    }

    const auto& lane = ledger.lane(slice.lane_id);
    if (!lane.policy.enabled) {
        return ValidationIssue::of("lane_disabled", "settlement lane is disabled");
    }

    if (lane.source_asset != intent.source_asset) {
        return ValidationIssue::of("source_asset", "lane source asset does not match intent");
    }

    if (lane.target_asset != intent.target_asset) {
        return ValidationIssue::of("target_asset", "lane target asset does not match intent");
    }

    if (!ledger.has_account(lane.settlement_vault)) {
        return ValidationIssue::of("vault_missing", "settlement vault is not registered");
    }

    return std::nullopt;
}

std::optional<ValidationIssue> Matcher::check_slice_amounts(const Lane& lane,
                                                            const Intent& intent,
                                                            const FillSlice& slice) const {
    if (!slice.source_amount.is_positive()) {
        return ValidationIssue::of("slice_source", "slice source amount must be positive");
    }

    if (!slice.quoted_target.is_positive()) {
        return ValidationIssue::of("slice_target", "slice target amount must be positive");
    }

    if (slice.price_bps <= 0) {
        return ValidationIssue::of("slice_price", "slice price must be positive");
    }

    if (!intent.partial.allow_partial && slice.ordinal != 0) {
        return ValidationIssue::of("single_fill", "single fill intent received additional slice");
    }

    if (intent.partial.min_slice_source.is_positive() &&
        slice.source_amount < intent.partial.min_slice_source) {
        return ValidationIssue::of("slice_min", "slice source amount is below policy floor");
    }

    if (intent.partial.max_slice_source.is_positive() &&
        slice.source_amount > intent.partial.max_slice_source) {
        return ValidationIssue::of("slice_max", "slice source amount exceeds policy cap");
    }

    if (lane.policy.max_fragment_source.is_positive() &&
        slice.source_amount > lane.policy.max_fragment_source) {
        return ValidationIssue::of("lane_fragment", "slice source amount exceeds lane cap");
    }

    return std::nullopt;
}

std::optional<ValidationIssue> Matcher::check_price(const Intent& intent,
                                                    const FillSlice& slice,
                                                    Amount expected_target) const {
    const auto floor_from_oracle = expected_target.checked_mul_bps_floor(intent.quote_floor_bps);
    const auto floor_from_slice = expected_target.checked_mul_bps_floor(slice.price_bps);
    const auto required = floor_from_oracle > floor_from_slice ? floor_from_oracle : floor_from_slice;

    if (slice.quoted_target < required) {
        return ValidationIssue::of("quote_floor", "slice target amount is below quote floor");
    }

    return std::nullopt;
}

MatchResult Matcher::evaluate(const Ledger& ledger,
                              const Intent& intent,
                              const ExecutionPlan& plan,
                              Timestamp now) const {
    std::vector<ValidationIssue> issues;

    if (!ledger.has_account(intent.owner)) {
        issues.push_back(ValidationIssue::of("owner_missing", "intent owner is not registered"));
    }

    if (!ledger.has_asset(intent.source_asset)) {
        issues.push_back(ValidationIssue::of("source_missing", "source asset is not registered"));
    }

    if (!ledger.has_asset(intent.target_asset)) {
        issues.push_back(ValidationIssue::of("target_missing", "target asset is not registered"));
    }

    if (!ledger.has_account(plan.operator_id)) {
        issues.push_back(ValidationIssue::of("operator_missing", "operator account is not registered"));
    } else if (!ledger.is_operator(plan.operator_id)) {
        issues.push_back(ValidationIssue::of("operator_auth", "operator account is not authorized"));
    }

    if (plan.intent_id != intent.id) {
        issues.push_back(ValidationIssue::of("intent_id", "plan intent id does not match intent"));
    }

    if (!(plan.intent_digest == intent.digest)) {
        issues.push_back(ValidationIssue::of("intent_digest", "plan intent digest does not match intent"));
    }

    if (intent.status != IntentStatus::Open) {
        issues.push_back(ValidationIssue::of("intent_status", "intent is not open"));
    }

    if (now < intent.valid_after) {
        issues.push_back(ValidationIssue::of("intent_time", "intent is not active yet"));
    }

    if (intent.expired(now)) {
        issues.push_back(ValidationIssue::of("intent_expired", "intent has expired"));
    }

    if (plan.slices.empty()) {
        issues.push_back(ValidationIssue::of("plan_empty", "execution plan has no slices"));
    }

    if (!intent.partial.allow_partial && plan.slices.size() > 1) {
        issues.push_back(ValidationIssue::of("partial_disabled", "intent does not allow multiple slices"));
    }

    if (intent.partial.max_slices > 0 &&
        static_cast<int>(plan.slices.size()) > intent.partial.max_slices) {
        issues.push_back(ValidationIssue::of("slice_count", "execution plan exceeds slice count"));
    }

    Amount gross_source = Amount::zero();
    Amount gross_target = Amount::zero();
    Amount operator_fee = Amount::zero();
    Amount protocol_fee = Amount::zero();
    Amount rebate = Amount::zero();

    std::set<std::string> slice_ids;
    for (const auto& slice : plan.slices) {
        if (slice.id.empty()) {
            issues.push_back(ValidationIssue::of("slice_id", "slice id is required"));
        } else if (!slice_ids.insert(slice.id).second) {
            issues.push_back(ValidationIssue::of("slice_duplicate", "slice id appears more than once"));
        }

        const auto asset_issue = check_slice_assets(ledger, intent, slice);
        if (asset_issue.has_value()) {
            issues.push_back(*asset_issue);
            continue;
        }

        const auto& lane = ledger.lane(slice.lane_id);
        const auto amount_issue = check_slice_amounts(lane, intent, slice);
        if (amount_issue.has_value()) {
            issues.push_back(*amount_issue);
            continue;
        }

        const auto expected_target = ledger.quote(intent.source_asset, intent.target_asset, slice.source_amount);
        const auto price_issue = check_price(intent, slice, expected_target);
        if (price_issue.has_value()) {
            issues.push_back(*price_issue);
        }

        gross_source = gross_source.checked_add(slice.source_amount);
        gross_target = gross_target.checked_add(slice.quoted_target);
        operator_fee = operator_fee.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.operator_fee_bps));
        protocol_fee = protocol_fee.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.protocol_fee_bps));
        if (slice.maker_rebate) {
            rebate = rebate.checked_add(slice.quoted_target.checked_mul_bps_floor(lane.policy.rebate_bps));
        }
    }

    if (gross_source > intent.max_source) {
        issues.push_back(ValidationIssue::of("source_cap", "execution plan exceeds intent source amount"));
    }

    if (intent.max_source.is_positive() && intent.min_target.is_positive() && gross_source.is_positive()) {
        const auto required_target =
            intent.min_target.checked_mul_ratio_floor(gross_source.units, intent.max_source.units);
        if (gross_target < required_target) {
            issues.push_back(ValidationIssue::of("target_floor", "execution plan target amount is below intent floor"));
        }
    }

    const auto total_charges = operator_fee.checked_add(protocol_fee).checked_add(rebate);
    if (gross_target <= total_charges) {
        issues.push_back(ValidationIssue::of("fees", "execution plan target amount cannot cover fees"));
    }

    if (ledger.has_account(intent.owner) && gross_source.is_positive()) {
        if (ledger.balance_of(intent.owner, intent.source_asset) < gross_source) {
            issues.push_back(ValidationIssue::of("owner_balance", "intent owner balance is insufficient"));
        }
    }

    if (!issues.empty()) {
        return MatchResult::reject(std::move(issues));
    }

    return MatchResult::ok(gross_source, gross_target, operator_fee, protocol_fee, rebate);
}

}  // namespace aether
